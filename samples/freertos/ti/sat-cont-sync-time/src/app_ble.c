/*
 * Copyright (c) 2026 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ti/ble/app_util/framework/bleapputil_api.h"
#include "ti/ble/host/gap/gap_advertiser.h"
#include <ti/ble/host/gatt/gattservapp.h>
#include "ti/drivers/dpl/ClockP.h"
#include "ti/drivers/dpl/SemaphoreP.h"
#include "ti/drivers/dpl/TaskP.h"
#include <ti/log/Log.h>

#include <hubble/hubble.h>
#include <hubble/ble.h>

#include "app_ble.h"

#define HUBBLE_BLE_UUID_CONNECTABLE 0xFCA7
#define HUBBLE_BLE_BUFFER_LEN       31U

#define ADV_INTERVAL_MIN_MS         1000U
#define ADV_INTERVAL_MAX_MS         1200U

#define BLE_ADV_HEADER_SIZE         6U
#define HUBBLE_BLE_ADV_HEADER                                                  \
	0x03, GAP_ADTYPE_16BIT_COMPLETE, LO_UINT16(0xfca6), HI_UINT16(0xfca6), \
		0x01, GAP_ADTYPE_SERVICE_DATA,

/* TODO: replace this by actual command once finalized */
#define HUBBLE_CMD            0x02
#define HUBBLE_CMD_UNIX_EPOCH 0x02
#define HUBBLE_CMD_EPHEMERIS  0x07

/*
 * 128-bit UUIDs in little-endian byte order
 * Service: ef2dc9a1-40be-44b6-9dda-8a00fcd61dc0
 * Characteristic: ef2dc9a1-40be-44b6-9dda-8a00fcd61dc1
 */
static const uint8_t _hubble_svc_uuid[ATT_UUID_SIZE] = {
	0xc0, 0x1d, 0xd6, 0xfc, 0x00, 0x8a, 0xda, 0x9d,
	0xb6, 0x44, 0xbe, 0x40, 0xa1, 0xc9, 0x2d, 0xef,
};
static const uint8_t _hubble_chr_uuid[ATT_UUID_SIZE] = {
	0xc1, 0x1d, 0xd6, 0xfc, 0x00, 0x8a, 0xda, 0x9d,
	0xb6, 0x44, 0xbe, 0x40, 0xa1, 0xc9, 0x2d, 0xef,
};

static const gattAttrType_t _hubble_svc = {ATT_UUID_SIZE, _hubble_svc_uuid};
static uint8_t _hubble_chr_props = GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP;
static uint8_t _hubble_chr_val;

static gattAttribute_t _hubble_attr_tbl[] = {
	GATT_BT_ATT(primaryServiceUUID, GATT_PERMIT_READ, (uint8_t *)&_hubble_svc),
	GATT_BT_ATT(characterUUID, GATT_PERMIT_READ, &_hubble_chr_props),
	GATT_ATT(_hubble_chr_uuid, GATT_PERMIT_WRITE, &_hubble_chr_val),
};

/* Forward declare */
static void _hubble_conn_start(void);
static bStatus_t _write_attr_cb(uint16 conn_handle, gattAttribute_t *attr,
				uint8 *val, uint16 len, uint16 offset,
				uint8 method);

static const gattServiceCBs_t _hubble_svc_callbacks = {
	.pfnReadAttrCB = NULL,
	.pfnWriteAttrCB = _write_attr_cb,
	.pfnAuthorizeAttrCB = NULL};

/* BLE adv specifics */
static uint8_t _conn_adv_handle;
static uint8_t _beacon_adv_data[HUBBLE_BLE_BUFFER_LEN] = {HUBBLE_BLE_ADV_HEADER};

/* For sync time and update adv data */
static SemaphoreP_Struct _sync_sem_struct;
static SemaphoreP_Handle _sync_sem_handle;

/* App specific variables */
extern uint64_t unix_time_ms;
static uint16_t _conn_handle = LL_CONNHANDLE_INVALID;

/* Adv data */
static GapAdv_params_t _conn_adv_params = {
	.eventProps = GAP_ADV_PROP_CONNECTABLE | GAP_ADV_PROP_SCANNABLE |
		      GAP_ADV_PROP_LEGACY,
	.primIntMin = 160,
	.primIntMax = 160,
	.primChanMap = GAP_ADV_CHAN_ALL,
	.peerAddrType = PEER_ADDRTYPE_RANDOM_OR_RANDOM_ID,
	.filterPolicy = GAP_ADV_AL_POLICY_ANY_REQ,
	.txPower = GAP_ADV_TX_POWER_NO_PREFERENCE,
	.primPhy = GAP_ADV_PRIM_PHY_1_MBPS,
	.secPhy = GAP_ADV_SEC_PHY_1_MBPS,
	.sid = 0,
	.zeroDelay = 0};

// static uint8_t _conn_adv_data[] = {
// 	0x02, /* len = type + 1 */
// 	GAP_ADTYPE_FLAGS,
// 	GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

// 	0x03, /* len = type + 2 bytes UUID */
// 	GAP_ADTYPE_16BIT_COMPLETE,
// 	LO_UINT16(HUBBLE_BLE_UUID_CONNECTABLE),
// 	HI_UINT16(HUBBLE_BLE_UUID_CONNECTABLE),
// };

static uint8_t _conn_adv_data[] = {
	/* Complete list of 16-bit UUIDs */
	0x03,
	GAP_ADTYPE_16BIT_COMPLETE,
	LO_UINT16(HUBBLE_BLE_UUID_CONNECTABLE),
	HI_UINT16(HUBBLE_BLE_UUID_CONNECTABLE),

	/* Complete local name "HUBBLE-TI" (9 chars) */
	0x0A, /* len = type + 9 bytes */
	GAP_ADTYPE_LOCAL_NAME_COMPLETE,
	'H',
	'u',
	'b',
	'b',
	'l',
	'e',
	'-',
	'T',
	'I',

	/* Service data, UUID 0xFCA7 */
	0x03,
	GAP_ADTYPE_SERVICE_DATA,
	LO_UINT16(HUBBLE_BLE_UUID_CONNECTABLE),
	HI_UINT16(HUBBLE_BLE_UUID_CONNECTABLE),
};

static const BLEAppUtil_AdvInit_t _conn_adv_init_set = {
	.advDataLen = sizeof(_conn_adv_data),
	.advData = _conn_adv_data,
	.scanRespDataLen = 0,
	.scanRespData = NULL,
	.advParam = &_conn_adv_params,
};

const BLEAppUtil_AdvStart_t _hubble_start_adv_set = {
	/* Use the maximum possible value. This is the spec-defined maximum for */
	/* directed advertising and infinite advertising for all other types */
	.enableOptions = GAP_ADV_ENABLE_OPTIONS_USE_MAX,
	.durationOrMaxEvents = 0,
};

/* GATT cb */
static bStatus_t _write_attr_cb(uint16 conn_handle, gattAttribute_t *attr,
				uint8 *val, uint16 len, uint16 offset,
				uint8 method)
{
	/* Match by 128-bit UUID */
	if (attr->type.len != ATT_UUID_SIZE ||
	    memcmp(attr->type.uuid, _hubble_chr_uuid, ATT_UUID_SIZE) != 0) {
		return ATT_ERR_ATTR_NOT_FOUND;
	}

	/* No partial / blob writes */
	if (offset != 0) {
		return ATT_ERR_ATTR_NOT_LONG;
	}

	/* Need at least the 2 bytes cmd */
	if (len < 2) {
		return ATT_ERR_INVALID_VALUE_SIZE;
	}

	if (val[0] != HUBBLE_CMD) {
		return ATT_ERR_UNSUPPORTED_REQ;
	}

	switch (val[1]) {
	case HUBBLE_CMD_UNIX_EPOCH:
		if (len != 2 + sizeof(uint64_t)) {
			return ATT_ERR_INVALID_VALUE_SIZE;
		}

		memcpy(&unix_time_ms, &val[2], sizeof(unix_time_ms));

		/* Work around because Log cast to uintptr_t (32-bit) */
		Log_printf(Log_Dual_Stack, Log_DEBUG,
			   "Received UNIX time 0x%08x%08x ms",
			   (uint32_t)(unix_time_ms >> 32),
			   (uint32_t)unix_time_ms);
		break;

	case HUBBLE_CMD_EPHEMERIS:
		break;

	default:
		return ATT_ERR_UNSUPPORTED_REQ;
	}

	return SUCCESS;
}

/* Conn handlers */
static void _conn_event_handler(uint32 event, BLEAppUtil_msgHdr_t *pMsg)
{
	switch (event) {
	case BLEAPPUTIL_LINK_ESTABLISHED_EVENT:
		_conn_handle = ((gapEstLinkReqEvent_t *)pMsg)->connectionHandle;
		Log_printf(Log_Dual_Stack, Log_DEBUG, "Client connected");

		break;

	case BLEAPPUTIL_LINK_TERMINATED_EVENT:
		_conn_handle = LL_CONNHANDLE_INVALID;
		Log_printf(Log_Dual_Stack, Log_DEBUG, "Disconnected");

		if (unix_time_ms != 0) {
			(void)hubble_time_set(unix_time_ms);
			SemaphoreP_post(_sync_sem_handle);

		} else {
			_hubble_conn_start();
		}
		break;
	}
}

static BLEAppUtil_EventHandler_t _conn_handler = {
	.handlerType = BLEAPPUTIL_GAP_CONN_TYPE,
	.pEventHandler = _conn_event_handler,
	.eventMask = BLEAPPUTIL_LINK_ESTABLISHED_EVENT |
		     BLEAPPUTIL_LINK_TERMINATED_EVENT,
};

static void _hubble_conn_start(void)
{
	if (BLEAppUtil_advStart(_conn_adv_handle, &_hubble_start_adv_set) !=
	    SUCCESS) {
		Log_printf(Log_Dual_Stack, Log_ERROR,
			   "Failed to start connectable advertising");
	}
}

static void _adv_init(void)
{
	bStatus_t status;

	/* register GATT service and conn handler */
	status = GATTServApp_RegisterService(
		_hubble_attr_tbl, GATT_NUM_ATTRS(_hubble_attr_tbl),
		GATT_MAX_ENCRYPT_KEY_SIZE, &_hubble_svc_callbacks);
	if (status != SUCCESS) {
		Log_printf(Log_Dual_Stack, Log_ERROR,
			   "Failed to register GATT service");
		return;
	}

	status = BLEAppUtil_registerEventHandler(&_conn_handler);
	if (status != SUCCESS) {
		Log_printf(Log_Dual_Stack, Log_ERROR,
			   "Failed to register connection event handler");
		return;
	}

	/* Init adv sets */
	status = BLEAppUtil_initAdvSet(&_conn_adv_handle, &_conn_adv_init_set);
	if (status != SUCCESS) {
		Log_printf(
			Log_Dual_Stack,
			Log_ERROR, "Failed to initialize connectable advertising set, err: %d",
			status);
		return;
	}
}

/* Public API */
bStatus_t ble_init(void)
{
	/* Create sems */
	_sync_sem_handle = SemaphoreP_constructBinary(&_sync_sem_struct, 0);
	if (_sync_sem_handle == NULL) {
		return (FAILURE);
	}

	return BLEAppUtil_invokeFunction(
		(InvokeFromBLEAppUtilContext_t)_adv_init, NULL);
}

bStatus_t ble_hubble_sync(void)
{
	if (BLEAppUtil_invokeFunction(
		    (InvokeFromBLEAppUtilContext_t)_hubble_conn_start, NULL) !=
	    SUCCESS) {
		Log_printf(Log_Dual_Stack, Log_ERROR,
			   "Failed to start connectable advertising");
		return (FAILURE);
	}

	Log_printf(Log_Dual_Stack, Log_INFO,
		   "Waiting for time sync...");

	/* Wait for sync to complete */
	(void)SemaphoreP_pend(_sync_sem_handle, SemaphoreP_WAIT_FOREVER);

	return (SUCCESS);
}

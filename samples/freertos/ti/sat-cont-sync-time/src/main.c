/*
 * Copyright (c) 2026 Hubble Network, Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <FreeRTOS.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <task.h>

#include <ti/devices/DeviceFamily.h>
#include <ti/drivers/Power.h>
#include "ti/drivers/dpl/ClockP.h"
#include "ti/drivers/dpl/SemaphoreP.h"
#include <ti/log/Log.h>

#include "ti_ble_config.h"
#include "ti/ble/stack_util/icall/app/icall.h"
#include "ti/ble/stack_util/health_toolkit/assert.h"

#ifndef USE_DEFAULT_USER_CFG
#include "ti/ble/app_util/config/ble_user_config.h"
/* BLE user defined configuration */
icall_userCfg_t user0Cfg = BLE_USER_CFG;
#endif /* USE_DEFAULT_USER_CFG */

#include "ti/ble/app_util/framework/bleapputil_api.h"

#include <hubble/hubble.h>
#include <hubble/sat/packet.h>

#include "app_ble.h"

#define SLEEP_PERIOD_MS 10000

/* Key and time */
static uint8_t _hubble_key[CONFIG_HUBBLE_KEY_SIZE];
uint64_t unix_time_ms;

BLEAppUtil_GeneralParams_t appMainParams = {
	.taskPriority = 1,
	.taskStackSize = 4096,
	.profileRole = (BLEAppUtil_Profile_Roles_e)(HOST_CONFIG),
	.addressMode = DEFAULT_ADDRESS_MODE,
	.deviceNameAtt = attDeviceName,
	.pDeviceRandomAddress = pRandomAddress,
};

static BLEAppUtil_PeriCentParams_t appMainPeriCentParams;

void criticalErrorHandler(int32 errorCode, void *pInfo)
{
	(void)errorCode;
	(void)pInfo;
}

void App_StackInitDoneHandler(gapDeviceInitDoneEvent_t *deviceInitDoneData)
{
	(void)deviceInitDoneData;
}

void *main_thread_entry(void *arg0)
{
	(void)arg0;

	struct hubble_sat_packet packet = {0};
	bStatus_t status;
	int ret;

	if (ble_init() != SUCCESS) {
		Log_printf(Log_Dual_Stack, Log_ERROR, "Failed to initialize BLE");
	}

	/* Wait for time and orbital params sync */
	status = ble_hubble_sync();
	if (status != SUCCESS) {
		Log_printf(Log_Dual_Stack, Log_ERROR,
			   "Failed to sync time and orbital params, err: %d",
			   status);
		return NULL;
	}

	for (;;) {
		ret = hubble_sat_packet_get(&packet, NULL, 0);
		if (ret != 0) {
			/* TODO: Call Error Handler */
			return NULL;
		}

		ret = hubble_sat_packet_send(&packet,
					     HUBBLE_SAT_RELIABILITY_NONE);
		if (ret != 0) {
			/* TODO: Call Error Handler */
			return NULL;
		}
		vTaskDelay(pdMS_TO_TICKS(SLEEP_PERIOD_MS));
	}

	return NULL;
}

int main()
{
	pthread_t thread;
	pthread_attr_t attrs;
	struct sched_param priParam;
	int ret;

	Board_init();

	/* TODO:
	 * hubble init MUST be called first before starting the BLE stack
	 * because it set up both the BT and customRF stacks. Otherwise, BT will
	 * work but you can not disable it.
	 */
	unix_time_ms = 0xdeadbeef;
	ret = hubble_init(unix_time_ms, _hubble_key);
	if (ret != 0) {
		return ret;
	}

	/* Part of the workaround so time sync works correctlys */
	unix_time_ms = 0U;

	/* Update User Configuration of the stack */
	user0Cfg.appServiceInfo->timerTickPeriod = ICall_getTickPeriod();
	user0Cfg.appServiceInfo->timerMaxMillisecond = ICall_getMaxMSecs();

	BLEAppUtil_init(&criticalErrorHandler, &App_StackInitDoneHandler,
			&appMainParams, &appMainPeriCentParams);

	/* Initialize the attributes structure with default values */
	ret = pthread_attr_init(&attrs);
	if (ret != 0) {
		return ret;
	}

	/* Set priority, detach state, and stack size attributes */
	priParam.sched_priority = 1;
	ret = pthread_attr_setschedparam(&attrs, &priParam);
	ret |= pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
	ret |= pthread_attr_setstacksize(&attrs, 2048);
	if (ret != 0) {
		/* failed to set attributes */
		while (1) {
		}
	}

	ret = pthread_create(&thread, &attrs, main_thread_entry, NULL);
	if (ret != 0) {
		Log_printf(Log_Dual_Stack, Log_ERROR,
			   "Failed to create main thread");
		return ret;
	}

	/* Start the FreeRTOS scheduler */
	vTaskStartScheduler();

	return 0;
}

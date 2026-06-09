/*
 * Copyright (c) 2026 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file app_ble.h
 * @brief Bluetooth LE advertising interface.
 *
 * Provides functions to initialize the BLE stack and control advertising.
 */

#ifndef APP_BLE_H
#define APP_BLE_H

#include "ti/ble/stack_util/bcomdef.h"

/**
 * @brief Initialize the Bluetooth LE subsystem.
 *
 * Registers GATT services, sets up the advertising data, and prepares the
 * controller for use. Must be called once before any other BLE function.
 *
 * @return SUCCESS or error code on failure.
 */
bStatus_t ble_init(void);

/**
 * @brief Synchronize time and orbital parameters data.
 *
 * Synchronizes the current time and orbital parameters data from
 * the Hubble Network Backend.
 * This starts a connectable advertising session. @ref ble_adv_stop must be
 * called to stop the ongoing broadcasting.
 *
 * @return SUCCESS or error code on failure.
 */
bStatus_t ble_hubble_sync(void);

#endif /* APP_BLE_H */

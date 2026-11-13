/*
 * Copyright (c) 2026 Hubble Network, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>

/* Standard C Libraries */
#include <errno.h>

/* Hubble */
#include <hubble/port/sat_radio.h>
#include <hubble/sat/packet.h>

#include "esp_sat_radio_config.h"
#include "sat_board.h"

#define HUBBLE_BASE_FREQUENCY 2482U
#define MAX_TX_POWER_DBM      20U

static const struct device *const _timer_dev =
	DEVICE_DT_GET(DT_CHILD(DT_NODELABEL(timer0), counter));

/*
 * This semaphore is used to protect a packet transmission and avoid
 * race conditions.
 */
K_SEM_DEFINE(_transmit_sem, 1, 1);

/**
 * This semaphore is used between symbol transmissions. It allows the current
 * thread to wait a symbol transmission without need to poll for a condition.
 */
K_SEM_DEFINE(_symbol_sem, 0, 1);

/* Current sat power */
static uint8_t _sat_power_dbm = MAX_TX_POWER_DBM;

/* Espressif PHY API */
extern void phy_set_step_01k(bool step_01k);
extern void phy_tx_tone(bool txtone_en, bool bt_mode, uint8_t pwr_index);
extern void phy_set_freq(uint16_t freq_mhz, int offset);

static void _timer_cb(const struct device *counter_dev, uint8_t chan_id,
		      uint32_t ticks, void *user_data)
{
	k_sem_give(&_symbol_sem);
}

static struct counter_alarm_cfg _alarm_cfg = {
	.flags = 0,
	.callback = _timer_cb,
	.user_data = NULL,
};

static int _timer_init(void)
{
	if (!device_is_ready(_timer_dev)) {
		return -ENODEV;
	}

	return 0;
}

static int _timer_start(uint32_t period_us)
{
	_alarm_cfg.ticks = counter_us_to_ticks(_timer_dev, period_us);
	return counter_set_channel_alarm(_timer_dev, 0, &_alarm_cfg);
}

static int _timer_enable(void)
{
	return counter_start(_timer_dev);
}

static int _timer_disable(void)
{
	(void)counter_cancel_channel_alarm(_timer_dev, 0);
	return counter_stop(_timer_dev);
}

static int _radio_cw_start(uint16_t step, uint32_t delay, uint32_t duration_us)
{
	int ret;

	/* Symbol on time */
	phy_set_freq(HUBBLE_BASE_FREQUENCY, step);
	phy_tx_tone(true, true, _sat_power_dbm);

	ret = _timer_start(duration_us);
	if (ret != 0) {
		phy_tx_tone(false, true, _sat_power_dbm);
		return ret;
	}

	k_sem_take(&_symbol_sem, K_FOREVER);

	/* Symbol off time */
	phy_tx_tone(false, true, _sat_power_dbm);

	ret = _timer_start(delay);
	if (ret != 0) {
		return ret;
	}

	k_sem_take(&_symbol_sem, K_FOREVER);
	return 0;
}

int hubble_sat_board_init(void)
{
	return _timer_init();
}

int hubble_sat_board_enable(void)
{
	int ret = _timer_enable();
	if (ret != 0) {
		return ret;
	}

	phy_set_step_01k(true);
	return 0;
}

int hubble_sat_board_disable(void)
{
	phy_set_step_01k(false);
	return _timer_disable();
}

int hubble_sat_board_packet_send(const struct hubble_sat_packet_frames *packet)
{
	int ret = 0;
	int8_t frame = -1;

	k_sem_take(&_transmit_sem, K_FOREVER);
	k_sem_reset(&_symbol_sem);

	for (size_t i = 0; i < packet->total_number_of_symbols; i++) {
		uint16_t step;
		uint8_t data_pos = i % HUBBLE_PACKET_FRAME_PAYLOAD_MAX_SIZE;

		if (data_pos == 0) {
			frame++;
		}

		step = ESP_STEP_SCALE(
			packet->frame[frame].data[data_pos] +
			HUBBLE_CHANNEL_OFFSET(packet->frame[frame].channel));

		/*
		 * Because there's a certain delay for the radio to ramp up,
		 * subtract the compensation to the off time delay
		 */
		ret = _radio_cw_start(
			step, HUBBLE_WAIT_SYMBOL_OFF_US - ESP_RADIO_OFF_DELAY_US,
			HUBBLE_WAIT_SYMBOL_US - ESP_RADIO_ON_DELAY_US);

		if (ret != 0) {
			break;
		}
	}

	k_sem_give(&_transmit_sem);
	return ret;
}

#ifdef CONFIG_HUBBLE_SAT_NETWORK_DTM_MODE

int hubble_sat_board_power_set(int8_t power)
{
	if (power < 0 || power > MAX_TX_POWER_DBM) {
		return -EINVAL;
	}

	_sat_power_dbm = power;
	return 0;
}

int hubble_sat_board_cw_start(uint8_t channel)
{
	uint16_t step = ESP_STEP_SCALE(32 + HUBBLE_CHANNEL_OFFSET(channel));
	phy_set_freq(HUBBLE_BASE_FREQUENCY, step);
	phy_tx_tone(true, true, _sat_power_dbm);
	return 0;
}

int hubble_sat_board_cw_stop(void)
{
	phy_tx_tone(false, true, _sat_power_dbm);
	return 0;
}

#endif /* CONFIG_HUBBLE_SAT_NETWORK_DTM_MODE */

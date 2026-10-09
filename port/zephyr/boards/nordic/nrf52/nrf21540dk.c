/*
 * Copyright (c) 2025 Hubble Network, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>

#include <zephyr/kernel.h>

#include <hubble/sat.h>
#include <hubble/sat/packet.h>
#include <sat_soc.h>

#include "fem.h"

#ifdef CONFIG_MPSL_FEM_ONLY
#include <mpsl_fem_protocol_api.h>

/* Frequency passed to the power split */
#define FEM_SPLIT_FREQ_MHZ 2482

/* From Fig 8, p.19 of the datasheet */
static const mpsl_tx_power_split_t soc_to_fem_gain_table[] = {
	{-8, 7}, /* 0 dBm */
	{-4, 5}, /* 1 dBm */
	{-8, 9}, /* 2 dBm */
	{-8, 9}, /* 3 dBm */
	{0, 4},  /* 4 dBm */
	{0, 5},  /* 5 dBm */
	{0, 5},  /* 6 dbm */
	{3, 3},  /* 7 dBm */
	{0, 6},  /* 8 dBm */
	{0, 6},  /* 9 dBm */
	{0, 7},  /* 10 dBm */
};
#endif

int hubble_sat_board_init(void)
{
	hubble_board_fem_setup();
	return 0;
}

int hubble_sat_board_enable(void)
{
	/* Set power, enable pa ... */
	return hubble_sat_soc_enable();
}

int hubble_sat_board_disable(void)
{
	return hubble_sat_soc_disable();
}

int hubble_sat_board_packet_send(const struct hubble_sat_packet_frames *packet)
{
	int ret;

	hubble_board_fem_enable();
	ret = hubble_sat_soc_packet_send(packet);
	hubble_board_fem_sleep();

	return ret;
}

#ifdef CONFIG_HUBBLE_SAT_NETWORK_DTM_MODE

int hubble_sat_board_power_set(int8_t power)
{
#ifdef CONFIG_MPSL_FEM_ONLY
	mpsl_tx_power_split_t split = {0};
	int ret;

	if (power > 10) {
		return -EINVAL;
	}

	if (power >= 0 && power <= 10) {
		split.radio_tx_power =
			soc_to_fem_gain_table[power].radio_tx_power;
		split.fem_pa_power_control =
			soc_to_fem_gain_table[power].fem_pa_power_control;
	} else {
		/* Split total power into SoC power + FEM gain, like radio_test */
		(void)mpsl_fem_tx_power_split(power, &split, MPSL_PHY_BLE_1M,
					      FEM_SPLIT_FREQ_MHZ, false);
	}

	ret = hubble_sat_soc_power_set(split.radio_tx_power);
	if (ret != 0) {
		return ret;
	}

	return (mpsl_fem_pa_power_control_set(split.fem_pa_power_control) == 0)
		       ? 0
		       : -EINVAL;
#else
	return hubble_sat_soc_power_set(power);
#endif
}

int hubble_sat_board_cw_start(uint8_t channel)
{
	hubble_board_fem_cw_enable();
	return hubble_sat_soc_cw_start(channel);
}

int hubble_sat_board_cw_stop(void)
{
	hubble_board_fem_sleep();
	return hubble_sat_soc_cw_stop();
}

#endif /* CONFIG_HUBBLE_SAT_NETWORK_DTM_MODE */

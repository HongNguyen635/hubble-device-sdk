/*
 * Copyright (c) 2026 Hubble Network, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if !defined(CONFIG_MPSL_FEM_API_AVAILABLE) || defined(CONFIG_MPSL_FEM_ONLY)

void hubble_board_fem_setup(void);

void hubble_board_fem_enable(void);

void hubble_board_fem_cw_enable(void);

void hubble_board_fem_bypass(void);

void hubble_board_fem_sleep(void);

#else

static inline void hubble_board_fem_setup(void)
{
}

static inline void hubble_board_fem_enable(void)
{
}

static inline void hubble_board_fem_cw_enable(void)
{
}

static inline void hubble_board_fem_bypass(void)
{
}

static inline void hubble_board_fem_sleep(void)
{
}

#endif /* !CONFIG_MPSL_FEM_API_AVAILABLE || CONFIG_MPSL_FEM_ONLY */

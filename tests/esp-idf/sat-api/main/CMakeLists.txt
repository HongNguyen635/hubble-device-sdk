# Copyright (c) 2026 Hubble Network, Inc.
# SPDX-License-Identifier: Apache-2.0

idf_component_register(
    SRCS "test_sat_api.c"
    PRIV_REQUIRES
        unity
        hubblenetwork-sdk
)

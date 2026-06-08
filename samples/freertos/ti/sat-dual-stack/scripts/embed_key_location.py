#!/usr/bin/env python3
#
# Copyright (c) 2026 Hubble Network, Inc.
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import base64
import geocoder

KEY_TEMPLATE = """
/*
 * This file contents was automatically generated.
 */
#define HUBBLE_KEY_SET 1

static uint8_t hubble_key[CONFIG_HUBBLE_KEY_SIZE] = {key};
"""

LOCATION_TEMPLATE = """
/*
* This file contents was automatically generated.
*/
#define DEV_LOCATION_SET 1

static struct hubble_sat_device_pos device_pos = {{
    .lat = {lat},
    .lon = {lon},
}};
"""


def write_data(key: str, output_dir: str):
    key_bytes = base64.b64decode(key)
    key_hex = "{" + ", ".join([hex(x) for x in key_bytes]) + "}"

    with open(f"{output_dir}/key.c", "w") as f:
        f.write(KEY_TEMPLATE.format(key=key_hex))
    print(f"Wrote {output_dir}/key.c ({len(key_bytes)} bytes)")

    g = geocoder.ip("me")
    if not g.ok or g.latlng is None:
        raise RuntimeError(f"IP geolocation failed: {g.status}")
    lat, lon = g.latlng

    with open(f"{output_dir}/location.c", "w") as f:
        f.write(LOCATION_TEMPLATE.format(lat=lat, lon=lon))
    print(f"Wrote {output_dir}/location.c (lat={lat}, lon={lon})")


def main():
    parser = argparse.ArgumentParser(description="Generate device key and location")
    parser.add_argument(
        "--key", required=True, help="Base64 key to embed in the device"
    )
    parser.add_argument(
        "-o", "--output-dir", default=".", help="Directory to write the generated files"
    )
    args = parser.parse_args()
    write_data(args.key, args.output_dir)


if __name__ == "__main__":
    main()

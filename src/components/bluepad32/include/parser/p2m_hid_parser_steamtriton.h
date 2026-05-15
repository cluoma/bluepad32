// SPDX-License-Identifier: Apache-2.0, MIT
// Copyright 2026 Colin Luoma

#ifndef P2M_HID_PARSER_STEAMTRITON_H
#define P2M_HID_PARSER_STEAMTRITON_H

#include <stdint.h>

#include "parser/uni_hid_parser.h"

// Steam devices
void uni_hid_parser_steamtriton_setup(struct uni_hid_device_s* d);
void uni_hid_parser_steamtriton_init_report(struct uni_hid_device_s* d);
void uni_hid_parser_steamtriton_parse_input_report(struct uni_hid_device_s* d, const uint8_t* report, uint16_t len);

#endif //P2M_HID_PARSER_STEAMTRITON_H

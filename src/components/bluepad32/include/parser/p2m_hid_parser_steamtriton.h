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
void uni_hid_parser_steamtriton_play_dual_rumble(struct uni_hid_device_s* d,
                                         uint16_t start_delay_ms,
                                         uint16_t duration_ms,
                                         uint8_t weak_magnitude,
                                         uint8_t strong_magnitude);

#endif //P2M_HID_PARSER_STEAMTRITON_H

// SPDX-License-Identifier: Apache-2.0, MIT
// Copyright 2019 Ricardo Quesada
// 2021-2022 John "Nielk1" Klein
// http://retro.moe/unijoysticle2

#ifndef P2M_HID_PARSER_DREAMWAVE_H
#define P2M_HID_PARSER_DREAMWAVE_H

#include <stdint.h>

#include "parser/uni_hid_parser.h"

// For DualSense gamepads
void uni_hid_parser_dreamwave_setup(struct uni_hid_device_s* d);
void uni_hid_parser_dreamwave_init_report(struct uni_hid_device_s* d);
void uni_hid_parser_dreamwave_parse_input_report(struct uni_hid_device_s* d, const uint8_t* report, uint16_t len);
void uni_hid_parser_dreamwave_parse_feature_report(struct uni_hid_device_s* d, const uint8_t* report, uint16_t len);
void uni_hid_parser_dreamwave_device_dump(struct uni_hid_device_s* d);

#endif  // P2M_HID_PARSER_DREAMWAVE_H

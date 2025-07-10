// SPDX-License-Identifier: Apache-2.0, MIT
// Copyright 2019 Ricardo Quesada
// 2021-2022 John "Nielk1" Klein
// http://retro.moe/unijoysticle2

// Technical info taken from:
// https://aur.archlinux.org/cgit/aur.git/tree/hid-playstation.c?h=hid-playstation-dkms
// https://gist.github.com/Nielk1/6d54cc2c00d2201ccb8c2720ad7538db
// https://controllers.fandom.com/wiki/Sony_DualSense

#include "parser/p2m_hid_parser_dreamwave.h"

#include <assert.h>

#include "bt/uni_bt_defines.h"
#include "uni_config.h"
#include "uni_hid_device.h"
#include "uni_log.h"
#include "uni_utils.h"

typedef enum {
    DS5_STATE_INITIAL,
    DS5_STATE_PAIRING_INFO_REQUEST,
    DS5_STATE_FIRMWARE_VERSION_REQUEST,
    DS5_STATE_CALIBRATION_REQUEST,
    DS5_STATE_READY,
} dreamwave_state_t;

typedef struct {
    uint8_t output_seq;
    dreamwave_state_t state;
    uint32_t hw_version;
    uint32_t fw_version;
    uint16_t update_version;

} dreamwave_instance_t;
_Static_assert(sizeof(dreamwave_instance_t) < HID_DEVICE_MAX_PARSER_DATA, "Dreamwave instance too big");

/* Main DualSense input report excluding any BT/USB specific headers. */
typedef struct __attribute((packed)) {
    uint8_t x, y;
    uint8_t rx, ry;
    uint8_t buttons[2];
} dreamwave_input_report_t;

static dreamwave_instance_t* get_dreamwave_instance(uni_hid_device_t* d);
// static void dreamwave_request_pairing_info_report(uni_hid_device_t* d);
// static void dreamwave_request_firmware_version_report(uni_hid_device_t* d);

void uni_hid_parser_dreamwave_init_report(uni_hid_device_t* d) {
    uni_controller_t* ctl = &d->controller;
    memset(ctl, 0, sizeof(*ctl));

    ctl->klass = UNI_CONTROLLER_CLASS_GAMEPAD;
}

void uni_hid_parser_dreamwave_setup(uni_hid_device_t* d) {
    dreamwave_instance_t* ins = get_dreamwave_instance(d);
    memset(ins, 0, sizeof(*ins));

    uni_hid_device_set_ready_complete(d);
}

void uni_hid_parser_dreamwave_parse_input_report(uni_hid_device_t* d, const uint8_t* report, uint16_t len) {
    dreamwave_instance_t* ins = get_dreamwave_instance(d);

    // for (int i = 0; i < len; i ++)
    // {
    //     printf("%02x ", report[i]);
    // }
    // printf("\r\n");

    // // Don't process reports until state is ready. Prevents possible div-by-0 on calibration
    // // and ignores other warnings.
    // if (ins->state != DS5_STATE_READY)
    //     return;

    // if (report[0] != 0x31) {
    //     loge("DS5: Unexpected report type: got 0x%02x, want: 0x31\n", report[0]);
    //     return;
    // }
    // if (len != 78) {
    //     loge("DS5: Unexpected report len: got %d, want: 78\n", len);
    //     return;
    // }

    uni_controller_t* ctl = &d->controller;
    const dreamwave_input_report_t* r = (dreamwave_input_report_t*)&report[1];

    // Axis
    ctl->gamepad.axis_x = (r->x - 128) * 4;
    ctl->gamepad.axis_y = (r->y - 128) * 4;
    ctl->gamepad.throttle = (r->rx) * 4;
    ctl->gamepad.brake = (r->ry) * 4;

    // Buttons
    if (r->buttons[0] & 0x01)
        ctl->gamepad.buttons |= BUTTON_A;  // West
    if (r->buttons[0] & 0x02)
        ctl->gamepad.buttons |= BUTTON_B;  // South
    if (r->buttons[0] & 0x04)
        ctl->gamepad.buttons |= BUTTON_X;  // East
    if (r->buttons[0] & 0x08)
        ctl->gamepad.buttons |= BUTTON_Y;  // North
    if (r->buttons[0] & 0x10)
        ctl->gamepad.dpad |= DPAD_UP;  // West
    if (r->buttons[0] & 0x20)
        ctl->gamepad.dpad |= DPAD_DOWN;  // South
    if (r->buttons[0] & 0x40)
        ctl->gamepad.dpad |= DPAD_LEFT;  // East
    if (r->buttons[0] & 0x80)
        ctl->gamepad.dpad |= DPAD_RIGHT;  // North

    if (r->buttons[1] & 0x01)
        ctl->gamepad.misc_buttons |= MISC_BUTTON_START;
}

void uni_hid_parser_dreamwave_device_dump(uni_hid_device_t* d) {
    dreamwave_instance_t* ins = get_dreamwave_instance(d);
    logi("\tDreamwave: FW version: %#x, HW version: %#x, update version: %#x\n", ins->fw_version,
         ins->hw_version, ins->update_version);
}

//
// Helpers
//
static dreamwave_instance_t* get_dreamwave_instance(uni_hid_device_t* d) {
    return (dreamwave_instance_t*)&d->parser_data[0];
}
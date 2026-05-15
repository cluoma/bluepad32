// SPDX-License-Identifier: Apache-2.0, MIT
// Copyright 2026 Colin Luoma


#include "parser/p2m_hid_parser_steamtriton.h"

#include "controller/uni_controller.h"
#include "hid_usage.h"
#include "uni_common.h"
#include "uni_hid_device.h"
#include "uni_log.h"

#define STEAMCONTROLLER2_OUTPUT_REPORT_SIZE 54

typedef struct __attribute__ ((packed)) {

    uint8_t id;
    uint8_t counter;

    // buttons
    struct {
        uint8_t btn_a : 1;
        uint8_t btn_b : 1;
        uint8_t btn_x : 1;
        uint8_t btn_y : 1;
        uint8_t btn_dots   : 1;
        uint8_t btn_rstick  : 1;
        uint8_t btn_start: 1;
        uint8_t btn_r4: 1;

        uint8_t btn_r5: 1;
        uint8_t btn_rshoulder: 1;
        uint8_t btn_dpad_down: 1;
        uint8_t btn_dpad_right: 1;
        uint8_t btn_dpad_left : 1;
        uint8_t btn_dpad_up: 1;
        uint8_t btn_select: 1;
        uint8_t btn_lstick: 1;

        uint8_t btn_home : 1;
        uint8_t btn_l4 : 1;
        uint8_t btn_l5 : 1;
        uint8_t btn_lshoulder : 1;
        uint8_t btn_rstick_touch : 1;  // senses touching the right analog stick
        uint8_t btn_rpad_touch : 1;    // senses touching the right track pad
        uint8_t btn_rpad_click : 1;    // right track pad is clicked
        uint8_t btn_rtrigger : 1;

        uint8_t btn_lstick_touch : 1;  // senses touching the left analog stick
        uint8_t btn_lpad_touch : 1;    // senses touching the left track pad
        uint8_t btn_lpad_click : 1;    // left track pad is clicked
        uint8_t btn_ltrigger : 1;
        uint8_t btn_rgrip_hold : 1;    // senses when the right-side grip of the controller is held/touched
        uint8_t btn_lgrip_hold : 1;    // senses when the left-side grip of the controller is held/touched
        uint8_t pad : 1;
        uint8_t pad2 : 1;
    };

    int16_t trigger_left;  // max 0x7fff
    int16_t trigger_right;

    int16_t joy_x;  // min: 0x8001 max: 0x7fff
    int16_t joy_y;  // min: 0x8001 max: 0x7fff
    int16_t joy_x2;
    int16_t joy_y2;

    uint8_t lpad[6];
    uint8_t rpad[6];
} steamcontrollertriton_report_t;

static hci_con_handle_t connection_handle;
static uni_hid_device_t* device;

void uni_hid_parser_steamtriton_setup(struct uni_hid_device_s* d) {
    device = d;
    connection_handle = d->conn.handle;

    // Set the type of controller class once.
    uni_controller_t* ctl = &d->controller;
    memset(ctl, 0, sizeof(*ctl));

    ctl->klass = UNI_CONTROLLER_CLASS_GAMEPAD;

    uni_hid_device_set_ready_complete(d);
}

void uni_hid_parser_steamtriton_init_report(uni_hid_device_t* d) {
    ARG_UNUSED(d);
}

void uni_hid_parser_steamtriton_parse_input_report(struct uni_hid_device_s* d, const uint8_t* report, uint16_t len) {

    if (report[0] != 0x45 || len < sizeof(steamcontrollertriton_report_t))
    {
        // controller input report
        return;
    }

    uni_controller_t* ctl = &d->controller;

    steamcontrollertriton_report_t gp_report;
    memcpy(&gp_report, report, sizeof(gp_report));

    ctl->gamepad.dpad = 0;
    ctl->gamepad.buttons = 0;
    ctl->gamepad.misc_buttons = 0;

    if (gp_report.btn_a) ctl->gamepad.buttons |= BUTTON_A;
    if (gp_report.btn_b) ctl->gamepad.buttons |= BUTTON_B;
    if (gp_report.btn_x) ctl->gamepad.buttons |= BUTTON_X;
    if (gp_report.btn_y) ctl->gamepad.buttons |= BUTTON_Y;

    if (gp_report.btn_start)  ctl->gamepad.misc_buttons |= MISC_BUTTON_START;
    if (gp_report.btn_select) ctl->gamepad.misc_buttons |= MISC_BUTTON_SELECT;
    if (gp_report.btn_home)   ctl->gamepad.misc_buttons |= MISC_BUTTON_SYSTEM;

    if (gp_report.btn_dpad_up)    ctl->gamepad.dpad |= DPAD_UP;
    if (gp_report.btn_dpad_down)  ctl->gamepad.dpad |= DPAD_DOWN;
    if (gp_report.btn_dpad_left)  ctl->gamepad.dpad |= DPAD_LEFT;
    if (gp_report.btn_dpad_right) ctl->gamepad.dpad |= DPAD_RIGHT;

    if (gp_report.btn_lshoulder) ctl->gamepad.buttons |= BUTTON_SHOULDER_L;
    if (gp_report.btn_rshoulder) ctl->gamepad.buttons |= BUTTON_SHOULDER_R;

    ctl->gamepad.axis_x = gp_report.joy_x / 64;
    ctl->gamepad.axis_y = (-1) * gp_report.joy_y / 64;
    ctl->gamepad.axis_rx = gp_report.joy_x2 / 64;
    ctl->gamepad.axis_ry = (-1) * gp_report.joy_y2 / 64;
    ctl->gamepad.throttle = gp_report.trigger_right / 32;
    ctl->gamepad.brake = gp_report.trigger_left / 32;
}


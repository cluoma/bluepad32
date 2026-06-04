// SPDX-License-Identifier: Apache-2.0, MIT
// Copyright 2026 Colin Luoma


#include "parser/p2m_hid_parser_steamtriton.h"

#include "controller/uni_controller.h"
#include "hid_usage.h"
#include "uni_common.h"
#include "uni_hid_device.h"
#include "uni_log.h"

#define STEAMCONTROLLER2_OUTPUT_REPORT_SIZE 54

static const uint8_t valve_service_uuid[16] = {
    0x10, 0x0F, 0x6C, 0x32, 0x17, 0x35, 0x43, 0x13,
    0xB4, 0x02, 0x38, 0x56, 0x71, 0x31, 0xE5, 0xF3
};

static const uint8_t valve_characteristic_rumble_uuid[16] = {
    0x10, 0x0f, 0x6c, 0xB5, 0x17, 0x35, 0x43, 0x13,
    0xb4, 0x02, 0x38, 0x56, 0x71, 0x31, 0xe5, 0xf3
};

typedef enum {
    QUERYING_SERVICES,
    FOUND_VALVE_SERVICE,
    QUERYING_CHARACTERISTICS,
    FOUND_CHARACTERISTICS // this is a single state because we only care about the haptic rumble characteristic
                          // should probably add a bitfield somewhere to check off which characteristics we found
} steamtriton_state_ble_t;

typedef enum {
    STEAMTRITON_STATE_RUMBLE_DISABLED,
    STEAMTRITON_STATE_RUMBLE_DELAYED,
    STEAMTRITON_STATE_RUMBLE_IN_PROGRESS,
} steamtriton_state_rumble_t;

typedef struct {
    // For BLE states and characteristics
    steamtriton_state_ble_t state_ble;
    gatt_client_service_t valve_service;
    gatt_client_characteristic_t valve_haptic_rumble_characteristic;

    // For delayed rumble and durations
    btstack_timer_source_t rumble_timer_duration;
    btstack_timer_source_t rumble_timer_delayed_start;
    steamtriton_state_rumble_t rumble_state;

    uint16_t rumble_weak_magnitude;
    uint16_t rumble_strong_magnitude;
    uint16_t rumble_duration_ms;

    uint8_t prev_rumble_weak_magnitude;
    uint8_t prev_rumble_strong_magnitude;
} steamtriton_instance_t;
_Static_assert(sizeof(steamtriton_instance_t) < HID_DEVICE_MAX_PARSER_DATA, "SteamTriton instance too big");

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

typedef struct __attribute__ ((packed)) {
    uint8_t type;
    uint16_t intensity;
    uint16_t speed_left;
    int8_t gain_left;
    uint16_t speed_right;
    int8_t gain_right;
} steamcontrollertriton_rumble_report_t;

// Forward declarations
static steamtriton_instance_t* get_steamtriton_instance(uni_hid_device_t* d);
static void steamtriton_play_dual_rumble_now(uni_hid_device_t* d,
                                     uint16_t duration_ms,
                                     uint8_t weak_magnitude,
                                     uint8_t strong_magnitude);
static void on_steamtriton_set_rumble_on(btstack_timer_source_t* ts);
static void on_steamtriton_set_rumble_off(btstack_timer_source_t* ts);

static void uni_steamtriton_handle_gatt_client_event(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size) {
    uint8_t att_status;
    gatt_client_service_t service;
    gatt_client_characteristic_t characteristic;

    ARG_UNUSED(channel);
    ARG_UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event = hci_event_packet_get_type(packet);

    // Get the uni device for this packet and device instance
    uint16_t con_handle = gatt_event_service_query_result_get_handle(packet);
    uni_hid_device_t* d = uni_hid_device_get_instance_for_connection_handle(con_handle);
    if (!d)
        return;
    steamtriton_instance_t* ins = get_steamtriton_instance(d);

    switch (ins->state_ble)
    {
        case QUERYING_SERVICES:
        case FOUND_VALVE_SERVICE:
        {
            switch (event)
            {
                case GATT_EVENT_SERVICE_QUERY_RESULT:
                    gatt_event_service_query_result_get_service(packet, &service);
                    // Check if it's Valve's custom service
                    if (!service.uuid16 && memcmp(valve_service_uuid, service.uuid128, 16) == 0)
                    {
                        loge("Found the STEAM SERVICE!\r\n");
                        ins->state_ble = FOUND_VALVE_SERVICE;
                        ins->valve_service = service;
                    }
                    break;
                case GATT_EVENT_QUERY_COMPLETE:
                    if (gatt_event_query_complete_get_att_status(packet) != ATT_ERROR_SUCCESS) {
                        loge("Error during Valve gatt service discovery\r\n");
                        break;
                    }
                    if (ins->state_ble == FOUND_VALVE_SERVICE)
                    {
                        ins->state_ble = QUERYING_CHARACTERISTICS;
                        gatt_client_discover_characteristics_for_service(uni_steamtriton_handle_gatt_client_event,
                                                                         d->conn.handle,
                                                                         &ins->valve_service);
                    }
                    break;
                default:
                    loge("Steam Triton: Unknown event: %#x\n", event);
            }
            break;
        }
        case QUERYING_CHARACTERISTICS:
        case FOUND_CHARACTERISTICS:
        {
            switch (event)
            {
                case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT:
                    gatt_event_characteristic_query_result_get_characteristic(packet, &characteristic);
                    if (!characteristic.uuid16 && memcmp(valve_characteristic_rumble_uuid, characteristic.uuid128, 16) == 0)
                    {
                        loge("Found the STEAM CHARACTERISTIC RUMBLE!\r\n");
                        ins->state_ble = FOUND_CHARACTERISTICS;
                        ins->valve_haptic_rumble_characteristic = characteristic;
                    }
                    break;
                case GATT_EVENT_QUERY_COMPLETE:
                    if (gatt_event_query_complete_get_att_status(packet) != ATT_ERROR_SUCCESS) {
                        loge("Error during Valve gatt characteristics discovery\r\n");
                    }
                    break;
                default:
                    loge("Steam Triton: Unknown event: %#x\n", event);
            }
            break;
        }
    }
}

void uni_hid_parser_steamtriton_setup(struct uni_hid_device_s* d) {
    // Clear device instance
    steamtriton_instance_t* ins = get_steamtriton_instance(d);
    memset(ins, 0, sizeof(*ins));

    // Kick off Valve's service and characteristics discovery
    ins->state_ble = QUERYING_SERVICES;
    gatt_client_discover_primary_services(uni_steamtriton_handle_gatt_client_event, d->conn.handle);

    uni_controller_t* ctl = &d->controller;
    memset(ctl, 0, sizeof(*ctl));

    ctl->klass = UNI_CONTROLLER_CLASS_GAMEPAD;
    uni_hid_device_set_ready_complete(d);
}

void uni_hid_parser_steamtriton_init_report(uni_hid_device_t* d) {
    ARG_UNUSED(d);
}

void uni_hid_parser_steamtriton_parse_input_report(struct uni_hid_device_s* d, const uint8_t* report, uint16_t len) {
    // 0x45 is the controller state input report
    if (report[0] != 0x45 || len < sizeof(steamcontrollertriton_report_t))
    {
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

    // Normalize Steam axis values
    ctl->gamepad.axis_x = gp_report.joy_x / 64;
    ctl->gamepad.axis_y = (-1) * gp_report.joy_y / 64;
    ctl->gamepad.axis_rx = gp_report.joy_x2 / 64;
    ctl->gamepad.axis_ry = (-1) * gp_report.joy_y2 / 64;
    ctl->gamepad.throttle = gp_report.trigger_right / 32;
    ctl->gamepad.brake = gp_report.trigger_left / 32;
}

void uni_hid_parser_steamtriton_play_dual_rumble(struct uni_hid_device_s* d,
                                         uint16_t start_delay_ms,
                                         uint16_t duration_ms,
                                         uint8_t weak_magnitude,
                                         uint8_t strong_magnitude) {
    if (d == NULL) {
        loge("SteamTriton: Invalid device\n");
        return;
    }

    steamtriton_instance_t* ins = get_steamtriton_instance(d);
    switch (ins->rumble_state) {
        case STEAMTRITON_STATE_RUMBLE_DELAYED:
            btstack_run_loop_remove_timer(&ins->rumble_timer_delayed_start);
            break;
        case STEAMTRITON_STATE_RUMBLE_IN_PROGRESS:
            btstack_run_loop_remove_timer(&ins->rumble_timer_duration);
            break;
        default:
            // Do nothing
            break;
    }

    if (start_delay_ms == 0) {
        steamtriton_play_dual_rumble_now(d, duration_ms, weak_magnitude, strong_magnitude);
    } else {
        // Set timer to have a delayed start
        ins->rumble_timer_delayed_start.process = &on_steamtriton_set_rumble_on;
        ins->rumble_timer_delayed_start.context = d;
        ins->rumble_state = STEAMTRITON_STATE_RUMBLE_DELAYED;
        ins->rumble_duration_ms = duration_ms;
        ins->rumble_strong_magnitude = strong_magnitude;
        ins->rumble_weak_magnitude = weak_magnitude;

        btstack_run_loop_set_timer(&ins->rumble_timer_delayed_start, start_delay_ms);
        btstack_run_loop_add_timer(&ins->rumble_timer_delayed_start);
    }
}

//
// Helpers
//
static steamtriton_instance_t* get_steamtriton_instance(uni_hid_device_t* d) {
    return (steamtriton_instance_t*)&d->parser_data[0];
}

static void steamtriton_send_output_report(uni_hid_device_t* d, steamcontrollertriton_rumble_report_t* out) {
    steamtriton_instance_t* ins = get_steamtriton_instance(d);
    if (ins->state_ble == FOUND_CHARACTERISTICS)
        gatt_client_write_value_of_characteristic_without_response(
            d->conn.handle,
            ins->valve_haptic_rumble_characteristic.value_handle,
            sizeof(*out), (uint8_t*)out);
}

static void steamtriton_stop_rumble_now(uni_hid_device_t* d) {
    steamtriton_instance_t* ins = get_steamtriton_instance(d);
    // No need to protect it with a mutex since it runs in the same main thread
    assert(ins->rumble_state == STEAMTRITON_STATE_RUMBLE_IN_PROGRESS);
    ins->rumble_state = STEAMTRITON_STATE_RUMBLE_DISABLED;

    // Сache the previous rumble value
    ins->prev_rumble_weak_magnitude = 0;
    ins->prev_rumble_strong_magnitude = 0;

    steamcontrollertriton_rumble_report_t out = {0};
    steamtriton_send_output_report(d, &out);
}

static void steamtriton_play_dual_rumble_now(uni_hid_device_t* d,
                                     uint16_t duration_ms,
                                     uint8_t weak_magnitude,
                                     uint8_t strong_magnitude) {
    steamtriton_instance_t* ins = get_steamtriton_instance(d);

    if (duration_ms == 0) {
        if (ins->rumble_state != STEAMTRITON_STATE_RUMBLE_DISABLED)
            steamtriton_stop_rumble_now(d);
        return;
    }

    // Сache the previous rumble value
    ins->prev_rumble_weak_magnitude = weak_magnitude;
    ins->prev_rumble_strong_magnitude = strong_magnitude;

    steamcontrollertriton_rumble_report_t out = {0};
    out.type = 0;
    out.intensity = 200;
    out.speed_left = strong_magnitude << 3;
    out.gain_left = 0;
    out.speed_right = weak_magnitude << 3;
    out.gain_right = 0;

    steamtriton_send_output_report(d, &out);

    // Set timer to turn off rumble
    ins->rumble_timer_duration.process = &on_steamtriton_set_rumble_off;
    ins->rumble_timer_duration.context = d;
    ins->rumble_state = STEAMTRITON_STATE_RUMBLE_IN_PROGRESS;
    btstack_run_loop_set_timer(&ins->rumble_timer_duration, duration_ms);
    btstack_run_loop_add_timer(&ins->rumble_timer_duration);
}

static void on_steamtriton_set_rumble_on(btstack_timer_source_t* ts) {
    uni_hid_device_t* d = ts->context;
    steamtriton_instance_t* ins = get_steamtriton_instance(d);

    steamtriton_play_dual_rumble_now(d, ins->rumble_duration_ms, ins->rumble_weak_magnitude, ins->rumble_strong_magnitude);
}

static void on_steamtriton_set_rumble_off(btstack_timer_source_t* ts) {
    uni_hid_device_t* d = ts->context;
    steamtriton_stop_rumble_now(d);
}
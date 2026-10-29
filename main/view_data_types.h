#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Screen / navigation ──────────────────────────────────────────────────── */

enum start_screen {
    SCREEN_SENSECAP_LOG = 0,
    SCREEN_WIFI_CONFIG = 1,
    SCREEN_DISPLAY_MODAL = 2,
    SCREEN_BROKER_MODAL = 3,
};

/* ── WiFi ─────────────────────────────────────────────────────────────────── */

#define WIFI_SCAN_LIST_SIZE 15

/* SSIDs are up to 32 bytes; ssid[33] keeps room for the NUL terminator so a
 * full-length 32-char SSID from a wifi_ap_record_t / wifi_event_sta_connected_t
 * copies in without losing the terminator. These structs are event payloads;
 * producers/consumers use sizeof() rather than assuming a length. */
struct view_data_wifi_st {
    bool    is_connected;
    bool    is_connecting;
    bool    is_network;
    char    ssid[33];
    int8_t  rssi;
};

struct view_data_wifi_config {
    char    ssid[33];
    uint8_t password[64];
    bool    have_password;
};

struct view_data_wifi_item {
    char    ssid[33];
    bool    auth_mode;
    int8_t  rssi;
};

struct view_data_wifi_scan_request {
    uint32_t scan_generation;
};

struct view_data_wifi_list {
    uint32_t                  scan_generation;
    bool                      is_connect;
    struct view_data_wifi_item connect;
    uint16_t                  cnt;
    struct view_data_wifi_item aps[WIFI_SCAN_LIST_SIZE];
};

struct view_data_wifi_scan_failure {
    uint32_t scan_generation;
};

struct view_data_wifi_connet_ret_msg {
    uint8_t ret; /* 0 = success, 1 = failure */
    char    msg[64];
};

/* ── Display ──────────────────────────────────────────────────────────────── */

struct view_data_display {
    int  brightness;        /* 0–100 */
    bool sleep_mode_en;
    int  sleep_mode_time_min;
};

/* ── Time ─────────────────────────────────────────────────────────────────── */

struct view_data_time_cfg {
    bool   time_format_24;
    bool   auto_update;
    time_t time;
    bool   set_time;
    bool   auto_update_zone;
    int8_t zone;
    bool   daylight;
} __attribute__((packed));

/* ── Sensors ──────────────────────────────────────────────────────────────── */

struct sensor_data_average {
    float  data;
    time_t timestamp;
    bool   valid;
};

struct sensor_data_minmax {
    float  max;
    float  min;
    time_t timestamp;
    bool   valid;
};

#define SENSOR_TYPE_LIST                           \
    X(SCD41_SENSOR_CO2,      "SCD41_CO2")         \
    X(SGP40_SENSOR_TVOC,     "SGP40_TVOC")        \
    X(SHT41_SENSOR_TEMP,     "SHT41_TEMP")        \
    X(SHT41_SENSOR_HUMIDITY, "SHT41_HUMIDITY")

#define X(type, str) type,
enum sensor_data_type {
    SENSOR_TYPE_LIST ENUM_SENSOR_ALL
};
#undef X

struct view_data_sensor_data {
    enum sensor_data_type sensor_type;
    float                 value;
};

struct view_data_sensor_history_data {
    enum sensor_data_type     sensor_type;
    struct sensor_data_average data_day[24];
    struct sensor_data_minmax  data_week[7];
    uint8_t                   resolution;
    float                     day_min;
    float                     day_max;
    float                     week_min;
    float                     week_max;
};

/* ── Home Assistant ───────────────────────────────────────────────────────── */

struct view_data_ha_sensor_data {
    uint8_t index;
    char    value[32];
};

struct view_data_ha_switch_data {
    uint8_t index;
    int     value;
};

struct view_data_ha_switch_command {
    uint8_t  index;
    int      value;
    uint32_t generation;
};

struct view_data_ha_switch_publish_result {
    uint8_t  index;
    int      value;
    uint32_t generation;
    bool     accepted;
};

/* ── View-event interface contract ───────────────────────────────────────── */

/*
 * Event manifest for the view_event module interface.
 *
 * Each entry documents:
 *   P  — producer(s)
 *   C  — consumer(s)
 *   Payload — data type posted with the event (NULL = no data)
 *   Policy  — command = bounded delivery with explicit rejection;
 *             state = nonblocking latest-value delivery with coalescing
 *
 * Full producer/consumer and delivery-policy details live in
 * docs/stability/milestone-1-event-reliability.md.
 */
enum view_event_id {
    VIEW_EVENT_SCREEN_START = 0,        // uint8_t enum start_screen; command
    VIEW_EVENT_TIME,                    // bool; state
    VIEW_EVENT_WIFI_ST,                 // struct view_data_wifi_st; state
    VIEW_EVENT_CITY,                    // char[32]; state
    VIEW_EVENT_SENSOR_DATA,             // struct view_data_sensor_data; keyed state
    VIEW_EVENT_SENSOR_TEMP_HISTORY,     // NULL trigger; state
    VIEW_EVENT_SENSOR_HUMIDITY_HISTORY, // NULL trigger; state
    VIEW_EVENT_SENSOR_TVOC_HISTORY,     // NULL trigger; state
    VIEW_EVENT_SENSOR_CO2_HISTORY,      // NULL trigger; state
    VIEW_EVENT_SENSOR_DATA_HISTORY,     // struct view_data_sensor_history_data; state
    VIEW_EVENT_WIFI_LIST,               // struct view_data_wifi_list with scan generation; command result
    VIEW_EVENT_WIFI_LIST_FAILED,        // struct view_data_wifi_scan_failure; command result
    VIEW_EVENT_WIFI_LIST_START,         // NULL; command notification
    VIEW_EVENT_WIFI_LIST_REQ,           // struct view_data_wifi_scan_request; command
    VIEW_EVENT_WIFI_CONNECT,            // struct view_data_wifi_config; command
    VIEW_EVENT_WIFI_CONNECT_RET,        // struct view_data_wifi_connet_ret_msg; command result
    VIEW_EVENT_WIFI_CFG_DELETE,         // NULL; command
    VIEW_EVENT_TIME_CFG_UPDATE,         // struct view_data_time_cfg; command
    VIEW_EVENT_TIME_CFG_APPLY,          // struct view_data_time_cfg; command
    VIEW_EVENT_DISPLAY_CFG,             // struct view_data_display; state
    VIEW_EVENT_BRIGHTNESS_UPDATE,       // int32_t brightness (0-100); state
    VIEW_EVENT_DISPLAY_CFG_APPLY,       // struct view_data_display; command
    VIEW_EVENT_SHUTDOWN,                // NULL; command
    VIEW_EVENT_FACTORY_RESET,           // NULL; command
    VIEW_EVENT_SCREEN_CTRL,             // bool (false=off, true=on); state
    VIEW_EVENT_MQTT_ADDR_CHANGED,       // NULL; command
    VIEW_EVENT_HA_ADDR_DISPLAY,         // NULL refresh notification; state
    VIEW_EVENT_HA_SENSOR,               // struct view_data_ha_sensor_data; keyed state
    VIEW_EVENT_HA_SWITCH_ST,             // struct view_data_ha_switch_command; command
    VIEW_EVENT_HA_SWITCH_SET,            // struct view_data_ha_switch_data; keyed state
    VIEW_EVENT_HA_SWITCH_PUBLISH_RESULT, // struct view_data_ha_switch_publish_result; command result

    VIEW_EVENT_ALL,
};

#ifdef __cplusplus
}
#endif

# WiFi Domain

Vertical slice for all WiFi functionality. Mirrors the pattern established by `main/ha/`.

## File Responsibilities

| File | Owns | LVGL |
|---|---|---|
| `wifi_model.c` | ESP WiFi event handling, state machine, ping network check, 5-min reconnect task | ✗ |
| `wifi_list_screen.c` | AP list widget lifecycle, item creation, show/hide spinner | ✓ |
| `wifi_connect_screen.c` | Connect dialog and AP-details dialog (modal overlays) | ✓ |
| `wifi_view.c` | Event subscriptions, status icon updates, screen navigation, component coordination | ✓ |
| `wifi_scan_ui_state.c` | Generation-safe pending/discard/stale scan transitions | ✗ |
| `wifi.h` | Umbrella header (`wifi_model.h` + `wifi_view.h`) | — |

## Init Sequence

```
indicator_wifi_view_init()    ← called from indicator_view_init()
  → creates Wi-Fi status/list/connect widgets on nav/modal parents
  → registers VIEW_EVENT handlers for WIFI_ST / WIFI_LIST / SCREEN_START / CONNECT_RET / etc.

indicator_wifi_model_init()   ← called later from indicator_model_init()
  → ESP netif + WiFi stack init
  → starts _indicator_wifi_task (ping / reconnect loop)
  → registers VIEW_EVENT handlers for WIFI_LIST_REQ / WIFI_CONNECT / CFG_DELETE / SHUTDOWN
  → posts VIEW_EVENT_SCREEN_START if no saved SSID
```

## Event Flow

```
User taps AP button
  → _on_unconnected_tap() [wifi_view.c]
  → wifi_connect_screen_show(ssid, have_password) [wifi_connect_screen.c]

User taps Join
  → _on_join() [wifi_connect_screen.c]
  → VIEW_EVENT_WIFI_CONNECT posted
  → wifi_connect_screen auto-dismisses

VIEW_EVENT_WIFI_CONNECT
  → wifi_model: starts connection
  → wifi_view: shows spinner (wifi_list_screen_show_spinner)

WIFI_EVENT_STA_CONNECTED (ESP WiFi)
  → wifi_model: posts VIEW_EVENT_WIFI_ST + VIEW_EVENT_WIFI_CONNECT_RET

VIEW_EVENT_WIFI_CONNECT_RET
  → wifi_view: starts a generation-tagged VIEW_EVENT_WIFI_LIST_REQ, shows toast

VIEW_EVENT_WIFI_LIST (scan result)
  → wifi_view: applies only if its generation is current
```

## LVGL Thread Safety

All LVGL widget mutations in `wifi_view.c` event handlers are wrapped with
`lv_port_sem_take()` / `lv_port_sem_give()`.

Screen component functions (`wifi_list_screen_*`, `wifi_connect_screen_*`)
are always called from within an already-held semaphore in `wifi_view.c`.

## LVGL Containment Rule

Only view/screen files in this domain may own LVGL widgets. `wifi_model.c`
must remain UI-free. Screen component files use `LV_IMAGE_DECLARE` /
`LV_FONT_DECLARE` for the specific assets they need and accept parent widget
pointers as parameters.

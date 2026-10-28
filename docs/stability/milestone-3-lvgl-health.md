# Milestone 3: LVGL health diagnostics

Milestone 3 adds passive diagnostics for a stalled LVGL task without changing
rendering, touch, sleep, or recovery behavior. It does not add an automatic
restart policy.

## Interface and seam

`main/lvgl_health/lvgl_health_state.[ch]` is the host-testable policy module. It
accepts only monotonic time and a heartbeat sequence, and emits edge-triggered
actions:

- `STALLED` once after 3 seconds without progress;
- `ESCALATED` once after 30 seconds without progress;
- `RECOVERED` once when progress resumes.

Repeated checks in the same state emit no action, which prevents log flooding.

`main/lv_port.c` remains the LVGL adapter. It creates a 500 ms LVGL timer, so
the heartbeat callback runs on the LVGL task itself. The callback passes control
to `lvgl_health_heartbeat()`, which captures `xTaskGetCurrentTaskHandle()` on its
first execution. The implementation never looks up a hard-coded task name.

The monitor task runs every second at lower priority than LVGL. On a transition
it records the real task's FreeRTOS state and stack low-water mark. A stack
low-water mark below 1024 bytes is reported once.

## Failure policy

- Diagnostics never call `esp_restart()` and never suspend, delete, or mutate
  the LVGL task.
- Failure to create the heartbeat timer or monitor task is logged and normal UI
  initialization continues.
- A task wake failure is logged; the existing 500 ms maximum LVGL sleep still
  allows the timer to be discovered.
- A 3 second gap is a warning. A 30 second gap is an escalation for manual
  diagnosis, not permission to reboot.

## Verification

`scripts/test_lvgl_health.py` compiles the pure policy on the host and proves
that warning, escalation, and recovery are edge-triggered. It also checks that
the runtime captures the current task handle, reads task state and stack
low-water information, contains no task-name lookup, and contains no restart
path.

The test runs through `scripts/dev_check.py --skip-build`. Firmware verification
uses the fixed ESP-IDF v5.5.6, LVGL 9.6.0~1, and esp_lvgl_port 2.9.0 dependency
set.

## Hardware boundary

Startup monitoring must show:

- `lvgl-health` captures an actual non-null LVGL task handle;
- normal UI activity produces no false stall warning;
- LCD, touch, Wi-Fi, and sensor updates remain functional;
- no panic, watchdog, assertion, or abnormal reset occurs.

An intentionally injected production freeze is out of scope. The pure state
test covers timeout transitions without adding a fault path to firmware.

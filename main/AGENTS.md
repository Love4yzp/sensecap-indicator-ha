# AGENTS.md — SenseCAP Indicator HA

Agent 开发指南。在修改代码之前请先读完本文件。

---

## 项目概述

ESP32-S3 + RP2040 固件，LVGL 9.6.0~1（ESP Component Manager 管理），ESP-IDF v5.5.6，FreeRTOS。
显示屏：480×480 RGB565，触摸输入。Home Assistant 通过 MQTT 集成。

---

## 文件布局

```
main/
  ha/           HA 域：MQTT 订阅/发布、switch/sensor/arc 控件
  wifi/         WiFi 域：扫描、连接、状态显示
  sensor/       Sensor 域：SHT41/SGP40/SCD41 数据展示
  display/      Display 域：亮度、睡眠模式
  rp2040/       RP2040 UART/COBS 通信
  btn/          按钮 GPIO
  mqtt/         MQTT 总线（共享实例）
  storage/      NVS 读写
  cmd/          UART 控制台命令
  nav/          lv_tileview 导航（主屏滑动）
  assets/       图像和字体资源（LVGL 9 格式）
  util/         公共工具函数

  main.c              入口
  indicator_model.c   全局模型初始化（各域 model_init 汇总）
  indicator_view.c    全局视图初始化（各域 view_init 汇总）
  indicator_enabler.h 功能开关（#include 各域头文件）
  lv_port.c           ⚠️ BSP 硬件边界，见禁区说明
  view_data_types.h   事件 ID + payload 数据结构体（权威定义）
  view_event/         跨域事件 deep module；公开 seam 为 view_event.h
  home_assistant_config.h  HA MQTT topic 配置
  idf_component.yml   ESP Component Manager 依赖声明
```

域的文件命名因域而异，没有统一的 `<domain>_view.c` 规律。实际示例：
```
main/ha/        ha.h, ha_mqtt.c, ha_sensor.c, ha_switch_screen.c, ha_config.c
main/wifi/      wifi.h, wifi_model.c, wifi_view.c, wifi_list_screen.c, wifi_connect_screen.c
main/sensor/    sensor_model.h, sensor_model.c, sensor_view.h, sensor_view.c
main/display/   display.h, display_model.c, display_view.h, display_view.c
main/nav/       nav.h, nav.c
main/assets/    ui_img_*.c, ui_font_*.c（图像/字体资源）
```
共同规律：`*_view.c` 只做 LVGL 控件操作；model/业务逻辑在其他 `.c` 文件中。

---

## 导航系统

主屏使用 `lv_tileview`，横向滑动，当前 3 个 tile：

```c
// main/nav/nav.h
#define NAV_TILE_HA_DATA  0   // 传感器数据页
#define NAV_TILE_HA_CTRL  1   // HA 开关控制页
#define NAV_TILE_HA_MIX   2   // HA 混合控件页（弧形/滑块）
#define NAV_TILE_COUNT    3
```

弹窗/设置使用 `lv_layer_top()` 作为父容器（悬浮在 tileview 之上）。

---

## 新建页面 — 四步套路

### 类型 A：全屏 Tile（可左右滑动的主页面）

**Step 1** — `main/nav/nav.h` 增加常量（两处都要改）：
```c
#define NAV_TILE_MY_PAGE  3
#define NAV_TILE_COUNT    4   // ← 必须同步 +1，否则 tile 显示空白
```

**Step 2** — `main/CMakeLists.txt` 的 `DIRECTORIES_TO_INCLUDE` 列表加入新目录：
```cmake
set(DIRECTORIES_TO_INCLUDE "util" "ha" "wifi" ... "my_page")
#                                                    ^^^^^^^^ 新增
```
⚠️ 不加此行，新目录下的 `.c` 文件会被静默忽略，build 通过但代码不执行。

**Step 3** — 新建域目录和两个文件：
```
main/my_page/
  my_page_view.h
  my_page_view.c
```

**Step 3** — `my_page_view.c` 最小模板：
```c
#include <string.h>
#include "lvgl.h"
#include "nav.h"
#include "lv_port.h"
#include "view_data_types.h"
#include "view_event.h"
#include "esp_log.h"

static const char *TAG = "my_page_view";

void my_page_view_init(void) {
    lv_port_sem_take();
    lv_obj_t *tile = nav_get_tile(NAV_TILE_MY_PAGE);
    if (!tile) { lv_port_sem_give(); return; }

    lv_obj_t *label = lv_label_create(tile);
    lv_label_set_text(label, "My Page");
    lv_obj_center(label);
    lv_port_sem_give();

    // 注册事件监听（如需响应域间事件）
    // view_event_subscribe(VIEW_EVENT_XXX, cb, NULL);
}
```

`my_page_view.h` 只需：
```c
#pragma once
void my_page_view_init(void);
```

**Step 5** — `main/indicator_view.c` 末尾调用：
```c
#include "my_page/my_page_view.h"
// ...
my_page_view_init();
```

### 类型 B：Modal 弹窗（设置/对话框）

不占 tile，浮在最上层：
```c
lv_obj_t *overlay = lv_obj_create(lv_layer_top());
lv_obj_set_size(overlay, 480, 480);
lv_obj_set_style_bg_color(overlay, lv_color_hex(0x1A1A1A), 0);
lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);   // 初始隐藏

// 显示：lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
// 隐藏：lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
```

参考实现：`main/wifi/wifi_view.c`、`main/display/display_view.c`、`main/ha/ha_config.c`。

---

## 域间通信规则

**唯一合法的 `VIEW_EVENT_*` 跨域通信 seam**：`view_event.h`。

```c
// 发送可合并状态
struct view_data_sensor_data data = { .sensor_type = SHT41_SENSOR_TEMP, .value = 25.3f };
view_event_delivery_t result =
    view_event_publish_state(VIEW_EVENT_SENSOR_DATA, &data, sizeof(data));

// 接收事件（在 view_init/model_init 中注册）
view_event_subscribe(VIEW_EVENT_SENSOR_DATA, my_handler, NULL);
```

所有事件 ID 和 payload 数据结构以 `main/view_data_types.h` 为权威位置。
调用方不得 include 私有 adapter，也不得自行选择 FreeRTOS queue、ESP
event loop 或 timeout。启动顺序在 LVGL 初始化后调用 `view_event_init()`。

---

## LVGL 使用规则

| 规则 | 说明 |
|------|------|
| 访问 LVGL 必须持锁 | `lv_port_sem_take()` / `lv_port_sem_give()` |
| 字体声明 | `LV_FONT_DECLARE(ui_font_font0)` |
| 图像声明 | `LV_IMAGE_DECLARE(ui_img_ic_temp_png)` |
| 发送 LVGL 事件 | `lv_obj_send_event(obj, LV_EVENT_CLICKED, NULL)` |
| include 路径 | `#include "lvgl.h"`（不是 `lvgl/lvgl.h`） |

### assets/ 图像格式（LVGL 9）

数据为 RGB565 色彩平面 + A8 alpha 平面，合计 3 字节/像素。LVGL 9 描述符格式：
```c
const lv_image_dsc_t ui_img_xxx = {
    .header.magic  = LV_IMAGE_HEADER_MAGIC,
    .header.cf     = LV_COLOR_FORMAT_RGB565A8,
    .header.flags  = 0,
    .header.w      = W,
    .header.h      = H,
    .header.stride = W * 2,        // RGB565 plane stride; alpha plane follows data
    .data_size     = sizeof(ui_img_xxx_data),
    .data          = ui_img_xxx_data,
};
```

---

## ⚠️ 禁区

| 文件/目录 | 原因 |
|-----------|------|
| `main/lv_port.c` | BSP 硬件边界，改动影响整个显示系统，非显示硬件任务不动 |
| `components/bsp/` | 硬件驱动层 |
| `managed_components/` | 由 ESP Component Manager 自动管理，不手动编辑 |

---

## 验证清单（每次改动后按顺序执行）

```bash
# 1. 架构扫描（检查域边界违规）
python3 scripts/architecture_scan.py

# 2. 构建（必须 0 error）
./dev build

# 3. HA/MQTT 协议改动时额外运行
python3 scripts/test_ha_switch_protocol.py
```

---

## 常见编译错误速查

| 错误信息 | 原因 | 修复 |
|----------|------|------|
| `lvgl/lvgl.h: No such file` | 旧 LVGL 8 路径 | 改为 `#include "lvgl.h"` |
| `lv_event_send` 类型不匹配 | LVGL 8 API | 改为 `lv_obj_send_event(obj, event, NULL)` |
| `always_zero` 无此成员 | 旧图像描述符格式 | 用上方 assets 格式重写 |
| `LV_IMG_CF_TRUE_COLOR_ALPHA` 未声明 | LVGL 8 常量 | 改为 `LV_COLOR_FORMAT_RGB565A8` 并设置 `header.stride` |
| tile 页面空白 | `NAV_TILE_COUNT` 未更新 | nav.h 中 COUNT 同步 +1 |
| LVGL assert / crash | 未持锁访问 LVGL | 加 `lv_port_sem_take/give` |

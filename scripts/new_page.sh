#!/usr/bin/env bash
# zsh/dash compat: lowercase/uppercase via tr/awk
# new_page.sh — scaffold a new LVGL page (tile or modal)
#
# Usage:
#   ./scripts/new_page.sh <name> [tile|modal]
#
# Examples:
#   ./scripts/new_page.sh settings        # defaults to tile
#   ./scripts/new_page.sh settings tile
#   ./scripts/new_page.sh broker modal
#
# For a tile page, the script also prints the two lines you need to add
# manually (nav.h constant + indicator_view.c call).

set -euo pipefail

NAME="${1:-}"
TYPE="${2:-tile}"

if [[ -z "$NAME" ]]; then
    echo "Usage: $0 <page_name> [tile|modal]"
    echo "  tile  — full-screen tileview page (default)"
    echo "  modal — floating overlay (settings dialog)"
    exit 1
fi

if [[ "$TYPE" != "tile" && "$TYPE" != "modal" ]]; then
    echo "Error: type must be 'tile' or 'modal'"
    exit 1
fi

# Derive names
SNAKE="$(echo "$NAME" | tr '[:upper:]' '[:lower:]')"
UPPER="$(echo "$NAME" | tr '[:lower:]' '[:upper:]')"
CAMEL="$(echo "$SNAKE" | awk -F'_' '{for(i=1;i<=NF;i++) $i=toupper(substr($i,1,1)) substr($i,2); OFS=""; print}')"

DIR="main/${SNAKE}"
H="${DIR}/${SNAKE}_view.h"
C="${DIR}/${SNAKE}_view.c"

if [[ -d "$DIR" ]]; then
    echo "Error: directory $DIR already exists"
    exit 1
fi

mkdir -p "$DIR"

# ── Header ──────────────────────────────────────────────────────────────────
cat > "$H" <<EOF
#pragma once

void ${SNAKE}_view_init(void);
EOF

# ── Source — tile ────────────────────────────────────────────────────────────
if [[ "$TYPE" == "tile" ]]; then
cat > "$C" <<EOF
#include <string.h>
#include "lvgl.h"
#include "nav.h"
#include "lv_port.h"
#include "view_data_types.h"
#include "view_event.h"
#include "esp_log.h"

static const char *TAG = "${SNAKE}_view";

void ${SNAKE}_view_init(void) {
    lv_port_sem_take();
    lv_obj_t *tile = nav_get_tile(NAV_TILE_${UPPER});
    if (!tile) {
        lv_port_sem_give();
        ESP_LOGE(TAG, "tile NAV_TILE_${UPPER} not found — check NAV_TILE_COUNT in nav.h");
        return;
    }

    /* ── build your UI here ─────────────────────────────────────────────── */
    lv_obj_t *label = lv_label_create(tile);
    lv_label_set_text(label, "${CAMEL}");
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    /* ───────────────────────────────────────────────────────────────────── */

    lv_port_sem_give();

    /* register event listeners here if needed:
    view_event_subscribe(VIEW_EVENT_XXX, _on_xxx, NULL);
    */
    ESP_LOGI(TAG, "init done");
}
EOF

# ── Source — modal ───────────────────────────────────────────────────────────
else
cat > "$C" <<EOF
#include <string.h>
#include "lvgl.h"
#include "lv_port.h"
#include "view_data_types.h"
#include "esp_log.h"

static const char *TAG = "${SNAKE}_view";
static lv_obj_t *s_overlay = NULL;

static void _on_close(lv_event_t *e) {
    if (s_overlay) lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

void ${SNAKE}_view_show(void) {
    if (!s_overlay) return;
    lv_port_sem_take();
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_port_sem_give();
}

void ${SNAKE}_view_hide(void) {
    if (!s_overlay) return;
    lv_port_sem_take();
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_port_sem_give();
}

void ${SNAKE}_view_init(void) {
    lv_port_sem_take();

    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_overlay, 480, 480);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_overlay, 16, LV_PART_MAIN);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    /* ── close button ───────────────────────────────────────────────────── */
    lv_obj_t *close_btn = lv_button_create(s_overlay);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_add_event_cb(close_btn, _on_close, LV_EVENT_CLICKED, NULL);

    /* ── build your modal content here ─────────────────────────────────── */
    lv_obj_t *title = lv_label_create(s_overlay);
    lv_label_set_text(title, "${CAMEL}");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    /* ───────────────────────────────────────────────────────────────────── */

    lv_port_sem_give();
    ESP_LOGI(TAG, "init done");
}
EOF

# update header for modal to include show/hide
cat > "$H" <<EOF
#pragma once

void ${SNAKE}_view_init(void);
void ${SNAKE}_view_show(void);
void ${SNAKE}_view_hide(void);
EOF
fi

# ── Print next steps ─────────────────────────────────────────────────────────
echo ""
echo "✓ Created: $H"
echo "✓ Created: $C"
echo ""

if [[ "$TYPE" == "tile" ]]; then
    # Find current NAV_TILE_COUNT
    COUNT=$(grep 'NAV_TILE_COUNT' main/nav/nav.h 2>/dev/null | grep -oE '[0-9]+' | tail -1)
    COUNT="${COUNT:-?}"
    if [[ "$COUNT" =~ ^[0-9]+$ ]]; then
        NEW_IDX=$COUNT
        NEXT=$((COUNT + 1))
    else
        NEW_IDX="?"
        NEXT="?"
    fi

    echo "━━━ Manual steps required ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "1. main/CMakeLists.txt — add \"${SNAKE}\" to DIRECTORIES_TO_INCLUDE:"
    echo "   set(DIRECTORIES_TO_INCLUDE \"util\" \"ha\" ... \"${SNAKE}\")"
    echo "   ⚠️  Without this, the .c files are silently excluded from the build."
    echo ""
    echo "2. main/nav/nav.h — add BEFORE the COUNT line:"
    echo "   #define NAV_TILE_${UPPER}  ${NEW_IDX}"
    echo "   #define NAV_TILE_COUNT    ${NEXT}"
    echo ""
    echo "3. main/indicator_view.c — add:"
    echo "   #include \"${SNAKE}/${SNAKE}_view.h\""
    echo "   ${SNAKE}_view_init();"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
else
    echo "━━━ Manual steps required ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "1. main/CMakeLists.txt — add \"${SNAKE}\" to DIRECTORIES_TO_INCLUDE:"
    echo "   set(DIRECTORIES_TO_INCLUDE \"util\" \"ha\" ... \"${SNAKE}\")"
    echo "   ⚠️  Without this, the .c files are silently excluded from the build."
    echo ""
    echo "2. main/indicator_view.c — add:"
    echo "   #include \"${SNAKE}/${SNAKE}_view.h\""
    echo "   ${SNAKE}_view_init();"
    echo ""
    echo "3. To show the modal from another domain, post an event or call:"
    echo "   ${SNAKE}_view_show();"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
fi

echo ""
echo "Then verify: python3 scripts/architecture_scan.py && idf.py build"

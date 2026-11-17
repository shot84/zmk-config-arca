// Arca rev.2 の OLED — Jump レイヤ（iPad の Jump Desktop 越しに MBP を使う間）の有無を L→R へ渡す / 2026-11-09
//   R は周辺側でレイヤの状態を知らない＝L（central）が変わるたびに relay で送る（R の円の中を J に）
#pragma once
#include <stdbool.h>
#include <zmk/event_manager.h>

struct arca_oled_jump_event {
    uint8_t on;
};

ZMK_EVENT_DECLARE(arca_oled_jump_event);

bool arca_oled_jump_on(void);

// Arca rev.2 の OLED — 縦 32×128 の 1 コマを描く（LVGL/ZMK に依存しない素の C）/ 2026-11-02
// ★手元の cc でもコンパイルされる（chat-files/ark_oled_assets.py --check が図の生成器と 1 画素ずつ照合する）
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define OLED_PW 32                         // 縦置きの幅（西→東）
#define OLED_PH 128                        // 縦置きの高さ（北→南）
#define OLED_STRIDE (OLED_PW / 8)
#define OLED_BUF (OLED_STRIDE * OLED_PH)   // 512

enum oled_mid { OLED_MID_CHAR = 0, OLED_MID_USB, OLED_MID_MARK };

struct oled_state {
    uint8_t pct;        // 電池 0..100（円の実線の長さ）
    uint8_t mid;        // 円の中: 字（BT の番号・U）／USB 給電の印／舟のマーク
    char mid_char;      // OLED_MID_CHAR のときの字
    uint8_t dim;        // 円の中を網点に（L＝BT が切れている・R＝L と切れている）
    int8_t mods;        // 修飾キー bit0..3＝control/option/shift/command（-1＝段を出さない＝R）
    char word[12];      // 縦に積む字（大文字に直して描く・Arc に無い字は飛ばす）
};

// buf: 行ごと・MSB が左（x=0＝西）・stride 4・1＝光る
void oled_draw(uint8_t *buf, const struct oled_state *s);

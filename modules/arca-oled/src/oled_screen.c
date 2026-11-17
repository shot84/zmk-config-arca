// Arca rev.2 の OLED — ZMK の画面差し替え口・状態の読み取り・縦→横の写し / 2026-11-02
//
// ■ 流れ
//   表示スレッドの lv_timer で ARCA_OLED_POLL_MS ごとに状態を読み → 前と違えば
//   oled_draw()（縦 32×128）→ 横 128×32 のキャンバスへ回して写す → lv_obj_invalidate を 1 回。
//   変わらなければ何もしない（I2C に流さない）。idle で ZMK が画面を消すと表示の tick ごと止まる。
//
// ■ rev3（LCD）から持ってきた作法（パネルに関係しない物だけ）
//   ・カスタム画面の差し替え口＝weak な zmk_display_status_screen() を上書き（screen は lv_obj_create(NULL) だけ）
//   ・LVGL の描画 API を 1 画素ずつ叩かない＝キャンバスのバッファへバイトで書いて invalidate 1 回（arca-fw §8.1）
//   ・修飾キーは押した瞬間はまだ未登録＝表示スレッドで読み直す（§8.3）
//   ・充電は VBUS のビット直読み（arca_charge.h・R でも読める）
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/battery.h>
#include <zmk/keymap.h>

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define ARCA_OLED_CENTRAL 1
#include <zmk/hid.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#else
#define ARCA_OLED_CENTRAL 0
#include <zmk/split/bluetooth/peripheral.h>
#endif

#if defined(CONFIG_ARCA_CHARGE_FULL_PCT)
#include <arca_charge.h>
#endif

#include "oled_draw.h"
#include "oled_jump.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define LW 128   // パネルの横（ZMK/LVGL から見た幅）
#define LH 32
#define LSTRIDE (LW / 8)
#define PALETTE 8   // I1 のキャンバスは先頭にパレット 8 バイト

static uint8_t cv_buf[PALETTE + LSTRIDE * LH + LV_DRAW_BUF_ALIGN] __aligned(4);
static uint8_t *cv_px;
static lv_obj_t *cv;
static uint8_t pbuf[OLED_BUF];
static struct oled_state shown;
static bool shown_valid;

// 縦（px: 西→東 0..31, py: 北→南 0..127）→ 横（lx 0..127, ly 0..31）
//   ★既定の向き: 組み込み画面が「東を下にすると読める」（ユーザー 11-02）
//     ＝横の y の下（31）が東・横の x の右（127）が北 → lx = 127 - py・ly = px
static void rotate_into_canvas(void) {
    memset(cv_px, 0, LSTRIDE * LH);
    for (int py = 0; py < OLED_PH; py++) {
        for (int px = 0; px < OLED_PW; px++) {
            if (!(pbuf[py * OLED_STRIDE + (px >> 3)] & (0x80u >> (px & 7)))) continue;
#if IS_ENABLED(CONFIG_ARCA_OLED_ROT180)
            int lx = py, ly = (OLED_PW - 1) - px;
#else
            int lx = (OLED_PH - 1) - py, ly = px;
#endif
            cv_px[ly * LSTRIDE + (lx >> 3)] |= (uint8_t)(0x80u >> (lx & 7));
        }
    }
}

static void read_state(struct oled_state *s) {
    memset(s, 0, sizeof(*s));
    s->pct = zmk_battery_state_of_charge();
    // USB 給電中（満タンも含む）は円の中を USB の印に（11-04 ユーザー「USB給電時は充電中の雷マークじゃなくてUSBのアイコンに」・
    // 充電中かどうかは XIAO の作り上ちゃんと読めない＝給電の有無だけ見る）
    bool vbus = false;
#if defined(CONFIG_ARCA_CHARGE_FULL_PCT)
    vbus = arca_charge_get() != ARCA_CHARGE_NONE;
#endif
#if ARCA_OLED_CENTRAL
    // 円の中: USB に出しているなら U・BLE なら番号（その番号の相手と切れている間は網点）
    struct zmk_endpoint_instance ep = zmk_endpoint_get_selected();
    s->mid = OLED_MID_CHAR;
    if (ep.transport == ZMK_TRANSPORT_USB) {
        s->mid_char = 'U';
    } else {
        int i = zmk_ble_active_profile_index();
        s->mid_char = (char)('1' + (i < 0 ? 0 : i % 9));
        s->dim = !zmk_ble_active_profile_is_connected();
    }
    if (vbus) { s->mid = OLED_MID_USB; s->dim = 0; }
    zmk_keymap_layer_index_t li = zmk_keymap_highest_layer_active();
    const char *nm = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(li));
    if (nm) strncpy(s->word, nm, sizeof(s->word) - 1);
    zmk_mod_flags_t m = zmk_hid_get_explicit_mods();
    s->mods = (int8_t)(((m & (MOD_LCTL | MOD_RCTL)) ? 1 : 0) | ((m & (MOD_LALT | MOD_RALT)) ? 2 : 0) |
                       ((m & (MOD_LSFT | MOD_RSFT)) ? 4 : 0) | ((m & (MOD_LGUI | MOD_RGUI)) ? 8 : 0));
#else
    // R: 円の中＝舟のマーク（L と切れている間は網点）・字＝ARCA・修飾キーの段は出さない
    bool linked = zmk_split_bt_peripheral_is_connected();
    s->mid = vbus ? OLED_MID_USB : OLED_MID_MARK;
    s->dim = !vbus && !linked;
    // Jump レイヤ中（L から知らせが来る）は舟のマークの代わりに J（11-09・USB 給電の印より先）
    if (linked && arca_oled_jump_on()) { s->mid = OLED_MID_CHAR; s->mid_char = 'J'; s->dim = 0; }
    strcpy(s->word, "ARCA");
    s->mods = -1;
#endif
}

static void refresh(bool force) {
    if (!cv_px) return;
    struct oled_state s;
    read_state(&s);
    if (!force && shown_valid && memcmp(&s, &shown, sizeof(s)) == 0) return;
    shown = s;
    shown_valid = true;
    oled_draw(pbuf, &s);
    rotate_into_canvas();
    lv_obj_invalidate(cv);   // ★1 コマにこの 1 回だけ
}

static void poll_cb(lv_timer_t *t) { refresh(false); }

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    cv = lv_canvas_create(screen);
    lv_canvas_set_buffer(cv, cv_buf, LW, LH, LV_COLOR_FORMAT_I1);
#if IS_ENABLED(CONFIG_ARCA_OLED_INK_BLACK)
    lv_canvas_set_palette(cv, 0, lv_color32_make(0xff, 0xff, 0xff, 0xff));   // 0＝地
    lv_canvas_set_palette(cv, 1, lv_color32_make(0x00, 0x00, 0x00, 0xff));   // 1＝光る
#else
    lv_canvas_set_palette(cv, 0, lv_color32_make(0x00, 0x00, 0x00, 0xff));
    lv_canvas_set_palette(cv, 1, lv_color32_make(0xff, 0xff, 0xff, 0xff));
#endif
    lv_obj_set_pos(cv, 0, 0);
    lv_draw_buf_t *db = lv_canvas_get_draw_buf(cv);
    // ★LVGL の stride がこちらの想定（16）より広いなら書かない（rev3 の arca_cv と同じ守り）
    if (db->header.stride != LSTRIDE) {
        LOG_ERR("arca_oled: stride %u != %d。描画を止める", db->header.stride, LSTRIDE);
        return screen;
    }
    cv_px = (uint8_t *)db->data + PALETTE;
    refresh(true);
    lv_timer_create(poll_cb, CONFIG_ARCA_OLED_POLL_MS, NULL);
    return screen;
}

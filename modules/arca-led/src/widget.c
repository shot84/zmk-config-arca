/*
 * Arca 状態LED — 基板の SK6812MINI-E（蓋の導光棒の真下）に
 *   電池残量 / BLEプロファイル / split接続 / レイヤ を色で出す。
 *
 * ★出自: caksoylar/zmk-rgbled-widget (MIT, Copyright (c) 2023 Cem Aksoylar) の
 *   判定ロジックをそのまま使い、**出力先だけ差し替えた**もの。
 *   本家は「3本の独立GPIO LED」専用（アドレサブル非対応）で、Arcaの基板に載っている
 *   SK6812MINI-E を光らせられない。触ったのは set_rgb_leds() と device 取得の2箇所だけ。
 *
 * ★なぜ要るか: rgbled_adapter は **XIAO基板上の内蔵LED**を光らせる設定で、
 *   ケースに入れると外から見えない。基板とケース（導光棒Ø3・4.85mm）は
 *   最初から SK6812 の位置に合わせて作ってある。
 *
 * SPDX-License-Identifier: MIT
 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/split/bluetooth/peripheral.h>

#if __has_include(<zmk/split/central.h>)
#include <zmk/split/central.h>
#else
#include <zmk/split/bluetooth/central.h>
#endif

#include <zephyr/logging/log.h>

#include <arca_led/widget.h>
#include <arca_charge.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// ★2026-10-28 v5（Arca rev.2）: 出力先を2通りに。
//   ・rev3 … 基板の SK6812（アドレサブル1個）＝エイリアス **arca-led**（overlay で &led_strip に張る）
//   ・v5   … **XIAO内蔵のRGB（GPIO 3本・負論理）**＝エイリアス **led-red / led-green / led-blue**
//            （台帳 E4「内蔵 RGB LED を使う・外付けの SK6812 はなし」）
//   arca-led があればそちら。判定ロジック（電池/BLE/split/レイヤ/充電）は共通。
#define ARCA_LED_STRIP DT_NODE_EXISTS(DT_ALIAS(arca_led))
BUILD_ASSERT(ARCA_LED_STRIP || (DT_NODE_EXISTS(DT_ALIAS(led_red)) &&
                                DT_NODE_EXISTS(DT_ALIAS(led_green)) &&
                                DT_NODE_EXISTS(DT_ALIAS(led_blue))),
             "エイリアス arca-led（SK6812）か led-red/green/blue（XIAO内蔵）のどちらかが要る");

// ★★SHOW_LAYER_* は peripheral では 0 に畳まれる（widget.h の ARCA_LED_HAS_KEYMAP）ので、
//   そちらで判定すると右では**何を書いても素通り**する飾りになる。
//   設定の誤りは左右どちらでも捕まえたいので、ここは生の CONFIG を見る。
BUILD_ASSERT(!(IS_ENABLED(CONFIG_ARCA_LED_WIDGET_SHOW_LAYER_CHANGE) &&
               IS_ENABLED(CONFIG_ARCA_LED_WIDGET_SHOW_LAYER_COLORS)),
             "CONFIG_ARCA_LED_WIDGET_SHOW_LAYER_CHANGE and CONFIG_ARCA_LED_WIDGET_SHOW_LAYER_COLORS "
             "are mutually exclusive");

#if ARCA_LED_STRIP
// アドレサブルLED（SK6812MINI-E ×1）。本家の led-red/green/blue エイリアスは要らない
// ＝色は1画素のRGB値で直接出す。
static const struct device *led_dev = DEVICE_DT_GET(DT_ALIAS(arca_led));
#else
// XIAO内蔵LED（gpio-leds の子3つ）。本家 rgbled-widget と同じ取り方
static const struct device *led_dev = DEVICE_DT_GET(DT_PARENT(DT_ALIAS(led_red)));
static const uint8_t rgb_idx[] = {DT_NODE_CHILD_IDX(DT_ALIAS(led_red)),
                                  DT_NODE_CHILD_IDX(DT_ALIAS(led_green)),
                                  DT_NODE_CHILD_IDX(DT_ALIAS(led_blue))};
#endif

// map from color values to names, for logging
static const char *color_names[] = {"black", "red",     "green", "yellow",
                                    "blue",  "magenta", "cyan",  "white"};

#if SHOW_LAYER_COLORS
static const uint8_t layer_color_idx[] = {
    CONFIG_ARCA_LED_WIDGET_LAYER_0_COLOR,  CONFIG_ARCA_LED_WIDGET_LAYER_1_COLOR,
    CONFIG_ARCA_LED_WIDGET_LAYER_2_COLOR,  CONFIG_ARCA_LED_WIDGET_LAYER_3_COLOR,
    CONFIG_ARCA_LED_WIDGET_LAYER_4_COLOR,  CONFIG_ARCA_LED_WIDGET_LAYER_5_COLOR,
    CONFIG_ARCA_LED_WIDGET_LAYER_6_COLOR,  CONFIG_ARCA_LED_WIDGET_LAYER_7_COLOR,
    CONFIG_ARCA_LED_WIDGET_LAYER_8_COLOR,  CONFIG_ARCA_LED_WIDGET_LAYER_9_COLOR,
    CONFIG_ARCA_LED_WIDGET_LAYER_10_COLOR, CONFIG_ARCA_LED_WIDGET_LAYER_11_COLOR,
    CONFIG_ARCA_LED_WIDGET_LAYER_12_COLOR, CONFIG_ARCA_LED_WIDGET_LAYER_13_COLOR,
    CONFIG_ARCA_LED_WIDGET_LAYER_14_COLOR, CONFIG_ARCA_LED_WIDGET_LAYER_15_COLOR,
    CONFIG_ARCA_LED_WIDGET_LAYER_16_COLOR, CONFIG_ARCA_LED_WIDGET_LAYER_17_COLOR,
    CONFIG_ARCA_LED_WIDGET_LAYER_18_COLOR, CONFIG_ARCA_LED_WIDGET_LAYER_19_COLOR,
    CONFIG_ARCA_LED_WIDGET_LAYER_20_COLOR, CONFIG_ARCA_LED_WIDGET_LAYER_21_COLOR,
    CONFIG_ARCA_LED_WIDGET_LAYER_22_COLOR, CONFIG_ARCA_LED_WIDGET_LAYER_23_COLOR,
    CONFIG_ARCA_LED_WIDGET_LAYER_24_COLOR, CONFIG_ARCA_LED_WIDGET_LAYER_25_COLOR,
    CONFIG_ARCA_LED_WIDGET_LAYER_26_COLOR, CONFIG_ARCA_LED_WIDGET_LAYER_27_COLOR,
    CONFIG_ARCA_LED_WIDGET_LAYER_28_COLOR, CONFIG_ARCA_LED_WIDGET_LAYER_29_COLOR,
    CONFIG_ARCA_LED_WIDGET_LAYER_30_COLOR, CONFIG_ARCA_LED_WIDGET_LAYER_31_COLOR,
};
#endif

// log shorthands
#define LOG_CONN_CENTRAL(index, status, color_label)                                               \
    LOG_INF("Profile %d %s, blinking %s", index, status,                                           \
            color_names[CONFIG_ARCA_LED_WIDGET_CONN_COLOR_##color_label])
#define LOG_CONN_PERIPHERAL(status, color_label)                                                   \
    LOG_INF("Peripheral %s, blinking %s", status,                                                  \
            color_names[CONFIG_ARCA_LED_WIDGET_CONN_COLOR_##color_label])
#define LOG_BATTERY(battery_level, color_label)                                                    \
    LOG_INF("Battery level %d, blinking %s", battery_level,                                        \
            color_names[CONFIG_ARCA_LED_WIDGET_BATTERY_COLOR_##color_label])

// a blink work item as specified by the color and duration
struct blink_item {
    uint8_t color;
    uint16_t duration_ms;
    uint16_t sleep_ms;
};

// flag to indicate whether the initial boot up sequence is complete
static bool initialized = false;

// track current color for persistent indicators (layer color)
uint8_t led_current_color = 0;
// ★3bitの色(0..7)では表せないものを出しているときの印。
//   点滅の直前に「同じ色なら一度消して見せる」判定に使うので、**どの色とも一致しない**
//   値でなければならない。充電の常灯（オレンジ）がこれに当たる。
#define LED_COLOR_RAW 0xff

// 1画素へ**生のRGB**を書く。★色ビットではなく実値を受ける。
//   充電の常灯はオレンジ＝R/G/Bのon/offでは作れないので、ここを分けた。
// ★★2026-08-26: **device_is_ready() を見ずに叩いていた**。SPI1/ws2812 の初期化が
//   失敗していると api が不正なまま呼ばれ、**ハードフォールトでシステムごと停止する**。
//   実際 rev3 の R（LED1を初めて実装）で、ログが
//   `Indicating initial battery status` の直後で **全スレッドぶん止まった**。
//   落ちるのではなく **1回だけ理由を出して黙る** ようにする。
//   LEDは飾りなので、これでキーボードとしては生き残る。
#if !ARCA_LED_STRIP
// ★GPIO は点くか消えるかだけ＝**0 より大きければ点ける**。明るさ（ARCA_LED_BRIGHTNESS）と
//   充電の残量グラデ（R/G の混ぜ具合）は出せない＝混ぜた色は最寄りの 8 色になる
//   （例: 充電中のオレンジ → 黄）。XIAO の赤い充電LED（ハード）は別に点く。
static void led_write(uint8_t r, uint8_t g, uint8_t b) {
    static bool dev_bad = false;
    if (!device_is_ready(led_dev)) {
        if (!dev_bad) {
            dev_bad = true;
            LOG_ERR("内蔵LED（gpio-leds）が ready でない。以後この表示は行わない");
        }
        return;
    }
    const uint8_t v[3] = {r, g, b};
    for (int i = 0; i < 3; i++) {
        int rc = v[i] ? led_on(led_dev, rgb_idx[i]) : led_off(led_dev, rgb_idx[i]);
        if (rc) {
            LOG_ERR("内蔵LED %d への書き込みに失敗: %d", i, rc);
        }
    }
}
#else
static void led_write(uint8_t r, uint8_t g, uint8_t b) {
    static bool dev_bad = false;
    if (!device_is_ready(led_dev)) {
        if (!dev_bad) {
            dev_bad = true;
            LOG_ERR("SK6812のデバイスが ready でない（SPI1の初期化を疑う）。"
                    "以後この表示は行わない");
        }
        return;
    }
    struct led_rgb px = {.r = r, .g = g, .b = b};
    int rc = led_strip_update_rgb((struct device *)led_dev, &px, 1);
    if (rc) {
        LOG_ERR("SK6812への書き込みに失敗: %d", rc);
    }
}
#endif

// low-level method to control the LED
// ★ここが本家との唯一の実質的な違い。
//   本家は 3本のGPIO LED を個別に on/off する。Arcaは **1個のアドレサブルLED** なので、
//   色ビット(R=BIT(0)/G=BIT(1)/B=BIT(2))を1画素にまとめて strip へ書く。
//   明るさは CONFIG_ARCA_LED_BRIGHTNESS（SK6812は素だと眩しい）。
static void set_rgb_leds(uint8_t color, uint16_t duration_ms) {
    const uint8_t v = CONFIG_ARCA_LED_BRIGHTNESS;
    led_write((color & BIT(0)) ? v : 0, (color & BIT(1)) ? v : 0, (color & BIT(2)) ? v : 0);
    led_current_color = color;
    if (duration_ms > 0) {
        k_sleep(K_MSEC(duration_ms));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 休符 — 点滅していないときに出しっぱなしにする色 / 2026-08-30
//
// ★このモジュールは元々「点滅＝一過性の事件／点滅のあと必ず led_layer_color へ戻る」
//   という作りになっている。充電は**続いている状態**なので、新しい表示を割り込ませるのではなく
//   その戻り先＝休符に載せるのが既存設計に一番素直で、点滅側を一切触らずに済む。
//
// 優先順位: レイヤ色(SHOW_LAYER_COLORS) > 充電 > 消灯
//   ＝ユーザーが明示的に割り当てた表示が、自動の充電表示に勝つ。
//   Arca の現在の .conf では SHOW_LAYER_COLORS は n なので、休符＝充電表示か消灯。
// ══════════════════════════════════════════════════════════════════════════
#if IS_ENABLED(CONFIG_ARCA_LED_CHARGE)
// いま休符が見せている充電状態。★「変わったか」の比較対象なので、
//   実際に塗った直後にだけ更新する（塗らずに更新すると取りこぼす）
static enum arca_charge_state charge_shown = ARCA_CHARGE_NONE;
// ★いま休符が塗っている残量(%)。2026-09-01 に色を残量グラデにしたのに、ポーリングは
//   charge_shown（NONE/ACTIVE/FULL）しか比べていなかった → 充電中に%が上がっても塗り直さず、
//   **置いた瞬間の色で固まる**。左はレイヤ色(SHOW_LAYER_COLORS)の通知が来るたびに
//   set_rest が走るので隠れていた。右(peripheral)はキーマップを持たずレイヤ通知が来ない＝
//   ドックに置き直す（NONE→ACTIVE）まで色が変わらなかった（2026-09-25 実機報告）。
static uint8_t charge_lvl_shown = 0;
#endif

// ★実体はこのファイルの下（SHOW_LAYER_COLORS のブロックの手前）。
//   休符はレイヤ色より後ろで定義されているものを先に読むので、前方宣言が要る。
extern uint8_t led_layer_color;

static void set_rest(uint16_t sleep_ms) {
#if IS_ENABLED(CONFIG_ARCA_LED_CHARGE)
    enum arca_charge_state s = arca_charge_get();
    charge_shown = s;
    if (led_layer_color == 0 && s != ARCA_CHARGE_NONE) {
        // ★2026-09-01（ユーザー要望）: 固定オレンジ→**残量で色が変わる**＋充電表示だけ弱く。
        //   赤(0%)→琥珀→緑(100%)の連続グラデ: r=v*(100-lvl)/100, g=v*lvl/100。
        //   FULLも式の帰結として緑になる（特別扱い不要）。輝度は専用の
        //   ARCA_LED_CHARGE_BRIGHTNESS（既定12・通常表示の40より弱い）。
        uint8_t lvl = zmk_battery_state_of_charge();
        charge_lvl_shown = lvl;
        const uint16_t v = CONFIG_ARCA_LED_CHARGE_BRIGHTNESS;
        led_write((uint8_t)(v * (100 - lvl) / 100), (uint8_t)(v * lvl / 100), 0);
        led_current_color = LED_COLOR_RAW;
        if (sleep_ms > 0) {
            k_sleep(K_MSEC(sleep_ms));
        }
        return;
    }
#endif
    set_rgb_leds(led_layer_color, sleep_ms);
}

// define message queue of blink work items, that will be processed by a
// separate thread
K_MSGQ_DEFINE(led_msgq, sizeof(struct blink_item), 16, 1);

static void indicate_connectivity_internal(void) {
    struct blink_item blink = {.duration_ms = CONFIG_ARCA_LED_WIDGET_CONN_BLINK_MS};

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    // ★ZMK main では `zmk_endpoints_selected()` は **`zmk_endpoint_get_selected()`** に改名済み
    //   （本家 rgbled-widget は v0.3 世代のまま）。R(peripheral)はこのブロックごと
    //   #if で消えるので**Lをビルドするまで露見しない**種類の差分。
    switch (zmk_endpoint_get_selected().transport) {
    case ZMK_TRANSPORT_USB:
#if IS_ENABLED(CONFIG_ARCA_LED_WIDGET_CONN_SHOW_USB)
        LOG_INF("USB connected, blinking %s", color_names[CONFIG_ARCA_LED_WIDGET_CONN_COLOR_USB]);
        blink.color = CONFIG_ARCA_LED_WIDGET_CONN_COLOR_USB;
        break;
#endif
    default: // ZMK_TRANSPORT_BLE
#if IS_ENABLED(CONFIG_ZMK_BLE)
        uint8_t profile_index = zmk_ble_active_profile_index();
        if (zmk_ble_active_profile_is_connected()) {
            LOG_CONN_CENTRAL(profile_index, "connected", CONNECTED);
            blink.color = CONFIG_ARCA_LED_WIDGET_CONN_COLOR_CONNECTED;
        } else if (zmk_ble_active_profile_is_open()) {
            LOG_CONN_CENTRAL(profile_index, "open", ADVERTISING);
            blink.color = CONFIG_ARCA_LED_WIDGET_CONN_COLOR_ADVERTISING;
        } else {
            LOG_CONN_CENTRAL(profile_index, "not connected", DISCONNECTED);
            blink.color = CONFIG_ARCA_LED_WIDGET_CONN_COLOR_DISCONNECTED;
        }
#endif
        break;
    }
#elif IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
    if (zmk_split_bt_peripheral_is_connected()) {
        LOG_CONN_PERIPHERAL("connected", CONNECTED);
        blink.color = CONFIG_ARCA_LED_WIDGET_CONN_COLOR_CONNECTED;
    } else {
        LOG_CONN_PERIPHERAL("not connected", DISCONNECTED);
        blink.color = CONFIG_ARCA_LED_WIDGET_CONN_COLOR_DISCONNECTED;
    }
#endif

    k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
}

static int led_output_listener_cb(const zmk_event_t *eh) {
    if (initialized) {
        indicate_connectivity();
    }
    return 0;
}

// debouncing to ignore all but last connectivity event, to prevent repeat blinks
static struct k_work_delayable indicate_connectivity_work;
static void indicate_connectivity_cb(struct k_work *work) { indicate_connectivity_internal(); }
void indicate_connectivity() { k_work_reschedule(&indicate_connectivity_work, K_MSEC(16)); }

ZMK_LISTENER(led_output_listener, led_output_listener_cb);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
// run led_output_listener_cb on endpoint and BLE profile change (on central)
#if IS_ENABLED(CONFIG_ARCA_LED_WIDGET_CONN_SHOW_USB)
ZMK_SUBSCRIPTION(led_output_listener, zmk_endpoint_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(led_output_listener, zmk_ble_active_profile_changed);
#endif // IS_ENABLED(CONFIG_ZMK_BLE)
#elif IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
// run led_output_listener_cb on peripheral status change event
ZMK_SUBSCRIPTION(led_output_listener, zmk_split_peripheral_status_changed);
#endif

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
static inline uint8_t get_battery_color(uint8_t battery_level) {
    if (battery_level == 0) {
        LOG_INF("Battery level undetermined (zero), blinking %s",
                color_names[CONFIG_ARCA_LED_WIDGET_BATTERY_COLOR_MISSING]);
        return CONFIG_ARCA_LED_WIDGET_BATTERY_COLOR_MISSING;
    }
    if (battery_level >= CONFIG_ARCA_LED_WIDGET_BATTERY_LEVEL_HIGH) {
        LOG_BATTERY(battery_level, HIGH);
        return CONFIG_ARCA_LED_WIDGET_BATTERY_COLOR_HIGH;
    }
    if (battery_level >= CONFIG_ARCA_LED_WIDGET_BATTERY_LEVEL_LOW) {
        LOG_BATTERY(battery_level, MEDIUM);
        return CONFIG_ARCA_LED_WIDGET_BATTERY_COLOR_MEDIUM;
    }
    LOG_BATTERY(battery_level, LOW);
    return CONFIG_ARCA_LED_WIDGET_BATTERY_COLOR_LOW;
}

void indicate_battery(void) {
    struct blink_item blink = {.duration_ms = CONFIG_ARCA_LED_WIDGET_BATTERY_BLINK_MS};
    int retry = 0;

#if IS_ENABLED(CONFIG_ARCA_LED_WIDGET_BATTERY_SHOW_SELF) ||                                          \
    IS_ENABLED(CONFIG_ARCA_LED_WIDGET_BATTERY_SHOW_PERIPHERALS)
    uint8_t battery_level = zmk_battery_state_of_charge();
    while (battery_level == 0 && retry++ < 10) {
        k_sleep(K_MSEC(100));
        battery_level = zmk_battery_state_of_charge();
    };

    blink.color = get_battery_color(battery_level);
    k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
#endif

#if IS_ENABLED(CONFIG_ARCA_LED_WIDGET_BATTERY_SHOW_PERIPHERALS) ||                                   \
    IS_ENABLED(CONFIG_ARCA_LED_WIDGET_BATTERY_SHOW_ONLY_PERIPHERALS)
    for (uint8_t i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        uint8_t peripheral_level;
#if __has_include(<zmk/split/central.h>)
        int ret = zmk_split_central_get_peripheral_battery_level(i, &peripheral_level);
#else
        int ret = zmk_split_get_peripheral_battery_level(i, &peripheral_level);
#endif
        if (ret == 0) {
            retry = 0;
            while (peripheral_level == 0 && retry++ < (CONFIG_ARCA_LED_WIDGET_BATTERY_BLINK_MS +
                                                       CONFIG_ARCA_LED_WIDGET_INTERVAL_MS) /
                                                          100) {
                k_sleep(K_MSEC(100));
#if __has_include(<zmk/split/central.h>)
                zmk_split_central_get_peripheral_battery_level(i, &peripheral_level);
#else
                zmk_split_get_peripheral_battery_level(i, &peripheral_level);
#endif
            }

            LOG_INF("Got battery level for peripheral %d:", i);
            blink.color = get_battery_color(peripheral_level);
            k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
        } else {
            LOG_ERR("Error looking up battery level for peripheral %d", i);
        }
    }
#endif
}

static int led_battery_listener_cb(const zmk_event_t *eh) {
    if (!initialized) {
        return 0;
    }

    // check if we are in critical battery levels at state change, blink if we are
    uint8_t battery_level = as_zmk_battery_state_changed(eh)->state_of_charge;

    if (battery_level > 0 && battery_level <= CONFIG_ARCA_LED_WIDGET_BATTERY_LEVEL_CRITICAL) {
        LOG_BATTERY(battery_level, CRITICAL);

        struct blink_item blink = {.duration_ms = CONFIG_ARCA_LED_WIDGET_BATTERY_BLINK_MS,
                                   .color = CONFIG_ARCA_LED_WIDGET_BATTERY_COLOR_CRITICAL};
        k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
    }
    return 0;
}

// run led_battery_listener_cb on battery state change event
ZMK_LISTENER(led_battery_listener, led_battery_listener_cb);
ZMK_SUBSCRIPTION(led_battery_listener, zmk_battery_state_changed);
#endif // IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)

uint8_t led_layer_color = 0;
#if SHOW_LAYER_COLORS
void update_layer_color(void) {
    uint8_t index = zmk_keymap_highest_layer_active();

    if (led_layer_color != layer_color_idx[index]) {
        led_layer_color = layer_color_idx[index];
        struct blink_item color = {.color = led_layer_color};
        LOG_INF("Setting layer color to %s for layer %d", color_names[led_layer_color], index);
        k_msgq_put(&led_msgq, &color, K_NO_WAIT);
    }
}

static int led_layer_color_listener_cb(const zmk_event_t *eh) {
    struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);

    // check if this is indeed an activity state changed event
    if (ev != NULL) {
        switch (ev->state) {
        case ZMK_ACTIVITY_SLEEP:
            LOG_INF("Detected sleep activity state, turn off LED");
            set_rgb_leds(0, 0);
            break;
        default: // not handling IDLE and ACTIVE yet
            break;
        }
        return 0;
    }

    // it must be a layer change event instead
    if (initialized) {
        update_layer_color();
    }
    return 0;
}

// run layer_color_listener_cb on layer status change event and activity state event
ZMK_LISTENER(led_layer_color_listener, led_layer_color_listener_cb);
ZMK_SUBSCRIPTION(led_layer_color_listener, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(led_layer_color_listener, zmk_activity_state_changed);
#endif // SHOW_LAYER_COLORS

// ★ガードは widget.h の ARCA_LED_HAS_KEYMAP に一本化した。ここと listener 側で
//   別々に条件を書いていたせいで、片方だけ central 限定になってリンクで落ちていた。
#if ARCA_LED_HAS_KEYMAP
void indicate_layer(void) {
    uint8_t index = zmk_keymap_highest_layer_active();
    static const struct blink_item blink = {.duration_ms = CONFIG_ARCA_LED_WIDGET_LAYER_BLINK_MS,
                                            .color = CONFIG_ARCA_LED_WIDGET_LAYER_COLOR,
                                            .sleep_ms = CONFIG_ARCA_LED_WIDGET_LAYER_BLINK_MS};
    static const struct blink_item last_blink = {.duration_ms = CONFIG_ARCA_LED_WIDGET_LAYER_BLINK_MS,
                                                 .color = CONFIG_ARCA_LED_WIDGET_LAYER_COLOR};
    LOG_INF("Blinking %d times %s for layer change", index,
            color_names[CONFIG_ARCA_LED_WIDGET_LAYER_COLOR]);

    for (int i = 0; i < index; i++) {
        if (i < index - 1) {
            k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
        } else {
            k_msgq_put(&led_msgq, &last_blink, K_NO_WAIT);
        }
    }
}
#endif // ARCA_LED_HAS_KEYMAP

#if SHOW_LAYER_CHANGE
static struct k_work_delayable layer_indicate_work;

static int led_layer_listener_cb(const zmk_event_t *eh) {
    // ignore if not initialized yet or layer off events
    if (initialized && as_zmk_layer_state_changed(eh)->state) {
        k_work_reschedule(&layer_indicate_work, K_MSEC(CONFIG_ARCA_LED_WIDGET_LAYER_DEBOUNCE_MS));
    }
    return 0;
}

static void indicate_layer_cb(struct k_work *work) { indicate_layer(); }

ZMK_LISTENER(led_layer_listener, led_layer_listener_cb);
ZMK_SUBSCRIPTION(led_layer_listener, zmk_layer_state_changed);
#endif // SHOW_LAYER_CHANGE

extern void led_process_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

    k_work_init_delayable(&indicate_connectivity_work, indicate_connectivity_cb);

#if SHOW_LAYER_CHANGE
    k_work_init_delayable(&layer_indicate_work, indicate_layer_cb);
#endif

    while (true) {
        // wait until a blink item is received and process it
        struct blink_item blink;
#if IS_ENABLED(CONFIG_ARCA_LED_CHARGE)
        // ★充電状態には**事件が無い**。右(peripheral)は CONFIG_ZMK_USB すら立たないので
        //   zmk_usb_conn_state_changed も飛んでこない（zmk/app/Kconfig:134 の depends）。
        //   → msgq の待ちに時限を付けて、暇なときだけ見に行く。
        //   ★スレッドを増やさない。**LEDへ書くのはこのスレッドだけ**という不変を保つ
        //     （別スレッドから led_strip_update_rgb を叩くと SPI1 を取り合う）。
        if (k_msgq_get(&led_msgq, &blink, K_MSEC(CONFIG_ARCA_LED_CHARGE_POLL_MS)) != 0) {
            // ★状態だけでなく**残量**も比べる（色が残量で決まるので）。
            //   %はZMKのキャッシュ（BATTERY_REPORT_INTERVAL ごとに更新）を読むだけ。
            enum arca_charge_state s = arca_charge_get();
            if (s != charge_shown ||
                (s != ARCA_CHARGE_NONE && zmk_battery_state_of_charge() != charge_lvl_shown)) {
                set_rest(0);
            }
            continue;
        }
#else
        k_msgq_get(&led_msgq, &blink, K_FOREVER);
#endif
        if (blink.duration_ms > 0) {
            LOG_DBG("Got a blink item from msgq, color %d, duration %d", blink.color,
                    blink.duration_ms);

            // Blink the leds, using a separation blink if necessary
            if (blink.color == led_current_color && blink.color > 0) {
                set_rgb_leds(0, CONFIG_ARCA_LED_WIDGET_INTERVAL_MS);
            }
            set_rgb_leds(blink.color, blink.duration_ms);
            if (blink.color == led_layer_color && blink.color > 0) {
                set_rgb_leds(0, CONFIG_ARCA_LED_WIDGET_INTERVAL_MS);
            }
            // wait interval before processing another blink
            // ★戻り先は「レイヤ色」ではなく**休符**。充電中はここが充電表示になる
            set_rest(blink.sleep_ms > 0 ? blink.sleep_ms : CONFIG_ARCA_LED_WIDGET_INTERVAL_MS);

        } else {
            LOG_DBG("Got a layer color item from msgq, color %d", blink.color);
            // ★duration 0 ＝「休符の色が変わった」の合図（update_layer_color から）。
            //   led_layer_color は送り手が先に更新済みなので、ここは休符を塗り直すだけ。
            //   set_rgb_leds(blink.color, 0) のままだと、レイヤ色が 0 に戻ったときに
            //   **充電中でも消灯**してしまう。
            set_rest(0);
        }
    }
}

// define led_process_thread with stack size 1024, start running it 100 ms after
// boot
K_THREAD_DEFINE(led_process_tid, 1024, led_process_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);

// ★2026-09-04: 前回のリセットが**フラッシュ操作中**だったかを起動時に報告する（Lの断続リセット調査）。
//   soc_flash_nrf.c（arca-fixes）が消去/書き込みの間だけ noinit RAM のマーカーを立てる。リセットを跨いで残るので、
//   起動直後（PRE_KERNEL_1＝設定書き込みが走る前）に写し取って消し、LEDスレッドで **白点滅（消去=3回／書き込み=2回）**。
//   nRF52のページ消去は約90msのCPU停止＝BLEコントローラのprepareパイプラインが溢れる（lll.c:894）仮説の正の証拠になる。
extern volatile uint32_t nrf_flash_op_in_progress __attribute__((weak));
static uint32_t flash_op_at_reset;
static int capture_flash_op_marker(void) {
    if (&nrf_flash_op_in_progress != NULL) {
        flash_op_at_reset = nrf_flash_op_in_progress;
        nrf_flash_op_in_progress = 0U;
    }
    return 0;
}
SYS_INIT(capture_flash_op_marker, PRE_KERNEL_1, 0);

static void indicate_flash_op_at_reset(void) {
    if (!IS_ENABLED(CONFIG_ARCA_LED_DEBUG_FLASH_MARKER)) {
        return;   // ★2026-10-16: 表面のデバッグ表示は既定で出さない（Kconfig の説明）
    }
    int n = (flash_op_at_reset == 0xF1A5E001U) ? 3 : (flash_op_at_reset == 0xF1A5E002U) ? 2 : 0;
    if (n == 0) {
        return;
    }
    LOG_WRN("previous reset happened during flash %s", n == 3 ? "ERASE" : "WRITE");
    struct blink_item blink = {.color = 7, .duration_ms = 150, .sleep_ms = 150};
    for (int i = 0; i < n; i++) {
        k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
    }
    k_sleep(K_MSEC(n * 300 + 200));
}

extern void led_init_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);
    indicate_flash_op_at_reset();

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    // check and indicate battery level on thread start
    LOG_INF("Indicating initial battery status");

    indicate_battery();

    // wait until blink should be displayed for further checks
    k_sleep(K_MSEC(CONFIG_ARCA_LED_WIDGET_BATTERY_BLINK_MS + CONFIG_ARCA_LED_WIDGET_INTERVAL_MS));
#endif // IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)

    // check and indicate current profile or peripheral connectivity status
    LOG_INF("Indicating initial connectivity status");
    indicate_connectivity();

#if SHOW_LAYER_COLORS
    LOG_INF("Setting initial layer color");
    update_layer_color();
#endif // SHOW_LAYER_COLORS

    initialized = true;
    LOG_INF("Finished initializing LED widget");
}

// run init thread on boot for initial battery+output checks
K_THREAD_DEFINE(led_init_tid, 1024, led_init_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 200);

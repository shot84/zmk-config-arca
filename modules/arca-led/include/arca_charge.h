// Arca — 充電状態の判定（VBUSの有無 × 電池残量）/ 2026-08-30
//
// ══════════════════════════════════════════════════════════════════════════
// ★なぜ LED モジュールの中にあるのに LCD からも include するのか
//
//   zmk-module-arca-led/CMakeLists.txt の `zephyr_include_directories(include)` は
//   **CONFIG に関係なく無条件**に走る。対して LCD 側の include は
//   `if(CONFIG_ARCA_LCD)` の中にある。build.sh の切り分け版には
//     ・left_nolcd … CONFIG_ARCA_LCD=n / ARCA_LED_WIDGET=y
//     ・r2_nold   … CONFIG_ARCA_LCD=y / ARCA_LED_WIDGET=n
//   の**両方**があるので、どちらか一方のモジュールに置くと、もう片方の版が落ちる。
//   両方が生き残る置き場はここだけ。
//   ★中身は **static inline だけ**にしてある＝リンク上の依存は作らない
//     （include パスの依存だけ）。3本目のモジュールを増やすと build.sh の
//     `ZMK_EXTRA_MODULES` を12箇所直すことになり、足し忘れの穴が増える。
//
// ══════════════════════════════════════════════════════════════════════════
// ★VBUS の読み方 — **ZMK の USB 状態は使えない**
//
//   `CONFIG_ZMK_USB` は zmk/app/Kconfig:134 で
//       depends on (!ZMK_SPLIT || (ZMK_SPLIT && ZMK_SPLIT_ROLE_CENTRAL))
//   ＝**右(peripheral)では立てられない**。実際 build/right/zephyr/.config に無い。
//   `zmk_usb_is_powered()` を使うと、右半分だけ充電表示が丸ごと死ぬ。
//
//   nRF52840 の POWER.USBREGSTATUS.VBUSDETECT は USB スタックとは無関係な
//   ハードのコンパレータなので、**左右どちらでも・USBが動いていなくても**読める
//   （Zephyr の usb_dc_nrfx.c 自身が、USBを起こす前の判定にこれを使っている）。
//
//   ドックのポゴ給電も同じビットに乗る。基板側の正典 chat-files/ark_body_net.py:
//       w("V5",        "U1.VBUS",    "DS1.K")
//       w("VBUS_DOCK", "J_DOCK.V+",  "DS1.A")
//   ＝ドックの V+ はショットキー DS1(SS14) を通って XIAO の VBUS パッドへ入り、
//     そこは USB VBUS と内部直結。だから **USB でもドックでも同じビットが立つ**。
//
// ══════════════════════════════════════════════════════════════════════════
// ★この判定の限界（推測ではなく、仕組み上そうなる）
//
//   XIAO nRF52840 の充電IC(BQ25101)の CHG 出力は**ピンに出ていない**
//   （Zephyr のボード定義にも無い）ので、「本当に充電が終わったか」は読めない。
//   ここでの満充電は「給電されていて、かつ電池の読みが FULL_PCT 以上」でしかない。
//   ★充電中は端子電圧が持ち上がるので、**セルが満タンになる前に 100% に張り付く**。
//     ＝満充電の表示は少し早めに出る。これは近似であって計測ではない。
#pragma once

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zmk/battery.h>

#if defined(CONFIG_SOC_FAMILY_NORDIC_NRF)
#include <hal/nrf_power.h>
#endif

enum arca_charge_state {
    ARCA_CHARGE_NONE = 0, // 給電されていない
    ARCA_CHARGE_ACTIVE,   // 給電あり・満充電ではない ＝ 充電中
    ARCA_CHARGE_FULL,     // 給電あり・電池の読みが満タン
};

// USB でもドックでも「5Vが来ているか」。★左右どちらでも同じ答えが出る
static inline bool arca_vbus_present(void) {
#if defined(CONFIG_SOC_FAMILY_NORDIC_NRF) && defined(NRF_POWER_HAS_USBREG) && NRF_POWER_HAS_USBREG
    return nrf_power_usbregstatus_vbusdet_get(NRF_POWER);
#else
    // nRF 以外へ移したときのため。★黙って「給電なし」に倒す（表示が出ないだけで壊れない）
    return false;
#endif
}

static inline enum arca_charge_state arca_charge_get(void) {
    if (!arca_vbus_present()) {
        return ARCA_CHARGE_NONE;
    }
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    uint8_t pct = zmk_battery_state_of_charge();
    // ★0 は「まだ一度も測れていない」（起動直後）。満充電と紛らわしいので充電中に倒す
    if (pct > 0 && pct >= CONFIG_ARCA_CHARGE_FULL_PCT) {
        return ARCA_CHARGE_FULL;
    }
#endif
    return ARCA_CHARGE_ACTIVE;
}

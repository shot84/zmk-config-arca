// Arca 状態LED — 公開プロトタイプ
// ★本家(zmk-rgbled-widget)は include/zmk_rgbled_widget/widget.h でこれを配っていた。
//   移植時に src/widget.c だけコピーしてヘッダを置き忘れ、fatal error になった（2026-08-24）。
//   関数は widget.c 内で **定義より前に呼ばれる**（indicate_connectivity が 178行目）ので
//   前方宣言は必須。他モジュールから叩きたくなった時もここを include する。
#pragma once

void indicate_battery(void);
void indicate_connectivity(void);
void indicate_layer(void);

// ★本家はこの2つを **ヘッダ側で** 定義していた（#if で使うので IS_ENABLED 必須）。
//   写し忘れると `'SHOW_LAYER_CHANGE' undeclared` で落ちる。
#include <zephyr/sys/util.h>

// ══════════════════════════════════════════════════════════════════════════
// ★★このモジュールの「レイヤ」表示は **central でしか成立しない** / 2026-08-30
//
//   split の peripheral は zmk/app/CMakeLists.txt:48 の
//     if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
//   の**外**なので、`src/keymap.c` も `src/events/layer_state_changed.c` も
//   ビルドに入らない ＝ 右半分には **レイヤという概念そのものが存在しない**
//   （build/right に keymap のオブジェクトが1つも無いことを実測で確認）。
//
//   ★conf を素直に両半へ入れたら、右が**リンクで**落ちた（2026-08-30に実際に踏んだ）:
//       undefined reference to `indicate_layer'
//       undefined reference to `as_zmk_layer_state_changed'
//       undefined reference to `zmk_event_zmk_layer_state_changed'
//     Kconfig は通す・コンパイルも通る・**リンクで初めて分かる**種類の穴だった。
//     `indicate_layer()` の定義側にだけ central のガードが掛かっていて、
//     呼ぶ側（listener）に掛かっていなかったのが原因。
//
//   → 条件をここに1本化する。conf は左右で同じ物が置けて、右では黙って消える。
//     ガードを2箇所に分けて書くと、また片方だけ直して同じ穴に落ちる。
// ══════════════════════════════════════════════════════════════════════════
#define ARCA_LED_HAS_KEYMAP                                                                        \
    (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

#define SHOW_LAYER_CHANGE                                                                          \
    (IS_ENABLED(CONFIG_ARCA_LED_WIDGET_SHOW_LAYER_CHANGE) && ARCA_LED_HAS_KEYMAP)
#define SHOW_LAYER_COLORS                                                                          \
    (IS_ENABLED(CONFIG_ARCA_LED_WIDGET_SHOW_LAYER_COLORS) && ARCA_LED_HAS_KEYMAP)

// Arca rev.2 の OLED — Jump レイヤの有無を L→R へ渡す / 2026-11-09
//   台帳 11-09「OLEDでJump用になってるかどうかわかるようにできる？右のロゴ部分変えて」
//   L: レイヤが変わるたび（と R がつながり直したとき）に送る。R: 受けた値を持つだけ（描くのは oled_screen.c）
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>

#include "oled_jump.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

ZMK_EVENT_IMPL(arca_oled_jump_event);

static bool jump_on;

bool arca_oled_jump_on(void) { return jump_on; }

ZMK_RELAY_EVENT_HANDLE(arca_oled_jump_event, ajm, );
ZMK_RELAY_EVENT_CENTRAL_TO_PERIPHERAL(arca_oled_jump_event, ajm, );

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>

static int jump_central_cb(const zmk_event_t *eh) {
    bool on = CONFIG_ARCA_OLED_JUMP_LAYER >= 0 &&
              zmk_keymap_layer_active(zmk_keymap_layer_index_to_id(CONFIG_ARCA_OLED_JUMP_LAYER));
    // R がつながり直したときは同じ値でも送る（切れている間に変わった分・R だけ起き直した分）
    bool reconnect = as_zmk_split_peripheral_status_changed(eh) != NULL;
    if (on != jump_on || reconnect) {
        jump_on = on;
        raise_arca_oled_jump_event((struct arca_oled_jump_event){.on = on});
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(arca_oled_jump_central, jump_central_cb);
ZMK_SUBSCRIPTION(arca_oled_jump_central, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(arca_oled_jump_central, zmk_split_peripheral_status_changed);
#else
static int jump_peripheral_cb(const zmk_event_t *eh) {
    const struct arca_oled_jump_event *ev = as_arca_oled_jump_event(eh);
    if (ev) jump_on = ev->on;
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(arca_oled_jump_peripheral, jump_peripheral_cb);
ZMK_SUBSCRIPTION(arca_oled_jump_peripheral, arca_oled_jump_event);
#endif

// Arca 検死装置（2026-08-31・R側TB捜査の副産物を恒久化）
//
// ■ なにをするか
//   1) 起動時に nRF の RESETREAS を印字（前回のリセットが PIN/WDT/SOFT/LOCKUP/電源 のどれか）
//   2) フォルト時に PC/LR/スレッド名/CFSR を __noinit RAM に記録して即リブート
//      → **次の起動のログで再生**（RSTのソフトリセットで SRAM は消えない）
//
// ■ なぜ要るか（実録 2026-08-31）
//   inputスレッドのスタックあふれ（1024で BLE通知の深さが入らない）が
//   「玉を転がすとリセットループ」として現れ、出力経路（USBログ）ごと巻き添えで
//   死ぬため現場のログが残らず、特定に丸一晩かかった。この装置があれば
//   ダンプ2行（thr=input / 野良PC）で終わっていた。
//   「毎回出る警告は、追跡する仕組みが無ければ出ていないのと同じ」の姉妹則:
//   **落ちた理由は、残す仕組みが無ければ存在しないのと同じ。**
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/fatal.h>
#include <zephyr/sys/reboot.h>
#include <soc.h>

LOG_MODULE_REGISTER(arca_autopsy, LOG_LEVEL_INF);

static __noinit struct {
    uint32_t magic;
    uint32_t uptime_ms;
    char buf[192];
} arca_autopsy;
#define AUTOPSY_MAGIC 0x41504f53 /* 'APOS' */

// ★本番は zmk-feature-watchdog が同名ハンドラでインシデント記録を持つ（多重定義になる）。
//   こちらは**ウォッチドッグ無効の診断ビルド用**の受け皿。
#if !IS_ENABLED(CONFIG_ZMK_WATCHDOG)
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf) {
    const char *tn = k_thread_name_get(k_current_get());
    int off = snprintf(arca_autopsy.buf, sizeof(arca_autopsy.buf),
                       "FATAL r=%u thr=%s cfsr=%08x hfsr=%08x", reason, tn ? tn : "?",
                       (unsigned)SCB->CFSR, (unsigned)SCB->HFSR);
    if (esf) {
        snprintf(arca_autopsy.buf + off, sizeof(arca_autopsy.buf) - off, " pc=%08x lr=%08x",
                 (unsigned)esf->basic.pc, (unsigned)esf->basic.lr);
    }
    arca_autopsy.uptime_ms = k_uptime_get_32();
    arca_autopsy.magic = AUTOPSY_MAGIC;
    sys_reboot(SYS_REBOOT_COLD);
}
#endif /* !CONFIG_ZMK_WATCHDOG */

static int arca_autopsy_init(void) {
    uint32_t rr = NRF_POWER->RESETREAS;
    NRF_POWER->RESETREAS = 0xFFFFFFFF;
    LOG_INF("RESETREAS=%08x%s%s%s%s%s", rr, (rr & 1) ? " PIN" : "", (rr & 2) ? " WDT" : "",
            (rr & 4) ? " SOFTREQ" : "", (rr & 8) ? " LOCKUP" : "",
            (rr == 0) ? " POR/BOD(電源)" : "");
    if (arca_autopsy.magic == AUTOPSY_MAGIC) {
        LOG_ERR("★前回のクラッシュ記録 up=%ums: %s", arca_autopsy.uptime_ms, arca_autopsy.buf);
    }
    arca_autopsy.magic = 0;
    return 0;
}
SYS_INIT(arca_autopsy_init, APPLICATION, 98);

// Arca rev.2 の OLED — 縦 32×128 の描画 / 2026-11-02
// ★形の正は図の生成器（chat-files/ark_oled_mock.py・ark_oled_mock_b.py）。
//   ここは oled_assets.h（その生成器から書き出したビット）を同じ規則で置くだけ。
//   配置の式を変えたら必ず `python3 -B chat-files/ark_oled_assets.py --check`（C と図を 1 画素ずつ照合）。
#include <string.h>
#include "oled_draw.h"
#include "oled_assets.h"

static void px_on(uint8_t *b, int x, int y) {
    if ((unsigned)x >= OLED_PW || (unsigned)y >= OLED_PH) return;
    b[y * OLED_STRIDE + (x >> 3)] |= (uint8_t)(0x80u >> (x & 7));
}

// 1bit の絵を (ox, oy) に重ねる（PIL の paste(g, xy, g) と同じ＝光る所だけ足す）
// dim: 絵の中の座標で (x+y) が奇数の画素を落とす（ark_oled_mock_b.mark_dim_img と同じ）
static void blit(uint8_t *b, const uint8_t *bits, int w, int h, int ox, int oy, bool dim) {
    int st = (w + 7) / 8;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (bits[y * st + (x >> 3)] & (0x80u >> (x & 7)))
                if (!dim || ((x + y) & 1) == 0) px_on(b, ox + x, oy + y);
}

static int floordiv(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }

static const struct oled_glyph *find(const char *set, const struct oled_glyph *tab, char c) {
    const char *p = strchr(set, c);
    return (c && p) ? &tab[p - set] : 0;
}

static void ring(uint8_t *b, int pct) {
    for (int y = 0; y < OLED_RING_D; y++)
        for (int x = 0; x < OLED_RING_D; x++) {
            uint8_t v = oled_ring[y * OLED_RING_D + x], t = v & 0x7f;
            if ((t && pct >= t) || (v & 0x80)) px_on(b, OLED_RING_X0 + x, OLED_RING_Y0 + y);
        }
}

// ark_oled_mock_b.center: (cx - w//2, cy - h//2)
static void center(uint8_t *b, const uint8_t *bits, int w, int h, bool dim) {
    blit(b, bits, w, h, OLED_CX - w / 2, OLED_CY - h / 2, dim);
}

// ark_oled_mock.stacked ＋ ark_oled_mock_b.word: 字を縦に積み、OLED_WORD_Y から修飾キーの段までの真ん中へ
static void word(uint8_t *b, const char *w) {
    const struct oled_glyph *g[sizeof(((struct oled_state *)0)->word)];
    int n = 0, hh = 0;
    for (const char *p = w; *p && n < (int)(sizeof g / sizeof g[0]); p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        const struct oled_glyph *q = find(OLED_WORD_CHARS, oled_word, c);
        if (!q) continue;
        g[n++] = q;
        hh += q->h;
    }
    if (!n) return;
    hh += OLED_WORD_GAP * (n - 1);
    int y = OLED_WORD_Y + floordiv(OLED_MOD_Y - 4 - OLED_WORD_Y - hh, 2);
    for (int i = 0; i < n; i++) {
        blit(b, &oled_word_bits[g[i]->off], g[i]->w, g[i]->h, (OLED_PW - g[i]->w) / 2, y, false);
        y += g[i]->h + OLED_WORD_GAP;
    }
}

// ark_oled_mock_b.mods: □ 4 つ（x = 4 + i*7・4×4）。押した物は塗り
static void mods(uint8_t *b, int m) {
    for (int i = 0; i < 4; i++) {
        int x0 = 4 + i * 7, y0 = OLED_MOD_Y;
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                if (((m >> i) & 1) || x == 0 || x == 3 || y == 0 || y == 3) px_on(b, x0 + x, y0 + y);
    }
}

void oled_draw(uint8_t *b, const struct oled_state *s) {
    memset(b, 0, OLED_BUF);
    ring(b, s->pct);
    if (s->mid == OLED_MID_USB) {
        center(b, oled_usb, OLED_USB_W, OLED_USB_H, s->dim);
    } else if (s->mid == OLED_MID_MARK) {
        center(b, oled_mark, OLED_MARK_W, OLED_MARK_H, s->dim);
    } else {
        const struct oled_glyph *q = find(OLED_MID_CHARS, oled_mid, s->mid_char);
        if (q) center(b, &oled_mid_bits[q->off], q->w, q->h, s->dim);
    }
    word(b, s->word);
    if (s->mods >= 0) mods(b, s->mods);
}

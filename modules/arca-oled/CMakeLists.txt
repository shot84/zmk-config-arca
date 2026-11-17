# Arca rev.2（v5）の OLED 画面 / 2026-11-02
# ★rev3 の LCD（zmk-module-arca-lcd）とは別。パネルが違う（SSD1306 128×32 を縦に置く）。
#   使い回しているのは ZMK/LVGL 側の作法だけ（カスタム画面の差し替え口・キャンバスへのバイト書き・状態の読み方）。
# ★`zephyr_library()` にしない＝app へ直接（zmk/*.h のパスが付かない＝状態 LED で踏んだ）
if(CONFIG_ARCA_OLED)
  target_sources(app PRIVATE src/oled_draw.c src/oled_screen.c src/oled_jump.c)
endif()

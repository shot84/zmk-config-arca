# ★`zephyr_library()` で別ライブラリにすると **zmk/*.h のインクルードパスが付かない**
#   （zmk/battery.h が見つからないで落ちる）。上流の rgbled-widget と同じく
#   **app ターゲットへ直接ソースを足す**のが正しい。
target_sources_ifdef(CONFIG_ARCA_LED_WIDGET app PRIVATE src/widget.c)

# ★include/ を通す（widget.h の前方宣言用）。app ターゲットに足したソースからも見えるよう
#   zephyr_include_directories（グローバル）を使う。上流も同じ。
zephyr_include_directories(include)

# ★検死装置（フォルトのPC/スレッド名をリセット越しに残す）。ARCA_LED_WIDGETとは独立
target_sources_ifdef(CONFIG_ARCA_AUTOPSY app PRIVATE src/autopsy.c)

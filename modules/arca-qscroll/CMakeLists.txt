# Arca の 5way の上下スクロール / 2026-11-04
# ★元は taichan1113/zmk-behavior-quick-scroll（aa679aa・MIT・アラサーBP さん）。Arca 用に 2 つ目の引数（短押しで送るキー）を足した。
# ★`zephyr_library()` にしない＝app へ直接（zmk/*.h のパスが付かない＝状態 LED で踏んだ）
# 押したキーの処理は中央（L）だけ＝周辺（R）では組まない（R でキーの事件が無くリンクで止まった・ZMK の &kp と同じ扱い）
if(CONFIG_MODULA_BEHAVIOR_QUICK_SCROLL AND ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL))
  target_sources(app PRIVATE src/behaviors/behavior_quick_scroll.c)
endif()

# zmk-config-arca

Arca rev.2 のファームウェア（ZMK）。分割・無線・左右にトラックボール（PMW3610）・OLED（SSD1306 128×32）・XIAO nRF52840。

ZMK は [cormoran/zmk の `main+dya`](https://github.com/cormoran/zmk/wiki/Available-Custom-Studio-RPC-aware-Branches)（Zephyr 4.1）を使っています。

## ファームの入手

1. このリポジトリの **Actions** → いちばん上の成功した **Build** を開く
2. 下の **Artifacts** の `firmware` をダウンロードして展開する

| ファイル | 書き込む先 |
|---|---|
| `arca_v5_left.uf2` | 左 |
| `arca_v5_right.uf2` | 右 |
| `settings_reset.uf2` | 不具合のときだけ（下の注意） |

## 書き込み

1. USB でつなぎ、XIAO の **RST ボタンを素早く 2 回**押す → `XIAO-SENSE` というドライブが出る
2. uf2 をそのドライブにコピーする（コピーが終わると自動で再起動してドライブは消える）
3. **左 → 右** の順に行う

`settings_reset.uf2` は**ペアリングと保存された設定をすべて消します**。左右がつながらなくなったときなどだけ使い、使った後は左右とも上の uf2 を書き直してペアリングし直してください。

## 設定の変更

左を USB でつなぎ、DYA（ZMK Studio の拡張）からキーマップやトラックボールの設定を変えられます。

## 中身

| 場所 | 中身 |
|---|---|
| `boards/shields/arca_v5/` | シールド（ピン・左右の設定） |
| `config/arca_v5.keymap` | キーマップ |
| `config/west.yml` | 使う ZMK・モジュールの版（固定） |
| `modules/arca-led` | 状態 LED |
| `modules/arca-oled` | OLED の画面 |
| `modules/arca-qscroll` | 5way のクイックスクロール |
| `patches/zephyr-arca.patch` | Zephyr の Bluetooth コントローラの修正（ビルド時に当てる） |

## ライセンス

自前のコードは MIT（`LICENSE`）。他から持ってきた物と OLED のフォントは `THIRD_PARTY_NOTICES.md`。

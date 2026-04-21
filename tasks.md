# 8086のDOSで動くvi風エディタ

- LSI-C86(試食版)でコンパイル
- SJIS仮定
- ANSI.SYSとxtermの両方にあるエスケープシーケンスで動作
- スモールモデル
  - 10KBとかそれぐらいのサイズまでのファイルを編集できる。
- キーバインドはvi風

# TODO
- [x] デザインドキュメント — `DESIGN.md`
- [x] 開発環境を準備 — `build/build.sh` (DOSBoxでLSI-C86を呼ぶ)、`Makefile`
- [x] テスト環境を準備 — `src/vitest.c` + `test/run.sh`、`make test`
- [x] 実装 — `src/vi.c` (16KB弱、50テスト全PASS)

## ビルドと実行
```
make             # vi.exe (DOS) + vi-host (Linux) をビルド
make test        # 単体テスト(95件)を実行
./test/manual.sh # DOSBox を開いて対話的に動作確認
./run-host.sh FILE  # SSH越しにLinuxで直接試せる (luit経由でSJIS↔UTF-8)
```

## サポートされているキー
ノーマル: `h j k l` 矢印 `0 $ G gg x dd i a A I o O : ZZ`
挿入: 印字可能文字 / `BS` / `CR` / `ESC` / `Ctrl+J`(→TUTモード)
ex: `:w [name]` `:q` `:q!` `:wq`
TUTモード(日本語入力): `rk→あ ek→か elk→が tltk→ぱ dlu→っ rz→熱 ,,→個` など2611エントリ(ひらがな/カタカナ/漢字)。
  配列は `src/tuttable.h` に生成済み。`make table` で再生成(ibus-tutcodeの rule を取得)。
  ESCでInsertモードに戻る。BSで打鍵中の1文字を戻す。

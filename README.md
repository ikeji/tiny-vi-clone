# vi-clone

8086 DOS 向けの vi 風エディタ。LSI C-86 試食版 (S モデル) でビルドし、
MS-DOS 2.0 以降 + ANSI.SYS で動く。SJIS テキストをそのまま編集でき、
TUT-Code による日本語入力を内蔵 (IME 不要)。

- 単一バイナリ、約 38KB の `.exe`
- `~10KB` までのテキストファイルを編集
- 8086 実機での実用速度 (差分再描画、TUT 二分探索、`VI_QUIET_STATUS` モード)
- Linux/gcc にもビルド可。SSH 越しに xterm でそのまま試せる

詳しい設計は [`DESIGN.md`](DESIGN.md)、実機ハング問題の調査履歴は [`DIAGNOSE.md`](DIAGNOSE.md)。

## ビルド

LSI C-86 試食版を `~/tmp/lsic` に展開していること、DOSBox が PATH にあることが前提。

```sh
make             # vi.exe (DOS) + vi-host (Linux) をビルド
make vi          # DOS 用だけ
make vi-quiet    # 実機向け省 IO ビルド (-DVI_QUIET_STATUS)
make host        # Linux/gcc 用だけ
make test        # 単体テストを DOSBox で実行
make table       # TUT 辞書 (src/tuttable.h) を再生成
make clean       # 生成物削除
```

`VI_DEFINES` 環境変数で `build/build.sh` に追加オプションを渡せる
(例: `VI_DEFINES=-DVI_QUIET_STATUS make vi`)。

## 実行

### DOSBox / 実機

```
vi filename.txt
```

ANSI.SYS をロードしておくこと。画面サイズは既定 80×25。`VI_COLS` /
`VI_ROWS` (または POSIX の `COLUMNS` / `LINES`) で上書き可。

### Linux ホスト (動作確認用)

```sh
./run-host.sh FILE       # luit 経由で SJIS↔UTF-8 を変換
./vi-host FILE           # 端末が SJIS なら直接
```

`run-host.sh` は `luit -encoding SJIS` で起動するので、UTF-8 端末
(xterm, tmux, ssh) でもそのまま日本語が読み書きできる。

## キーバインド

ノーマルモード:

```
h j k l 矢印    カーソル移動
0 $ G gg        行頭/行末/最終行/先頭行
x dd            1文字削除/行削除 (削除した内容はヤンクレジスタへ)
yy              行をヤンク
p P             レジスタの内容を後ろ/前に貼り付け
u               直前の変更を取り消す (もう一度 u でやり直し)
i I a A o O     挿入モードへ
:               ex コマンド
ZZ              :wq と同じ
ESC             保留中の d/y/g/Z をキャンセル
```

挿入モード:

```
印字可能文字     カーソル位置に挿入
BS               1文字削除 (SJIS 境界尊重)
CR               改行
Ctrl+J           TUT モード (日本語入力) へ
ESC              ノーマルモードへ
```

ex コマンド:

```
:w [name]   保存 (オプションで別名)
:q          終了 (未保存なら拒否)
:q!         強制終了
:wq         保存して終了
```

## 日本語入力 (TUT-Code)

挿入モードから `Ctrl+J` で TUT モードに入る。打鍵 → かな/カタカナ/漢字
(SJIS 2 バイト) に直接変換。

```
rk → あ      ek → か      elk → が
tltk → ぱ    dlu → っ
rz → 熱      ,, → 個
```

辞書は約 2600 エントリ。`src/tuttable.h` として生成済み。
[`ibus-tutcode`](https://github.com/deton/ibus-tutcode) の rule から
`build/gen_tut_table.py` で抽出している (`make table` で再生成)。

`ESC` で挿入モードに戻る。`BS` は打鍵中なら 1 ストローク戻し、
打鍵が空なら直前の文字を削除する。

## ファイル構成

```
src/
  vi.c             メイン実装 (単一ファイル)
  vitest.c         単体テスト
  tuttable.h       TUT 辞書 (生成物)
build/
  build.sh         DOS 向けビルド (DOSBox 越しに lcc)
  build-host.sh    Linux ホスト向けビルド (gcc)
  gen_tut_table.py TUT 辞書ジェネレータ
  patch_nofpu.py   cdos.obj の FPU 検出を無効化するポストパッチ
test/
  run.sh           DOSBox で .exe を実行する汎用ランナー
  manual.sh        対話セッションを開く
DESIGN.md          設計
DIAGNOSE.md        実機ハング問題の調査履歴
```

## ライセンス / クレジット

- TUT 辞書 (`src/tuttable.h`) は `ibus-tutcode` の `tutcode_rule.py`
  (GPL) から抽出した派生物
- それ以外のコードはこのリポジトリの作者による

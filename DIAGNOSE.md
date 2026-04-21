# 実機(MS-DOS 2.0 on HIDOS VM)でのハング問題 — 解決済み

## 原因

HIDOS VM が使う **libx86emu は x87 (8087) FPU 命令 (ESC prefix 0xD8-0xDF) を実装していない**。
LSI-C86 の `cdos.obj` startup が必ず最初に `FNINIT` / `FNSTSW` を実行して FPU を検出しようとするため、
未実装 opcode で暴走→ハング。

## 対策

ポストビルドで `FNINIT` (DB E3) を `JMP short +0x2E` に置換し、FPU検出コード全体を
スキップして「FPU 無し」の分岐へ直行させる(`build/patch_nofpu.py`)。
`build/build.sh` が自動適用するので、`make vi` でそのまま VM-compatible な `vi.exe` ができる。
DOSBox / 実 PC では無害。

--- 以下、切り分け履歴 ---



`hello.exe` (printf版) が実機で固まる件の切り分け用にビルドした3種類。
DOSBoxでは3つとも正しく出力します。

## 実行順(上から順に試してください)

| # | バイナリ         | 仕組み                     | サイズ  |
|---|------------------|----------------------------|---------|
| 1 | `hi_bdos.exe`    | INT 21h AH=09 を `bdosp()` 経由で直叩き (stdio非経由) | ~6KB   |
| 2 | `hi_write.exe`   | POSIX `write(1, ...)` (INT 21h AH=40、stdio バッファ非経由) | ~6KB   |
| 3 | `hi_crlf.exe`    | `printf("hi\r\n")` — CRLF明示 (stdioフル) | ~12KB  |
| 参 | `hello.exe`     | `printf("hi\n")` — 元の実装 (LFのみ) | ~12KB  |

## 期待結果と解釈

### 1だけ動く、2/3が動かない
- `bdos()` は直接INT 21h、`write()` / `printf()` はLSI-C86のファイルディスクリプタ層を経由
- この層が DOS 2.0 にない何か(IOCTL等)を呼んでいる可能性

### 1/2が動く、3だけ動かない
- 問題は stdio (stdout バッファリング、フォーマット処理)
- 出力はしているが flush されず、終了処理で詰まっている可能性

### 1/2/3 ぜんぶ動く
- 元の `hello.exe` だけの問題。LF→CRLF変換が CON ドライバでされていない
- 実は表示していたが行頭上書きで見えなかったパターン

### ぜんぶ動かない (3つとも固まる)
- startup (cdos.obj) が根本的に合っていない
- 次手: `tinymain.obj` 版を作るか、`.COM` 形式にする

## 実機での確認方法

転送後、まず:
```
dir hi_bdos.exe
```
DOSBox側のサイズ `5856` と一致するか確認 (ずれてたら転送破損)。

動かしたら:
```
hi_bdos
```
で `hi from bdos` が出れば1は動作。他2つも同様。

## 固まったとき

- `Ctrl+C` / `Ctrl+Break` で戻れるか(戻れれば入力待ち、戻れないとハング)
- 画面に何も出ていなくても `Ctrl+C` を数回打って `DIR` が通るなら、printf の出力が単に見えていないだけの可能性

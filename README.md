# w7telnet — a temporary, unauthenticated telnet listener

> **日本語版は下にあります。** / Japanese version follows below.

**w7telnet** is a small **telnet listener**: you start it by hand for a short
while, on a network you already know is safe, to get a first shell on an old
host (originally Windows 7) so you can do some work — then you stop it. It is
**not a daemon** and is not meant to run unattended. It hands an interactive
shell to whoever connects, **with no authentication**.

The listener binary is `w7telnet-listen`:

- **Windows** (`w7telnet-listen-x64.exe` / `-x86.exe`) — hands out `cmd.exe`.
  Built CRT-free so it runs on a stock Windows 7 with only `KERNEL32.dll` and
  `WS2_32.dll`.
- **macOS / Linux / other POSIX** (`w7telnet-listen`) — hands out an interactive
  shell inside a pseudo-terminal (`forkpty`), using the connecting user's login
  shell by default.

## Please read first
Because there is no authentication, anyone who can reach the listener gets to
run commands on the machine with the privileges of the process. Make sure you
fully understand **which hosts can reach it** and **what level of access you
are granting** before running it. If that is not clear to you, do not use it.
Start it only on isolated test LANs / VMs / closed lab environments, and **stop
it when you are done** — it is a temporary foothold, not a service to leave up.

## Layout
```
src/
  core/      OS-independent code (telnet IAC filtering)
  windows/   Win32 freestanding backend + build.bat / build-msvc.bat
  posix/     macOS / Linux / other POSIX backend (forkpty)
```
The protocol logic in `core/` is shared; only the OS-specific layer differs per
platform. A future Linux/BSD-specific tweak also belongs under `posix/`.
(If a client side is ever added, the natural counterpart name is
`w7telnet-connect`; today the project ships only the listener.)

## Build

### macOS / Linux (native)
`make` is standard on these systems, so no extra tooling is needed:
```bash
make            # builds ./w7telnet-listen for this host
```

### Windows binaries (cross-compiled from macOS/Linux with mingw-w64)
```bash
make win-x64    # -> w7telnet-listen-x64.exe
make win-x86    # -> w7telnet-listen-x86.exe
make win        # both
```

### Notes for macOS builds
Built and smoke-tested on Apple Silicon (arm64) macOS. Intel Macs are not tested yet.
- `make` needs the Xcode Command Line Tools (`xcode-select --install`).
- `make win-x64` / `make win-x86` need mingw-w64 (`brew install mingw-w64`).
- For a quick localhost test, start `./w7telnet-listen 2323 127.0.0.1` in one terminal. Wait briefly before sending input, or the connection closes before the shell answers:
  ```bash
  { sleep 1; printf 'echo hi\r\n'; sleep 1; printf 'exit\r\n'; sleep 1; } | nc 127.0.0.1 2323
  ```
- These macOS cross-compiled exe files were not run from this Mac; the Windows build has been verified separately on a real Windows 7 Ultimate host (both the gcc/mingw and MSVC builds).

### Windows, building on Windows without make
Two options, each producing a self-contained exe that runs on a stock
Windows 7. Both 32- and 64-bit outputs have been verified to depend only on
`KERNEL32.dll` and `WS2_32.dll`. Each script takes an optional architecture
argument — `x64` (default) or `x86` — and writes `w7telnet-listen-x64.exe` /
`w7telnet-listen-x86.exe`. Run from the repository root:

- **MSYS2 / mingw-w64 gcc** (CRT-free build):
  ```bat
  src\windows\build.bat          :: 64-bit -> w7telnet-listen-x64.exe
  src\windows\build.bat x86      :: 32-bit -> w7telnet-listen-x86.exe
  ```
  Needs the matching toolchain installed in MSYS2:
  `pacman -S mingw-w64-x86_64-gcc` (x64) / `pacman -S mingw-w64-i686-gcc`
  (x86). Set `MSYS2_ROOT` if MSYS2 is not at `C:\msys64`.
- **MSVC** (free Visual Studio / Build Tools, "Desktop development with C++"):
  ```bat
  src\windows\build-msvc.bat      :: 64-bit -> w7telnet-listen-x64.exe
  src\windows\build-msvc.bat x86  :: 32-bit -> w7telnet-listen-x86.exe
  ```
  The CRT is linked statically (`/MT`), so no VC runtime or UCRT DLLs are
  needed. The script finds VS 2022 automatically, or run it from the matching
  "Native Tools Command Prompt for VS".

### Everything buildable on this host
```bash
make all
```

## Usage

### Windows
Run from an Administrator command prompt (binding port 23 needs it):
```
w7telnet-listen-x64.exe                 :: listen on 0.0.0.0:23
w7telnet-listen-x64.exe 2323            :: port 2323
w7telnet-listen-x64.exe 23 127.0.0.1    :: localhost only (safest)
```
You may need to allow the port through Windows Firewall:
```
netsh advfirewall firewall add rule name="w7telnet" dir=in action=allow protocol=TCP localport=23
```

### macOS / Linux
Binding port 23 needs root; use an unprivileged port otherwise.
```bash
sudo ./w7telnet-listen                  # 0.0.0.0:23
./w7telnet-listen 2323                  # 0.0.0.0:2323
./w7telnet-listen 2323 127.0.0.1        # localhost only (safest)
./w7telnet-listen -s /bin/bash 2323     # force a specific shell
```
By default the connecting user's login shell is used, looked up from the
password database, then `$SHELL`, then `/bin/sh`. Use `-s` to override it.

## Connecting from a client
```
telnet <target-ip> 23
```
or PuTTY in Telnet mode. You get a shell prompt immediately. `exit` ends the
session; the listener itself stops with Ctrl+C.

## Notes
- One thread per connection, so multiple clients can connect at once.
- Incoming telnet IAC negotiation sequences are stripped before the bytes
  reach the shell (`src/core/telnet.c`).
- Local echo is left to the client (Windows telnet / PuTTY).
- The Windows build is **CRT-free** (no UCRT/msvcrt): it depends only on
  `KERNEL32.dll` and `WS2_32.dll`, both present on a stock Windows 7, so no
  extra runtime is required. (Homebrew's mingw-w64 is a UCRT-only build and a
  normal link would require `api-ms-win-crt-*.dll`, which is missing on
  Windows 7; building with only Win32 APIs avoids that.) The POSIX build has no
  such restriction and uses the normal C library.

Verify the Windows binary's DLL dependencies with:
```bash
x86_64-w64-mingw32-objdump -p w7telnet-listen-x64.exe | grep "DLL Name"
```

---

# w7telnet — 一時的な、認証なしの telnet listener（日本語）

**w7telnet** は小さな **telnet listener（待ち受け）** です。安全だと分かっている
ネットワークで、古いホスト（元は Windows 7）に入って作業するための「最初の壁」を
一時的に外す目的で、手で起動して、済んだら止める——という使い方を想定しています。
**daemon（常駐）ではありません。** 接続してきた相手に**認証なしで**対話シェルを
渡します。

待ち受けのバイナリ名は `w7telnet-listen` です:

- **Windows** (`w7telnet-listen-x64.exe` / `-x86.exe`) … `cmd.exe` を渡します。
  CRT 非依存ビルドなので、素の Windows 7 でも `KERNEL32.dll` と `WS2_32.dll`
  だけで動きます。
- **macOS / Linux / その他 POSIX** (`w7telnet-listen`) … 擬似端末 (`forkpty`) 上で
  対話シェルを渡します。既定では接続ユーザーのログインシェルを使います。

## 最初に読んでください
認証がないため、この listener に到達できる相手は、プロセスの権限でそのマシン上の
コマンドを実行できます。**どこからの接続を受け付けるか**、**どんな権限を与える
ことになるか**を充分に理解したうえで使用してください。意味が理解できない場合は
使用しないでください。隔離された検証用 LAN / VM / 閉じたラボ環境でのみ起動し、
**作業が済んだら止めてください**——常駐させるサービスではなく、一時的な足がかりです。

## 構成
```
src/
  core/      OS 非依存コード（telnet IAC 除去）
  windows/   Win32 freestanding 実装 + build.bat / build-msvc.bat
  posix/     macOS / Linux / その他 POSIX 実装（forkpty）
```
`core/` のプロトコル処理は共有で、OS 依存の層だけが環境ごとに異なります。
今後の Linux/BSD 固有の調整も `posix/` 配下に入れます。
（将来クライアント側を作る場合、自然な対の名前は `w7telnet-connect`。今は
listener だけを提供しています。）

## ビルド

### macOS / Linux（ネイティブ）
これらの環境では `make` が標準で入っているため追加ツールは不要です。
```bash
make            # このホスト向けに ./w7telnet-listen を生成
```

### Windows バイナリ（macOS/Linux から mingw-w64 でクロスコンパイル）
```bash
make win-x64    # -> w7telnet-listen-x64.exe
make win-x86    # -> w7telnet-listen-x86.exe
make win        # 両方
```

### macOS でのビルド メモ
Apple Silicon (arm64) の macOS で、ビルドと localhost での簡単な動作確認まで行いました。Intel Mac では未確認です。
- `make` には Xcode のコマンドラインツールが必要です (`xcode-select --install`)。
- `make win-x64` / `make win-x86` には mingw-w64 が必要です (`brew install mingw-w64`)。
- 動作確認は、別のターミナルで `./w7telnet-listen 2323 127.0.0.1` を起動してから行います。入力を流し込むと接続が先に閉じてしまうため、少し待ちを入れてください。
  ```bash
  { sleep 1; printf 'echo hi\r\n'; sleep 1; printf 'exit\r\n'; sleep 1; } | nc 127.0.0.1 2323
  ```
- これらの macOS でクロスコンパイルした exe は、その Mac 上からは実行していません。Windows ビルド自体は、別途 実機の Windows 7 Ultimate で動作確認済みです（gcc/mingw・MSVC の両方）。

### Windows 上で、make なしでビルド
2 通りあり、どちらも素の Windows 7 で動く自己完結の exe を生成します。32bit /
64bit いずれも依存 DLL は `KERNEL32.dll` と `WS2_32.dll` のみであることを確認済み
です。各スクリプトはアーキテクチャ引数（`x64`＝既定 / `x86`）を取り、
`w7telnet-listen-x64.exe` / `w7telnet-listen-x86.exe` を生成します。リポジトリ
直下から実行します。

- **MSYS2 / mingw-w64 gcc**（CRT 非依存ビルド）:
  ```bat
  src\windows\build.bat          :: 64bit -> w7telnet-listen-x64.exe
  src\windows\build.bat x86      :: 32bit -> w7telnet-listen-x86.exe
  ```
  対応するツールチェインを MSYS2 に入れておく必要があります:
  `pacman -S mingw-w64-x86_64-gcc`（x64）/ `pacman -S mingw-w64-i686-gcc`（x86）。
  MSYS2 が `C:\msys64` 以外にある場合は `MSYS2_ROOT` を設定してください。
- **MSVC**（無料の Visual Studio / Build Tools の「C++ によるデスクトップ開発」）:
  ```bat
  src\windows\build-msvc.bat      :: 64bit -> w7telnet-listen-x64.exe
  src\windows\build-msvc.bat x86  :: 32bit -> w7telnet-listen-x86.exe
  ```
  CRT を静的リンク（`/MT`）するため、VC ランタイムや UCRT の DLL は不要です。
  スクリプトが VS 2022 を自動検出します。もしくは対応する「Native Tools Command
  Prompt for VS」から実行してください。

### このホストでビルドできるものすべて
```bash
make all
```

## 使い方

### Windows
管理者権限のコマンドプロンプトで実行します（ポート 23 の待ち受けに必要）。
```
w7telnet-listen-x64.exe                 :: 0.0.0.0:23 で待ち受け
w7telnet-listen-x64.exe 2323            :: ポート 2323
w7telnet-listen-x64.exe 23 127.0.0.1    :: localhost のみ（一番安全）
```
Windows ファイアウォールで受信許可が必要な場合があります。
```
netsh advfirewall firewall add rule name="w7telnet" dir=in action=allow protocol=TCP localport=23
```

### macOS / Linux
ポート 23 の待ち受けには root が必要です。それ以外は非特権ポートを使ってください。
```bash
sudo ./w7telnet-listen                  # 0.0.0.0:23
./w7telnet-listen 2323                  # 0.0.0.0:2323
./w7telnet-listen 2323 127.0.0.1        # localhost のみ（一番安全）
./w7telnet-listen -s /bin/bash 2323     # 使うシェルを明示指定
```
既定では接続ユーザーのログインシェルを使います（パスワードデータベース →
`$SHELL` → `/bin/sh` の順で解決）。`-s` で上書きできます。

## クライアント側からの接続
```
telnet <対象IP> 23
```
または PuTTY の Telnet モード。接続するとすぐシェルのプロンプトが出ます。
`exit` でセッション終了、listener 本体は Ctrl+C で停止します。

## 仕様メモ
- 接続ごとにスレッドを起こすので複数同時接続に対応。
- クライアントから来る telnet の IAC ネゴシエーション列は、シェルに渡す前に
  除去します（`src/core/telnet.c`）。
- 入力のローカルエコーはクライアント（Windows telnet / PuTTY）任せです。
- Windows ビルドは **C ランタイム(UCRT/msvcrt)非依存**で、依存 DLL は
  `KERNEL32.dll` と `WS2_32.dll` のみ。どちらも素の Windows 7 に標準で存在
  するため追加ランタイムは不要です。（Homebrew の mingw-w64 は UCRT 専用
  ビルドのため、通常リンクだと `api-ms-win-crt-*.dll` を要求して Windows 7 で
  動きません。それを避けるため Win32 API だけで実装しています。）POSIX ビルドに
  その制約はなく、通常の C ライブラリを使います。

依存 DLL の確認:
```bash
x86_64-w64-mingw32-objdump -p w7telnet-listen-x64.exe | grep "DLL Name"
```

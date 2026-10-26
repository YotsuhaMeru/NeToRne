# NeToRne

Sony torne CECH-ZD1J を Windows の TVTest、または Linux の Mirakurun から利用するためのソースコードと設定例です。Windows は C/C++ 製 BonDriver、Linux は Rust 製受信機を使います。

## 収録内容

| ディレクトリ | 内容 |
| --- | --- |
| [bondriver/](bondriver/README.md) | Windows x64 用 BonDriver、ビルド・テスト手順 |
| [linux/](linux/README.md) | Rust 受信機、Mirakurun・udev 設定例 |
| nixos/ | Nix パッケージと NixOS モジュール |
| [firmware/](firmware/README.md) | 差分パッチ、適用ツール、開発用 C/Rust ソース |
| windows/ | CECH-ZD1J 用 Zadig 設定例 |
| tests/ | ネイティブ処理とソース配布物の検査コード |

## 利用前の準備

CECH-ZD1J、アンテナ、通常受信では B-CAS カードが必要です。次のファイルは利用者が別途用意し、プロジェクト直下を基準に配置してください。受信機は対応ファイルのハッシュを検証します。

```text
BCJB95006/USRDIR/mb86h47_aac_idle.bin
BCJB95006/USRDIR/mb86h47_aac_trc.bin
research/decrypted/PvrRecSvr.prx
research/decrypted/libM2tsAes.elf
```

元ファームウェア、改変済みファームウェア本体、復号済みモジュール、SPI ダンプ、TVTest 本体、コンパイラ、受信データは同梱しません。配置先とローカルのログ・解析資料・旧配布物は `.gitignore` で公開対象から除外しています。

## Windows / TVTest

Windows x64 と Zig 0.15.2 を用意し、プロジェクト直下で実行します。

```powershell
./bondriver/build.ps1 -ZigPath C:/tools/zig/zig.exe
```

`bondriver/dist/` の DLL と INI を、終了した 64bit TVTest のフォルダへコピーします。コピー先 INI の `Root` をプロジェクトの絶対パスへ変更し、対象 USB 機器を WinUSB へ設定してください。詳細は [Windows の利用方法](bondriver/README.md) を参照してください。

## Linux / Mirakurun

Rust 1.85 以降、C コンパイラ、pkg-config、libusb の開発用ファイルを用意します。

```sh
cargo build --release --locked --manifest-path linux/receiver/Cargo.toml
sh linux/torne-recv --channel 20 --check
sh linux/torne-recv --channel 20 --seconds 10 > sample.ts
```

物理チャンネルは受信地域に合わせて指定してください。USB 権限、Mirakurun、NixOS の設定は [Linux の利用方法](linux/README.md) に記載しています。

## GitHub Actions の自動ビルド

GitHub へ push するか PR を作成・更新すると、`Build binaries` が以下の 2 種類をビルドします。Actions タブから `Run workflow` で手動実行もできます。完了した実行の Artifacts からダウンロードしてください。保存期間は 30 日です。

| Artifact | 内容 |
| --- | --- |
| `BonDriver_Torne-windows-x64` | 通常版 DLL、INI、第三者ライセンス表記、SHA-256 一覧 |
| `torne-recv-linux-x86_64` | `torne-recv` 単体の tar.gz、SHA-256 一覧 |

`v` で始まるタグ（例: `v0.1.0`）を push すると、両方のビルド・テストが成功した後に GitHub Release を作成し、`BonDriver_Torne-windows-x64.zip`、`torne-recv-linux-x86_64.tar.gz`、両アーカイブの `SHA256SUMS` を添付します。通常のブランチ push・PR では Artifacts の保存まで行います。同じタグの実行を再実行すると、既存 Release の同名ファイルを更新します。

変更をコミット・push した後、リリースするコミットにタグを付けて送信します。

```sh
git tag v0.1.0
git push origin v0.1.0
```

Windows は Zig 0.15.2、Linux は Ubuntu 22.04 と stable Rust を使います。Linux の tar.gz は実行権限を保持し、`tar -xzf torne-recv-linux-x86_64.tar.gz` で展開できます。実行先には glibc 2.35 以降と libusb 1.0 の共有ライブラリが必要です（Debian / Ubuntu では `libusb-1.0-0`）。資材の場所は `--root` で指定してください。BonDriver の INI は配置後に `Root` を資材の絶対パスへ変更してください。

ビルドと USB を開かないテストを実行し、BonDriver と Linux 受信機だけを成果物にします。ファームウェアや復号済みモジュールは不要で、成果物にも含みません。実行時には引き続き [利用前の準備](#利用前の準備) のファイルが必要です。

## ファームウェアのパッチ

Python 3.10 以降の標準ライブラリだけで元ファイルへ差分を適用できます。SPI ダンプや追加 Python パッケージは不要です。

```sh
python firmware/tools/apply-patch.py --variant psi-passthrough
```

入力と出力の SHA-256 を照合し、`firmware/build/` へ新規保存します。元ファイルは上書きしません。INI や受信コマンドから生成物を明示指定してください。方式・対応範囲は [パッチの説明](firmware/README.md) を参照してください。

## ソース配布アーカイブ

```sh
python scripts/package-source.py
python -m unittest discover -s tests -p test_source_distribution.py
```

`dist/tvtuner-source.zip` と SHA-256 ファイルを生成します。Git が無視していないファイルだけを収録し、元ファームウェアやビルド生成物の混入を検査します。Git リポジトリで実行してください。コミットやリモートへの送信は行いません。

Git 登録前に `git status --short --untracked-files=all` と `git add --dry-run .` で対象を確認できます。既存の `archive/`、`rel/`、`diagnostics/`、`research/`、TVTest フォルダはローカル保管用です。

## 制約とライセンス

ファームウェア全体の置換、全多重 TS の完全性、すべての TVTest 機能や長時間運転は保証しません。Linux は純正と PAT/PMT 保持版に対応します。スクランブル保持版は Windows 用の実験的構成です。機器への転送は RAM ロードで、SPI Flash 書き換え用イメージは含みません。

第三者由来コードの表記は [bondriver/THIRD_PARTY_LICENSES.txt](bondriver/THIRD_PARTY_LICENSES.txt) を参照してください。プロジェクト全体の新たなライセンス指定は行っていません。

# NeToRne

CECH-ZD1J を Windows の TVTest、または Linux の Mirakurun から利用するためのソースコードと設定例です。Windows では C/C++ 製の BonDriver、Linux では Rust 製の受信プログラムを使います。

## ダウンロード

[Releases](https://github.com/YotsuhaMeru/NeToRne/releases) から、利用する OS 向けのファイルをダウンロードしてください。配布バイナリを使う場合、Zig や Rust によるビルドは不要です。

| ファイル | 内容 |
| --- | --- |
| `BonDriver_Torne-windows-x64.zip` | Windows x64 用 BonDriver |
| `torne-recv-linux-x86_64.tar.gz` | Linux x86_64 用の受信バイナリ `torne-recv` |
| `SHA256SUMS` | 上記アーカイブの SHA-256 一覧 |

Mirakurun の補助スクリプト・設定例やファームウェアのパッチ適用ツールは、同じ Release の `Source code` を展開して利用してください。バイナリのアーカイブには含まれません。

## 収録内容

| ディレクトリ | 内容 |
| --- | --- |
| [bondriver/](bondriver/README.md) | Windows x64 用 BonDriver、ビルド・テスト手順 |
| [linux/](linux/README.md) | Rust 受信機、Mirakurun・udev 設定例 |
| `nixos/` | Nix パッケージと NixOS モジュール |
| [firmware/](firmware/README.md) | ファームウェアに適用できる差分パッチ、適用ツール、開発用 C/Rust ソース |
| `windows/` | CECH-ZD1J 用 Zadig 設定例 |
| `tests/` | ネイティブ処理とソース配布物の検査コード |

# 使い方

## 準備

CECH-ZD1J とアンテナが必要です。通常受信では B-CAS カードも必要です。次のファイルはディスク等から別途用意し、保存先のフォルダ内に以下の構成で配置してください。Windows では `C:/tvtuner`、Linux では `/opt/tvtuner` を例に説明します。実行時に各ファイルのハッシュを検証します。
一部のファイルは暗号化されているため、復号化してから配置する必要があります。

```text
BCJB95006/USRDIR/mb86h47_aac_idle.bin
BCJB95006/USRDIR/mb86h47_aac_trc.bin
research/decrypted/PvrRecSvr.prx
research/decrypted/libM2tsAes.elf
```

オリジナルのファームウェアなどは付属していません。

ファームウェアやモジュールを置くフォルダの絶対パスを、BonDriver では INI の `Root` に、Linux の受信プログラムでは `--root` に指定します。ソースコードを展開したフォルダに、ファームウェアやモジュールを置くこともできます。

## Windows / TVTest

Windows x64 と 64bit TVTest を用意し、ダウンロードした `BonDriver_Torne-windows-x64.zip` を展開します。

TVTest を終了してから、`BonDriver_Torne.dll` と `BonDriver_Torne.ini` を TVTest のフォルダへコピーし、INI の `Root` にファームウェアやモジュールを置くフォルダの絶対パスを指定してください。

```ini
[Torne]
Root=C:/tvtuner
```

INI のほかの項目はそのまま残します。対象 USB 機器を Zadig で WinUSB に設定し、TVTest で BonDriver を選択してください。詳細は [Windows の利用方法](bondriver/README.md) を参照してください。

## Linux / Mirakurun

Linux x86_64 で glibc 2.35 以降と libusb 1.0 の共有ライブラリが必要です。Debian / Ubuntu では `sudo apt-get install libusb-1.0-0` でライブラリを導入できます。

ダウンロードしたアーカイブを展開し、ファームウェアやモジュールを置くフォルダを指定して実行します。

```sh
tar -xzf torne-recv-linux-x86_64.tar.gz
./torne-recv --root /opt/tvtuner --channel 20 --check
./torne-recv --root /opt/tvtuner --channel 20 --seconds 10 > sample.ts
```

`--check` は USB 機器に接続せずに、ファームウェアやモジュール、設定を確認します。受信前に USB 権限を設定し、物理チャンネルは受信地域に合わせて指定してください。バイナリのインストール、USB 権限、Mirakurun、NixOS の設定は [Linux の利用方法](linux/README.md) に記載しています。

## ファームウェアのパッチ

ファームウェアにパッチを適用すると、データ放送を含むTSや、デスクランブルしていない生のTSファイルを取得できるようになります。

Python 3.10 以降の標準ライブラリだけで元ファイルへ差分を適用できます。SPI ダンプや追加 Python パッケージは不要です。

同じ Release の `Source code` を展開し、そのフォルダに [準備](#準備) で挙げたファイルを配置してください。そのフォルダをファームウェアやモジュールの保存先として、以下を実行します。

```sh
python firmware/tools/apply-patch.py --variant psi-passthrough
```

入力と出力の SHA-256 を照合し、`firmware/build/` へ新規保存します。元ファイルは上書きしません。生成したファイルのパスを、INI や受信コマンドの引数に指定してください。方式・対応範囲は [パッチの説明](firmware/README.md) を参照してください。

# 開発者向け

## GitHub Actions の自動ビルド

通常の push・PR では、BonDriver の C/C++ ソース・ヘッダー・PowerShell スクリプト・INI・第三者ライセンス表記、Linux 受信機の `src/`・`Cargo.toml`・`Cargo.lock`、またはビルドワークフローが変更されると、`Build binaries` が以下の 2 種類をビルドします。README やドキュメントだけの変更では実行しません。

リリース用の `v*` タグ push と Actions タブの `Run workflow` による手動実行では、変更ファイルに関係なくビルドします。完了した実行の Artifacts からダウンロードしてください。保存期間は 30 日です。

| Artifact | 内容 |
| --- | --- |
| `BonDriver_Torne-windows-x64` | 通常版 DLL、INI、第三者ライセンス表記、SHA-256 一覧 |
| `torne-recv-linux-x86_64` | `torne-recv` 単体の tar.gz、SHA-256 一覧 |

`v` で始まるタグ（例: `v0.1.0`）を push すると、両方のビルド・テストが成功した後に GitHub Release を作成し、`BonDriver_Torne-windows-x64.zip`、`torne-recv-linux-x86_64.tar.gz`、両アーカイブの `SHA256SUMS` を添付します。通常のブランチ push・PR では Artifacts の保存まで行います。同じタグでワークフローを再実行すると、既存 Release の同名ファイルを更新します。

変更をコミット・push した後、リリースするコミットにタグを付けて送信します。

```sh
git tag v0.1.0
git push origin v0.1.0
```

Windows は Zig 0.15.2、Linux は Ubuntu 22.04 と stable Rust を使います。ローカルでビルドする手順は [BonDriver](bondriver/README.md#ソースからビルドする場合) と [Linux 受信機](linux/README.md#ソースからビルドする場合) の README を参照してください。

ビルドと USB を開かないテストを実行し、BonDriver と Linux 受信機だけを成果物にします。ビルドやテストにはファームウェアや復号済みモジュールは不要で、成果物にも含みません。受信プログラムの実行時には [準備](#準備) で挙げたファイルが必要です。

## ソース配布アーカイブ

```sh
python scripts/package-source.py
python -m unittest discover -s tests -p test_source_distribution.py
```

`dist/tvtuner-source.zip` と SHA-256 ファイルを生成します。Git が無視していないファイルだけを収録し、元ファームウェアやビルド生成物の混入を検査します。Git リポジトリで実行してください。コミットやリモートへの送信は行いません。

Git 登録前に `git status --short --untracked-files=all` と `git add --dry-run .` で対象を確認できます。既存の `archive/`、`rel/`、`diagnostics/`、`research/`、TVTest フォルダはローカル保管用です。

## 制約とライセンス

ファームウェア全体の置換や全多重 TS の完全性、すべての TVTest 機能の動作や長時間運転は保証しません。Linux は純正と PAT/PMT 保持版に対応します。スクランブル保持版は Windows 用の実験的構成です。ファームウェアは機器の RAM に読み込みます。SPI Flash 書き換え用イメージは含みません。

第三者由来コードの表記は [bondriver/THIRD_PARTY_LICENSES.txt](bondriver/THIRD_PARTY_LICENSES.txt) を参照してください。プロジェクト全体のライセンスは新たに指定していません。

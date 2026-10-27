# NeToRne

CECH-ZD1J を Windows の TVTest、または Linux の Mirakurun から利用するためのソフトです。

## ダウンロード

[Releases](https://github.com/YotsuhaMeru/NeToRne/releases) から、それぞれの OS 向けのファイルをダウンロードしてください。

Mirakurun の補助スクリプト・設定例やファームウェアのパッチ適用ツールは、このrepoをcloneして利用してください。リリースには含まれません。

## 収録内容

| ディレクトリ | 内容 |
| --- | --- |
| [bondriver/](bondriver/README.md) | Windows x64 用 BonDriver、ビルド・テスト手順 |
| [linux/](linux/README.md) | Linux 受信ソフト、Mirakurun・udev 設定例 |
| `nixos/` | Nix パッケージと NixOS モジュール |
| [firmware/](firmware/README.md) | ファームウェアに適用できる差分パッチ、適用ツール、開発用 C/Rust ソース |
| `windows/` | CECH-ZD1J 用 Zadig 設定例 |
| `tests/` | ネイティブ処理とソース配布物の検査コード |

# 使い方

## 準備

CECH-ZD1J とアンテナ、torneのディスクダンプが必要です。また、通常受信では B-CAS カードも必要です。

次のファイルはtorneのディスク等から別途用意し、保存先のフォルダ内に以下の構成で配置してください。

Windows では `C:/tvtuner`、Linux では `/opt/tvtuner` を例に説明します。実行時に各ファイルのハッシュを検証します。
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

このrepoをcloneし、フォルダ内に [準備](#準備) で挙げたファイルを配置してください。そのフォルダをファームウェアやモジュールの保存先として、以下を実行します。

```sh
python firmware/tools/apply-patch.py --variant psi-passthrough
```

入力と出力の SHA-256 を照合し、`firmware/build/` へ新規保存します。元ファイルは上書きしません。生成したファイルのパスを、INI や受信コマンドの引数に指定してください。方式・対応範囲は [パッチの説明](firmware/README.md) を参照してください。

## 制約とライセンス

ファームウェア全体の置換や全多重 TS の完全性、すべての TVTest 機能の動作や長時間運転は保証しません。Linux は純正と PAT/PMT 保持版に対応します。スクランブル保持版は Windows 用の実験的構成です。ファームウェアは機器の RAM に読み込みます。SPI Flash 書き換え用イメージは含みません。

第三者由来コードの表記は [bondriver/THIRD_PARTY_LICENSES.txt](bondriver/THIRD_PARTY_LICENSES.txt) を参照してください。プロジェクト全体のライセンスは新たに指定していません。

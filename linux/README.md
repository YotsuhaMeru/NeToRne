# Linux / Mirakurun

Rust 製の `torne-recv` は libusb を使い、認証・選局・TS 出力を行います。[Releases](https://github.com/YotsuhaMeru/NeToRne/releases) のバイナリを利用でき、通常の受信に Rust / Cargo、C コンパイラ、Python は不要です。

## ダウンロードとインストール

Linux x86_64、glibc 2.35 以降と libusb 1.0 の共有ライブラリが必要です。Debian / Ubuntu では次のようにライブラリを導入します。

```sh
sudo apt-get update
sudo apt-get install libusb-1.0-0
```

[Releases](https://github.com/YotsuhaMeru/NeToRne/releases) から `torne-recv-linux-x86_64.tar.gz` をダウンロードし、そのファイルがあるディレクトリで実行します。

```sh
tar -xzf torne-recv-linux-x86_64.tar.gz
sudo install -m 0755 torne-recv /usr/local/bin/torne-recv
```

アーカイブには実行バイナリ `torne-recv` だけが入っています。元ファームウェアとモジュールの計 4 ファイルは [ルート README](../README.md#準備) に従って別途用意してください。以下の手順では、ファームウェアやモジュールを `/opt/tvtuner` に保存するものとします。

## 単独受信

ファームウェアやモジュールを置くフォルダを `--root` で指定します。まず `--check` で確認し、後述の USB 権限を設定してから受信してください。

```sh
torne-recv --root /opt/tvtuner --channel 20 --check
torne-recv --root /opt/tvtuner --channel 20 --seconds 10 > sample.ts
```

`--check` は USB 機器に接続せずに、ファイルと設定を検査します。物理チャンネルは地域に合わせて指定してください。188 バイト単位の TS パケットは標準出力へ、診断ログは標準エラーと状態ディレクトリへ出力します。`--seconds 0` を指定すると、停止するまで受信します。

`--root` を省略すると、ファームウェアやモジュールを環境変数 `TORNE_ROOT` で指定したフォルダから読み込みます。`TORNE_ROOT` も未設定なら、カレントディレクトリを使います。

## USB 権限と Mirakurun

同じ Release の `Source code` をダウンロードして展開し、その中の `linux/` ディレクトリを `/opt/tvtuner/linux/` に配置します。受信用バイナリのアーカイブには、これらの補助スクリプトや設定例は含まれません。

udev ルールを導入し、受信ユーザーを `video` グループへ追加します。以下はログイン中のユーザーで受信する場合の例です。Mirakurun を使う場合は、その実行ユーザーも `video` グループへ追加してください。

```sh
sudo install -m 0644 /opt/tvtuner/linux/torne-receiver.rules /etc/udev/rules.d/torne-receiver.rules
sudo udevadm control --reload-rules
sudo usermod -aG video "$USER"
sudo chmod +x /opt/tvtuner/linux/torne-recv /opt/tvtuner/linux/torne-recv-watchdog
```

グループ変更を反映するため、ログインし直すか、対象サービスを再起動してから USB を再接続してください。ファームウェアやモジュールを置くフォルダには受信ユーザーが読み取れる権限を設定します。

Mirakurun の `command` には `torne-recv-watchdog` を指定します。補助スクリプトは既定ではソースからビルドしたバイナリを使うため、配布バイナリを使う場合は、`TORNE_BINARY` にそのパスを、`TORNE_ROOT` にファームウェアやモジュールの保存先を指定します。`tuners.yml` を参考に、Mirakurun の設定を次のように調整してください。

```yaml
- name: Sony torne CECH-ZD1J
  types:
    - GR
  command: /usr/bin/env TORNE_BINARY=/usr/local/bin/torne-recv TORNE_ROOT=/opt/tvtuner /opt/tvtuner/linux/torne-recv-watchdog --channel <channel> --state-dir /var/lib/torne-receiver
  isDisabled: false
```

`/var/lib/torne-receiver` は Mirakurun の実行ユーザーが書き込めるディレクトリとして用意してください。`channels-nagoya.yml` は受信地域に合わせて置き換えます。watchdog は受信プログラムがエラー終了すると、そのプログラムを同じチャンネルで再起動します。待ち時間の既定値は 3 秒です。環境変数 `TORNE_RESTART_DELAY` で 1〜60 秒に変更できます。

直接起動した場合、状態は `$XDG_STATE_HOME/torne` または `~/.local/state/torne` に保存します。NixOS では `/var/lib/torne-receiver/` に保存します。`--state-dir` で変更できます。同じ機器を使うプロセスは同じ状態ディレクトリを指定してください。

## NixOS

NixOS モジュールを使う場合は、Nix でソースからビルドします。Release の `Source code` または Git のチェックアウトを `/opt/tvtuner` に配置し、`configuration.nix` に追加します。

```nix
{
  imports = [ /opt/tvtuner/nixos/torne-mirakurun.nix ];
  services.torneReceiver.enable = true;
  services.torneReceiver.root = "/opt/tvtuner";
  services.torneReceiver.channels = [
    { name = "NHK総合・名古屋"; physical = 20; }
    { name = "CBCテレビ"; physical = 18; }
  ];
}
```

`sudo nixos-rebuild switch` 後に USB を再接続します。`sudo -u mirakurun torne-recv --channel 20 --check` でファームウェアやモジュールを確認してください。NixOS モジュールは watchdog を使用し、Rust バイナリだけをビルドして Nix store に保存します。元ファームウェアとモジュールは store にコピーしません。

```sh
nix-build nixos
./result/bin/torne-recv --root /opt/tvtuner --channel 20 --check
nix-shell nixos/shell.nix
```

## PAT/PMT 保持版

同じ Release の `Source code` を展開し、[パッチ適用手順](../firmware/README.md) に従って生成します。以下はソースコードとファームウェア・モジュールを `/opt/tvtuner` に配置した場合の例です。パッチ適用時には Python 3.10 以降が必要です。

```sh
cd /opt/tvtuner
python firmware/tools/apply-patch.py --variant psi-passthrough
torne-recv --root /opt/tvtuner --operational-firmware firmware/build/mb86h47_aac_trc-psi-passthrough.bin --channel 20 --seconds 10 > sample.ts
```

NixOS では `services.torneReceiver.operationalFirmware = "firmware/build/mb86h47_aac_trc-psi-passthrough.bin";` を追加します。省略すると純正ファームウェアを使います。Linux はスクランブル保持版・その他の C 置換候補に対応していません。

自動復帰ではプロセスを再起動し、RAM ファームウェアを再初期化します。USB 電源の遮断・再列挙は行いません。全多重 TS の完全性と実環境の長時間安定性は別途検証が必要です。

## ソースからビルドする場合

ソースを変更する場合や、配布バイナリの対象外の環境では、Rust 1.85 以降と Cargo、C コンパイラ、pkg-config、libusb の開発用ファイルを用意します。Debian / Ubuntu のパッケージ名は `build-essential pkg-config libusb-1.0-0-dev` です。プロジェクト直下で実行してください。

```sh
cargo build --release --locked --manifest-path linux/receiver/Cargo.toml
cargo test --locked --manifest-path linux/receiver/Cargo.toml
```

バイナリは `linux/receiver/target/release/torne-recv` に生成されます。ソースのフォルダにファームウェアやモジュールを置く場合は、`sh linux/torne-recv --channel 20 --check` で付属のラッパーから起動できます。ビルド時に元ファームウェアやモジュールは不要です。

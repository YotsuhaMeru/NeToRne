# Linux / Mirakurun

Rust 製の `torne-recv` は libusb を使い、認証・選局・TS 出力を行います。Python は受信に不要です。必要な元ファームウェアとモジュール 4 ファイルは [ルート README](../README.md#利用前の準備) の位置へ用意してください。

## ビルドと単独受信

Rust 1.85 以降 / Cargo、C コンパイラ、pkg-config、libusb の開発用ファイルが必要です。Debian / Ubuntu のパッケージ名は `build-essential pkg-config libusb-1.0-0-dev` です。

```sh
cargo build --release --locked --manifest-path linux/receiver/Cargo.toml
cargo test --locked --manifest-path linux/receiver/Cargo.toml
sh linux/torne-recv --channel 20 --check
sh linux/torne-recv --channel 20 --seconds 10 > sample.ts
```

`--check` は USB を開かずにファイルと設定を検査します。物理チャンネルは地域に合わせて指定してください。標準出力は 188 バイト TS、診断ログは標準エラーと状態ディレクトリへ出力します。`--seconds 0` は停止まで受信します。

バイナリは `linux/receiver/target/release/torne-recv` です。別の場所から使う場合は `--root /path/to/tvtuner` で資材の位置を指定します。ラッパーの環境変数は `TORNE_BINARY` と `TORNE_ROOT` です。実行時に Rust / Cargo は不要ですが、通常ビルドでは libusb の共有ライブラリが必要です。

## USB 権限と Mirakurun

`torne-receiver.rules` を `/etc/udev/rules.d/` に配置してルールを再読み込みし、受信ユーザーを `video` グループへ追加して USB を再接続してください。

```sh
chmod +x linux/torne-recv linux/torne-recv-watchdog
```

Mirakurun の tuner command には `torne-recv-watchdog` を指定します。`tuners.yml` の配置先を調整し、`channels-nagoya.yml` は受信地域に合わせて置き換えてください。watchdog は受信機がエラー終了すると同じチャンネルで再試行します。待ち時間は既定 3 秒、`TORNE_RESTART_DELAY` で 1〜60 秒に変更できます。

状態は直接起動時に `$XDG_STATE_HOME/torne` または `~/.local/state/torne`、NixOS では `/var/lib/torne-receiver/` に保存します。`--state-dir` で変更できます。同じ機器を使うプロセスは同じ状態ディレクトリを指定してください。

## NixOS

プロジェクトを `/opt/tvtuner` に配置し、`configuration.nix` に追加します。

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

`sudo nixos-rebuild switch` 後に USB を再接続します。`sudo -u mirakurun torne-recv --channel 20 --check` で資材を確認してください。モジュールは watchdog を使用し、Rust バイナリだけを Nix store にビルドします。元ファームウェアとモジュールは store にコピーしません。

```sh
nix-build nixos
./result/bin/torne-recv --root /opt/tvtuner --channel 20 --check
nix-shell nixos/shell.nix
```

## PAT/PMT 保持版

```sh
python firmware/tools/apply-patch.py --variant psi-passthrough
sh linux/torne-recv --operational-firmware firmware/build/mb86h47_aac_trc-psi-passthrough.bin --channel 20 --seconds 10 > sample.ts
```

NixOS では `services.torneReceiver.operationalFirmware = "firmware/build/mb86h47_aac_trc-psi-passthrough.bin";` を追加します。省略時は純正です。Linux はスクランブル保持版・その他の C 置換候補に対応していません。

自動復帰はプロセス再起動と RAM ファームウェア再初期化で、USB 電源の遮断・再列挙は行いません。全多重 TS の完全性と実環境の長時間安定性は別途検証が必要です。

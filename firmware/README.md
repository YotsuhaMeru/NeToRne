# ファームウェア差分パッチと開発用ソース

元ファームウェアや改変済みの全体イメージを配布せず、対応する入力から対象候補を再現する差分を `patches/` に収録しています。入力は torne 1.10 の `mb86h47_aac_trc.bin`、SHA-256 は `653f2377c838a4e7276b447fd992084d5e6e84a90ac4d389851f82309b662a04` です。

## 適用

プロジェクト直下で Python 3.10 以降を使います。外部パッケージ・復号鍵・SPI ダンプは不要です。

```sh
python firmware/tools/apply-patch.py --list
python firmware/tools/apply-patch.py --variant psi-passthrough
# 入力と出力を明示する場合
python firmware/tools/apply-patch.py --variant psi-passthrough --input /path/to/mb86h47_aac_trc.bin --output /path/to/patched.bin
```

既定入力は `BCJB95006/USRDIR/mb86h47_aac_trc.bin`、出力は `firmware/build/` です。入力・出力の SHA-256、サイズ、差分領域を照合します。同じ入力への書き込みや、異なる内容の既存出力の上書きを拒否します。同じ出力がある場合は照合だけ行います。USB アクセスや自動ロードは行いません。

| variant | 用途 | 対応 |
| --- | --- | --- |
| `preserve-es` | ES/PMT 設定 getter の変更 | Windows・互換性試験用 |
| `psi-passthrough` | PAT/PMT 保持。AV 再構成などは残る | Windows / Linux |
| `raw-af` | C 置換を含む TS 転送と AF 保持候補 | Windows・実験用 |
| `raw-scrambled` | B-CAS 起動を省き、放送スクランブルを保持 | Windows・実験用 |

Linux は純正と `psi-passthrough` のハッシュだけを受け付けます。実験候補で全多重の完全性を保証するものではありません。

Windows では INI の `OperationalFirmware` に生成先を設定します。Linux では次のように指定します。

```sh
sh linux/torne-recv --operational-firmware firmware/build/mb86h47_aac_trc-psi-passthrough.bin --channel 20 --check
```

受信機が RAM へロードするためのイメージです。SPI Flash 書き換え用ではありません。

## パッチ形式

JSON は `torne-xor-v1` 形式です。元ファイル・生成先の SHA-256、サイズと、変更箇所の offset / XOR 差分を記録します。変更のない領域、元ファイル全体、SPI ダンプや鍵は収録しません。各 variant は純正 trc に直接適用し、他のパッチと重ねません。

## 開発用ソース

`c/` に C99 / FR アセンブリ / リンカスクリプト、`src/` に `no_std` Rust コア、`tests/` と `examples/` に検査コードを残しています。開発用コア単体のビルド結果はロード可能な完全ファームウェアではありません。idle/trc 全体の置換と board adapter は未完成です。

```sh
cargo test --locked --manifest-path firmware/Cargo.toml
```

Windows のホスト C テストと FR30 コアのコンパイル:

```powershell
./firmware/test-c.ps1 -Compiler C:/tools/zig/zig.exe
# Python 3.14 以降。固定 URL のツールチェーンを取得し、ハッシュを照合します。
python firmware/tools/prepare-fr30.py
./firmware/build-fr30.ps1
```

過去の実機ログ、逆コンパイル出力、ダンプを前提にする解析・候補生成スクリプトはローカル保管とし、公開対象から除外しました。配布した差分の再現には上記の適用ツールを使ってください。

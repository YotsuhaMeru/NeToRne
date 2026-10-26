# Windows / BonDriver

Windows x64 の TVTest 向け BonDriver です。対象は Sony torne CECH-ZD1J です。

## 準備とビルド

64bit TVTest と Zig 0.15.2 を別途用意します。必要な元ファームウェアとモジュールの配置は [ルート README](../README.md#利用前の準備) を参照してください。

プロジェクト直下で実行します。Zig が PATH にあれば `-ZigPath` は省略できます。環境変数 `TORNE_ZIG` でも指定できます。

```powershell
./bondriver/build.ps1 -ZigPath C:/tools/zig/zig.exe
# USB 要求などの追加ログが必要な場合
./bondriver/build.ps1 -ZigPath C:/tools/zig/zig.exe -Diagnostics
```

通常版は `bondriver/dist/`、診断版は `bondriver/dist/diagnostics/` に DLL と INI を生成します。

## TVTest への配置

1. TVTest を終了します。
2. 生成された `BonDriver_Torne.dll` と `BonDriver_Torne.ini` を TVTest のフォルダへコピーします。
3. コピー先 INI の `Root` をプロジェクトの絶対パスへ変更します（例: `Root=C:/tvtuner`）。既存 INI はビルドで上書きされません。
4. Zadig で VID `054C` / PID `04B2` の対象機器を WinUSB に設定します。`windows/torne-zadig.cfg` は機器選択の設定例です。
5. USB、アンテナ、B-CAS カードを接続し、TVTest で `BonDriver_Torne.dll` を選択します。
6. チャンネルスキャンで「サービスを検索する」と「信号レベルを無視する」を有効にします。

TVTest フォルダへ直接ビルドする場合は `-OutputDirectory C:/TVTest` を指定します。新規 INI の `Root` にはプロジェクトの絶対パスが入ります。

## パッチ済みファームウェア

[パッチ適用手順](../firmware/README.md) で生成してから INI に指定します。各パスは `Root` 基準です。`OperationalFirmware` が空欄なら純正 trc を使います。

```ini
[Torne]
Root=C:/tvtuner
FirmwareDirectory=BCJB95006\USRDIR
OperationalFirmware=firmware\build\mb86h47_aac_trc-psi-passthrough.bin
PvrModule=research\decrypted\PvrRecSvr.prx
MediaModule=research\decrypted\libM2tsAes.elf
```

スクランブル保持版は `OperationalFirmware=firmware\build\mb86h47_aac_trc-c-raw-scrambled.bin` を指定します。B-CAS 起動を省き、放送スクランブルを保持して出力します。PC 側の放送復号処理は含みません。

## 復帰動作と制約

RF ロック喪失や TS 停止が約 5 秒続く場合、USB 通信エラー、選局・Purge の失敗時に同じ物理チャンネルで再試行します。失敗した再試行の終了から約 3 秒後に次を試みます。DLL と同じ場所の `BonDriver_Torne-recovery.log` と診断情報を確認してください。機器の電源遮断や USB 再列挙は行いません。致命的状態 `ffff` からの復帰は未確認です。

全 TS の完全性、字幕・BML 表示、全機能と長時間安定性は未検証です。診断版は USB 要求などを追加ログへ保存します。生成物・ログは公開対象から除外されます。

## テスト

以下は USB を開きません。ネイティブテストにはローカルの 4 ファイルと `preserve-es` / `psi-passthrough` パッチ生成物が必要です。診断テストにもローカルの 4 ファイルが必要です。

```powershell
./bondriver/test-recovery.ps1 -ZigPath C:/tools/zig/zig.exe
./bondriver/test-diagnostics.ps1 -ZigPath C:/tools/zig/zig.exe
python firmware/tools/apply-patch.py --variant preserve-es
python firmware/tools/apply-patch.py --variant psi-passthrough
./bondriver/test-native.ps1 -ZigPath C:/tools/zig/zig.exe
```

`test-native.ps1 -Execute` は実機を使用します。事前に TVTest を終了し、通常版 DLL を `bondriver/dist/` にビルドしてください。試験は物理 20 / 22 チャンネルを使います。地域が異なる場合は試験ソースの調整が必要です。

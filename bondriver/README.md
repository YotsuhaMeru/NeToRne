# Windows / BonDriver

Windows x64 の TVTest 向け BonDriver です。対象は Sony torne CECH-ZD1J です。

## ダウンロードと準備

64bit TVTest を別途用意し、[Releases](https://github.com/YotsuhaMeru/NeToRne/releases) から `BonDriver_Torne-windows-x64.zip` をダウンロードして展開します。通常の利用に Zig やソースコードは不要です。

ZIP には `BonDriver_Torne.dll`、`BonDriver_Torne.ini`、`THIRD_PARTY_LICENSES.txt`、各ファイルの SHA-256 を記載した `SHA256SUMS` が入っています。

元ファームウェアや復号済みモジュールは含まれません。[ルート README](../README.md#準備) に従って別途用意してください。以下の手順では、ファームウェアやモジュールを `C:/tvtuner` に保存するものとします。

## TVTest への配置

1. TVTest を終了します。
2. 展開した `BonDriver_Torne.dll` と `BonDriver_Torne.ini` を TVTest のフォルダへコピーします。更新時は既存 INI を保持し、設定を引き継いでください。
3. コピー先 INI の `Root` をファームウェアやモジュールを置くフォルダの絶対パスへ変更します（例: `Root=C:/tvtuner`）。配布 INI の `Root=..\..` は配置先に合わせて変更する必要があります。
4. Zadig で VID `054C` / PID `04B2` の対象機器を WinUSB に設定します。同じ Release の `Source code` にある `windows/torne-zadig.cfg` は機器選択の設定例です。
5. USB、アンテナ、B-CAS カードを接続し、TVTest で `BonDriver_Torne.dll` を選択します。
6. チャンネルスキャンで「サービスを検索する」と「信号レベルを無視する」を有効にします。

純正ファームウェアを使う場合の INI は以下のとおりです。`Root` 以外のパスはファームウェアやモジュールを置くフォルダを基準に指定します。

```ini
[Torne]
Root=C:/tvtuner
FirmwareDirectory=BCJB95006\USRDIR
OperationalFirmware=
PvrModule=research\decrypted\PvrRecSvr.prx
MediaModule=research\decrypted\libM2tsAes.elf
```

## パッチ済みファームウェア

同じ Release の `Source code` を展開し、[パッチ適用手順](../firmware/README.md) で生成してから INI に指定します。ソースを展開したフォルダにファームウェアやモジュールを置くか、生成したファームウェアを既存の保存先フォルダ内の `firmware/build/` へコピーしてください。各パスは `Root` 基準です。`OperationalFirmware` が空欄なら純正の trc ファームウェアを使います。

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

RF ロック喪失や TS 停止が約 5 秒続いた場合、USB 通信エラーが起きた場合、選局・Purge に失敗した場合は、同じ物理チャンネルで再試行します。再試行に失敗すると、その終了から約 3 秒後にもう一度再試行します。DLL と同じ場所の `BonDriver_Torne-recovery.log` と診断情報を確認してください。機器の電源遮断や USB 再列挙は行いません。致命的状態 `ffff` からの復帰は未確認です。

全 TS の完全性、字幕・BML 表示、すべての機能の動作と長時間の安定性は未検証です。診断版は USB 要求などを追加ログへ保存します。生成物・ログは公開対象から除外されます。

## ソースからビルドする場合

開発や診断版の作成には、ソースコードと Zig 0.15.2 を用意します。以下はプロジェクト直下で実行します。Zig が PATH にあれば `-ZigPath` は省略できます。環境変数 `TORNE_ZIG` でも指定できます。

```powershell
./bondriver/build.ps1 -ZigPath C:/tools/zig/zig.exe
# USB 要求などの追加ログが必要な場合
./bondriver/build.ps1 -ZigPath C:/tools/zig/zig.exe -Diagnostics
```

通常版の DLL と INI は `bondriver/dist/` に、診断版は `bondriver/dist/diagnostics/` に生成されます。ビルド時に元ファームウェアやモジュールは不要です。

TVTest フォルダへ直接ビルドする場合は `-OutputDirectory C:/TVTest` を指定します。新規 INI の `Root` にはプロジェクトの絶対パスが入ります。既存 INI はビルドで上書きされません。

## 開発用テスト

以下のテストでは USB 機器に接続しません。ネイティブテストには、[準備](../README.md#準備) で挙げた 4 ファイルと `preserve-es` / `psi-passthrough` のパッチ生成物が必要です。診断テストにも同じ 4 ファイルが必要です。

```powershell
./bondriver/test-recovery.ps1 -ZigPath C:/tools/zig/zig.exe
./bondriver/test-diagnostics.ps1 -ZigPath C:/tools/zig/zig.exe
python firmware/tools/apply-patch.py --variant preserve-es
python firmware/tools/apply-patch.py --variant psi-passthrough
./bondriver/test-native.ps1 -ZigPath C:/tools/zig/zig.exe
```

`test-native.ps1 -Execute` は実機を使用します。事前に TVTest を終了し、通常版 DLL を `bondriver/dist/` にビルドしてください。試験は物理 20 / 22 チャンネルを使います。地域が異なる場合は試験ソースの調整が必要です。

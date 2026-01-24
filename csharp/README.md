# C# tools

## UdpPhotoReceiver

MATLAB の `matlab/udp_photo_receiver.m` 相当の UDP 受信・フレーム復元・深度(320×240, 8-bit)ヒートマップ表示を行う WinForms アプリです．

### 使い方

```powershell
cd csharp\UdpPhotoReceiver
dotnet run
```

- UDPポート: `9000`
- パケット先頭 24 bytes ヘッダーを想定(`magic=0x12345678`)
- `total_size`, `chunk_index`, `total_chunks`, `chunk_data_size` を使ってフレーム復元します

### メモ

- `chunk_index==0` を新フレーム開始として扱い，前フレームは未完でも表示します(MATLAB版の挙動に合わせています)．
- 10秒受信できないフレームは破棄します．

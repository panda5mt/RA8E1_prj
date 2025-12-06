# RA8E1 Real-time Image Transmission System

RA8E1マイコンを使用したリアルタイム画像伝送システム。カメラからキャプチャした画像をOctalRAMに保存し、UDP通信でMATLABに送信してリアルタイム表示するプロジェクト。

## システム概要

### ハードウェア構成
- **マイコン**: Renesas RA8E1 (R7FA8AFDCFB)
- **メモリ**: OctalRAM IS66WVO8M8DALL (64Mbit, 8MB)
- **カメラ**: YUV422(YUYV)形式対応カメラセンサー
- **通信**: Ethernet UDP (ポート9000)
- **解像度**: QVGA (320×240ピクセル)

### 主要機能
- CEUペリフェラルによるYUV422画像キャプチャ
- OctalRAMへの画像データ保存 (153,600バイト)
- UDP通信による512バイトチャンク分割送信
- MATLABでのリアルタイム画像受信・表示
- リトルエンディアン対応YUV422デコード

## プロジェクト構造

```
RA8E1_prj/
├── src/                    # ソースコード
│   ├── hal_entry.c        # メインエントリーポイント
│   ├── main_thread*_entry.c # FreeRTOSタスク
│   ├── cam.c              # カメラ制御
│   ├── hyperram_integ.c   # OctalRAM統合
│   └── usb_cdc.h          # USB CDC通信
├── matlab/                # MATLAB受信コード
│   ├── udp_photo_receiver.m    # メイン受信関数
│   ├── test_udp_simple.m      # UDP接続テスト
│   └── viewQVGA_YUV.m         # YUV参照デコーダー
├── ra_gen/                # FSP生成ファイル
├── ra_cfg/                # FSPコンフィギュレーション
└── cmake/                 # CMake設定
```

## 使用方法

### MATLAB受信側
```matlab
% UDP受信開始
udp_photo_receiver

% 受信完了後、QVGA (320x240) RGB画像が自動表示されます
```

### 通信プロトコル
- **送信間隔**: 20ms/パケット
- **チャンクサイズ**: 512バイト
- **総パケット数**: 300パケット
- **パケット構造**: 24バイトヘッダー + データ

##  Building via CLI:
Configure: ```cmake -DARM_TOOLCHAIN_PATH="/your/toolchain/path" -DCMAKE_TOOLCHAIN_FILE=cmake/gcc.cmake  -G Ninja -B build/Debug```

- Ex.: ```cmake -DARM_TOOLCHAIN_PATH="C:/LLVMEmbeddedToolchainForArm-17.0.1-Windows-x86_64/bin" -DCMAKE_TOOLCHAIN_FILE=cmake/llvm.cmake  -G Ninja -B build/Debug```
- Ex.: ```cmake -DARM_TOOLCHAIN_PATH="C:/LLVMEmbeddedToolchainForArm-17.0.1-Windows-x86_64/bin" -DCMAKE_TOOLCHAIN_FILE=cmake/llvm.cmake  -DCMAKE_BUILD_TYPE=Release -G Ninja -B build/Release```

Build: ```cmake --build build/Debug```


### Configure via Visual Studio Code
- Set ARM_LLVM_TOOLCHAIN_PATH as an environment variable before starting VS code or alternatively set ARM_TOOLCHAIN_PATH in .vscode/cmake-kits.json
- Select "ARM LLVM kit with toolchainFile" kit in VS Code status bar
- It is recommended to avoid spaces in the toolchain and project paths as they might be interpreted as delimiters by CMake and the other build tools.

Example:

```set ARM_LLVM_TOOLCHAIN_PATH=C:/LLVMEmbeddedToolchainForArm-17.0.1-Windows-x86_64/bin```
```cd "c:/Users/lynxe/Documents/GitHub/RA8E1_prj" && code .```

- Click build in VS Code status bar

## 技術仕様

### OctalRAM アドレス変換
Octal RAM特有のアドレスフォーマットに対応：
```c
uint32_t converted_addr = ((base_addr & 0xfffffff0) << 6) | (base_addr & 0x0f);
```

### YUV422 画像フォーマット
- **メモリレイアウト**: [V0 Y1 U0 Y0] (リトルエンディアン, 4バイト/2ピクセル)
- **色空間変換**: ITU-R BT.601標準
- **MATLAB デコード**: `dsp.UDPReceiver`使用

### パケット構造
```c
typedef struct {
    uint32_t magic_number;     // 0x12345678
    uint32_t total_size;       // 153600バイト
    uint32_t chunk_index;      // 0-299
    uint32_t total_chunks;     // 300
    uint32_t chunk_offset;     // オフセット
    uint16_t chunk_data_size;  // 512バイト
    uint16_t checksum;         // チェックサム
} udp_photo_header_t;        // 24バイト
```

## トラブルシューティング

### よくある問題
1. **UDP受信エラー**: MATLABのDSP System Toolbox確認
2. **画像表示なし**: ポート9000のファイアウォール設定確認
3. **色彩異常**: YUV422フォーマット・エンディアン設定確認

# RA8E1 Real-time Image Transmission & Depth Reconstruction System

RA8E1マイコンを使用したリアルタイム画像伝送・深度再構成システム．カメラからキャプチャした画像から勾配を計算し，FFTまたは簡易積分法による深度再構成を行い，結果をUDP通信でMATLABに送信してリアルタイム表示するプロジェクト．

<p align="center">
  <img src="src/RA8E1Board_1.jpeg" alt="RA8E1 Board" width="600">
  <br>
  <em>RA8E1開発基板 - OV5642カメラモジュール，LAN8720A Ethernet PHY，IS66WVO8M8DALL OctalRAM搭載</em>
</p>

## システム概要

- **マイコン**: Renesas RA8E1 (R7FA8AFDCFB) ARM Cortex-M85 @ 200MHz
- **SIMD**: ARM Helium MVE (M-Profile Vector Extension) 有効
- **カメラ**: OV5642 (YUV422形式，QVGA 320×240)
- **メモリ**: OctalRAM IS66WVO8M8DALL (8MB)
- **通信**: Ethernet UDP (ポート9000)
- **機能**: 
  - リアルタイム動画ストリーミング (約1-2 fps)
  - リアルタイム勾配計算 (p/q勾配)
  - 深度再構成 (2つの手法):
    - **FFT法**: Frankot-Chellappa法 (~26秒/フレーム，高品質)
    - **簡易法**: 行積分法 (<1ms/フレーム，MVE最適化)

## 前提条件

### ハードウェア
- RA8E1開発基板(組み立て済み)
- Ethernetケーブル(クロス/ストレートどちらでも可)
- USB Type-Cケーブル(電源・書き込み・デバッグ用)

### ソフトウェア
- **Visual Studio Code** + Renesas Extension
- **LLVM Embedded Toolchain for Arm** v18.1.3
- **CMake** 3.25以降
- **Ninja** ビルドシステム
- **Renesas Flash Programmer**(書き込み用)
- **MATLAB** + **DSP System Toolbox**(必須)

## クイックスタートガイド

初めての方は以下の3ステップで動作確認できます：

### 1. ビルド
```bash
# cmake/llvm.cmake でツールチェーンパスを設定
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/llvm.cmake -G Ninja -B build/Debug
cmake --build build/Debug
```

### 2. 書き込み
1. CON1をショートしてSW1押下(ブートモード)
2. Renesas Flash Programmerで `build/Debug/RA8E1_prj.hex` を書き込み
3. CON1ショートを外してSW1押下(通常起動)

### 3. ネットワーク接続と動作確認
1. **PCとRA8E1をEthernetケーブルで直接接続**(クロス接続，推奨)
2. AutoIPによりIPアドレス自動割り当て(169.254.x.x)
3. USB CDC経由でシリアルログを確認：`[LwIP] AutoIP IP: 169.254.xxx.xxx`
4. MATLABで `udp_photo_receiver` 実行 → 動画表示

> **Note**: クロス接続(PC⇔RA8E1直結)がAutoIPで設定不要のため推奨です．DHCPサーバーがある環境ではルーター経由でも動作します．

## 開発環境のセットアップ

このプロジェクトは**Visual Studio Code + Renesas Extension**を使用した開発を推奨しています．

### 必要なコンパイラ

**LLVM Embedded Toolchain for Arm**
- **推奨バージョン**: v18.1.3(Renesas指定バージョン)
- **ダウンロード**: 
  - Windows/Intel Linux: [Renesas FSP Releases](https://github.com/renesas/fsp/releases) からFSP with e2 studioをインストールし，LLVMツールチェーンを選択してインストール
  - その他のプラットフォーム: [LLVM Embedded Toolchain for Arm](https://github.com/ARM-software/LLVM-embedded-toolchain-for-Arm/releases)
- **インストール例**: 
  - Windows: `C:/LLVM-ET-Arm-18.1.3-Windows-x86_64/`
  - Linux: `/home/user/LLVM-ET-Arm-18.1.3-Linux-AArch64/`

#### PATH指定方法

**方法1: [cmake/llvm.cmake](cmake/llvm.cmake) を直接編集(推奨)**
```cmake
# cmake/llvm.cmake の3行目を編集
set(ARM_TOOLCHAIN_PATH "C:/LLVM-ET-Arm-18.1.3-Windows-x86_64/bin")
# または
# set(ARM_TOOLCHAIN_PATH "/home/user/LLVM-ET-Arm-18.1.3-Linux-AArch64/bin")
```

**方法2: CMakeコマンドラインで指定**
```bash
cmake -DARM_TOOLCHAIN_PATH="C:/LLVM-ET-Arm-18.1.3-Windows-x86_64/bin" ...
```

**セットアップ方法は下記のRenesas公式動画を参照してください**:
- [Visual Studio Code - How to Install Renesas Extensions](https://www.bing.com/videos/riverview/relatedvideo?q=renesas+fsp+vsCode&mid=2458A3064E6E4F935C8E2458A3064E6E4F935C8E&FORM=VIRE)

## ビルド方法

### Visual Studio Codeでのビルド

**Renesas Extensionをインストール済みの場合**：
- **F7キーを押すだけでビルドできます**(最も簡単)
- または，ステータスバーの"Build"ボタンをクリック

詳細なセットアップ手順は[Renesas公式ビデオ](https://www.bing.com/videos/riverview/relatedvideo?q=renesas+fsp+vsCode&mid=2458A3064E6E4F935C8E2458A3064E6E4F935C8E&FORM=VIRE)を参照してください．

**注意事項**：
- ツールチェーンパスは [cmake/llvm.cmake](cmake/llvm.cmake) で設定
- CMake Kitは"ARM LLVM kit with toolchainFile"を選択
- パスにスペースを含めないことを推奨

Example:
```powershell
set ARM_LLVM_TOOLCHAIN_PATH=C:/LLVMEmbeddedToolchainForArm-18.1.3-Windows-x86_64/bin
cd "c:/Users/lynxe/Documents/GitHub/RA8E1_prj"
code .
```

### CLI でのビルド
Configure:
```bash
cmake -DARM_TOOLCHAIN_PATH="C:/LLVMEmbeddedToolchainForArm-18.1.3-Windows-x86_64/bin" -DCMAKE_TOOLCHAIN_FILE=cmake/llvm.cmake -G Ninja -B build/Debug
```

Build:
```bash
cmake --build build/Debug
```

### ARM SBC(Raspberry Piなど)でのビルド

Raspberry PiなどのARM SBCでは**RASC.exe(Windows専用)が動作しません**．以下の手順でRASCを呼び出さずにビルドできます：

1. **[cmake/GeneratedSrc.cmake](cmake/GeneratedSrc.cmake) を編集**
2. **99行目から129行目までをコメントアウト**(Pre-build stepとPost-build step)

```cmake
# Pre-build step: run RASC to generate project content if configuration.xml is changed
# add_custom_command(
#     OUTPUT
#         configuration.xml.stamp
#     COMMAND
#         ${RASC_EXE_PATH}  -nosplash --launcher.suppressErrors --generate ...
#     COMMAND
#         ${CMAKE_COMMAND} -E touch configuration.xml.stamp
#     COMMENT
#         "RASC pre-build to generate project content"
#     DEPENDS
#         ${CMAKE_CURRENT_SOURCE_DIR}/configuration.xml
# )
#
# add_custom_target(generate_content ALL
#   DEPENDS configuration.xml.stamp
# )
#
# add_dependencies(${PROJECT_NAME}.elf generate_content)
#
#
# # Post-build step: run RASC to generate the SmartBundle file
# add_custom_command(
#     TARGET
#         ${PROJECT_NAME}.elf
#     POST_BUILD
#     COMMAND
#         echo Running RASC post-build to generate Smart Bundle (.sbd) file
#     COMMAND
#         ${RASC_EXE_PATH} -nosplash --launcher.suppressErrors --gensmartbundle ...
#     VERBATIM
# )
```

3. 通常通りビルド
```bash
cmake -DARM_TOOLCHAIN_PATH="/home/user/LLVM-ET-Arm-18.1.3-Linux-AArch64/bin" -DCMAKE_TOOLCHAIN_FILE=cmake/llvm.cmake -G Ninja -B build/Debug
cmake --build build/Debug
```

Build:
```bash
cmake --build build/Debug
```

## マイコンへの書き込み方法

<p align="center">
  <img src="RA8E1Board_2.jpeg" alt="RA8E1 Boot Mode" width="600">
  <br>
  <em>ブートモード設定 - CON1ショートとSW1ボタン位置</em>
</p>

### ブートモードへの移行
1. **CON1をショート**: ショートプラグでCON1の2ピンを短絡(※シルク印刷は写真では上下反転)
2. **SW1を押す**: リセットボタン(SW1)を押してブートモードに入る
3. **書き込み準備完了**: RA8マイコンが書き込み待機状態になる

### Renesas Flash Programmerでの書き込み
1. [Renesas Flash Programmer](https://www.renesas.com/ja/software-tool/renesas-flash-programmer-programming-gui)をダウンロード・インストール
2. ビルドで生成されたHEXファイルを指定:
   - デバッグビルド: `build/Debug/RA8E1_prj.hex`
   - リリースビルド: `build/Release/RA8E1_prj.hex`
3. 接続設定:
   - **インターフェース**: USB CDC
   - **デバイス**: RA8E1 (R7FA8AFDCFB)
4. 書き込み実行後，**CON1のショートを外してからSW1を押す**(通常起動)

## 使用方法

### MATLAB受信側
```matlab
% UDP受信開始(無制限受信モード)
udp_photo_receiver

% リアルタイムで動画ストリーミング表示
% 停止: Ctrl+C または画像ウィンドウを閉じる
```

### 通信プロトコル
- **動作モード**: マルチフレーム動画送信
- **チャンク送信間隔**: 0ms(最速，pbuf確保失敗時は1msリトライ)
- **フレーム間隔**: 2ms(設定可変)
- **フレーム数**: 無制限(total_frames = -1)または指定数
- **チャンクサイズ**: 512バイト/パケット
- **総パケット数**: 300パケット/フレーム
- **パケット構造**: 24バイトヘッダー + 512バイトデータ
- **実効フレームレート**: 約1-2 fps(ネットワーク環境依存)

## ネットワーク接続

### 方法1: AutoIP(クロス接続，推奨)

**最も簡単な接続方法です．ルーターやスイッチの設定が不要です．**

1. **PCとRA8E1を直接Ethernetケーブルで接続**
   - クロスケーブル/ストレートケーブルどちらでも可(Auto MDI-X対応)
2. **自動的にIPアドレスが割り当てられます**
   - RA8E1: `169.254.x.x`(AutoIP)
   - PC: Windowsは自動的にAutoIP有効化
3. **USB CDC経由でシリアルログ確認**
   ```
   [LwIP] DHCP timeout: AutoIP start...
   [LwIP] AutoIP IP: 169.254.xxx.xxx
   [VIDEO] Starting 1000 frame transmission: ...
   ```

### 方法2: DHCP(ルーター経由)

DHCPサーバーがある環境(家庭用ルーターなど)では自動的にIPアドレスが割り当てられます．

1. **RA8E1とPCを同じルーター/スイッチに接続**
2. **シリアルログでIPアドレス確認**
   ```
   [LwIP] DHCP IP: 192.168.x.xxx
   ```

> **Tip**: クロス接続(方法1)はネットワーク設定に悩まされず，すぐに動作確認できるため推奨します．

## 動作確認

### 1. USB CDCログの確認

TeraTerm，PuTTY，Arduino IDEのシリアルモニタなどでUSB CDC(仮想COMポート)に接続：

- **ボーレート**: 自動(USB CDC)
- **期待されるログ**:
  ```
  [ETH] LAN8720A Ready
  [LwIP] AutoIP IP: 169.254.xxx.xxx  (または DHCP IP)
  [VIDEO] Starting 1000 frame transmission: 153600 bytes/frame, 300 chunks/frame
  [VIDEO] F10/1000 done
  [VIDEO] F20/1000 done
  ...
  ```

### 2. MATLAB受信確認

```matlab
% udp_photo_receiver.m を実行
udp_photo_receiver
```

**期待される動作**:
- ウィンドウが開いて動画がリアルタイム表示される
- 10秒ごとに統計情報が表示: `Frames: 100 (1.23 fps)`

**トラブル時**:
- UDP受信できない → Windowsファイアウォールでポート9000を許可
- 画像が表示されない → DSP System Toolboxがインストールされているか確認

## 設定のカスタマイズ

### C側設定(main_thread1_entry.c)
```c
ctx->interval_ms = 0;           // チャンク間隔 (0=最速, 推奨3-5ms)
ctx->frame_interval_ms = 2;     // フレーム間隔 (ms)
ctx->total_frames = -1;         // -1=無制限, 数値=指定フレーム数
```

### 深度再構成設定 (main_thread3_entry.c)
```c
#define USE_DEPTH_METHOD 0      // 2=マルチグリッド(ポアソン解法), その他=簡易行積分
#define USE_SIMPLE_DIRECT_P 1   // 1=HyperRAMから直接読み出し, 0=従来SRAM経由
```

**深度再構成モード**:
- **USE_DEPTH_METHOD = 2**: マルチグリッド・ポアソン解法
  - 処理時間: 約0.5〜2秒/フレーム
  - 中品質。HyperRAM上にワークスペースを展開
  - 品質重視または後処理向け

- **USE_DEPTH_METHOD != 2**: 簡易行積分法
  - 処理時間: 1ms未満/フレーム
  - MVE最適化済み(Helium命令で20-25%高速化)
  - リアルタイム用途に最適。表面品質は低め

**簡易版バリアント**:
- **USE_SIMPLE_DIRECT_P = 1**: p勾配をHyperRAMから直接ストリーミング（最速・RAM節約）
- **USE_SIMPLE_DIRECT_P = 0**: 従来のSRAMバッファ経由（デバッグや他形式出力が必要な場合）

### MATLAB側設定(udp_photo_receiver.m)
```matlab
total_timeout_sec = inf;        % inf=無制限, 数値=秒数制限
frame_timeout_sec = 10;         % フレームタイムアウト
```

## プロジェクト構造

```
RA8E1_prj/
├── src/                    # ソースコード
│   ├── hal_entry.c        # メインエントリーポイント
│   ├── main_thread0_entry.c # カメラキャプチャタスク
│   ├── main_thread1_entry.c # UDP送信タスク
│   ├── main_thread2_entry.c # 予約タスク
│   ├── main_thread3_entry.c # 勾配計算・深度再構成タスク
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

## トラブルシューティング

### よくある問題
1. **UDP受信エラー**: MATLABのDSP System Toolbox確認
2. **画像表示なし**: ポート9000のファイアウォール設定確認
3. **色彩異常**: YUV422フォーマット・エンディアン設定確認
4. **pbuf allocエラー**: `interval_ms`を3-5msに増やす(lwIPメモリプール不足)
5. **フレームレート低下**: ネットワーク帯域，MATLAB処理速度を確認

---

## ハードウェア詳細

### 基板設定

#### Ethernet PHY (LAN8720A)
- **PHY IC**: LAN8720A
- **インターフェース**: RMII (Reduced Media Independent Interface)
- **動作確認**: DHCP自動IP取得，AutoIP対応

#### OctalRAM接続
- **IC**: IS66WVO8M8DALL
- **容量**: 64Mbit (8MB)
- **インターフェース**: Octal SPI
- **ベースアドレス**: `HYPERRAM_BASE_ADDR`
- **アドレス変換**: `((addr & 0xfffffff0) << 6) | (addr & 0x0f)`
- **アクセス制限**: 64バイト単位推奨

#### カメラインターフェース (CEU)
- **カメラモジュール**: OV5642
- **信号方式**: DVP (Digital Video Port)
- **データフォーマット**: YUV422 (YUYV)
- **制御インターフェース**: SCCB (I2C互換)
- **解像度**: QVGA (320×240)
- **フレームサイズ**: 153,600バイト (320×240×2)

#### USB通信
- **機能**: CDC (Communications Device Class)
- **用途**: デバッグログ出力 (`xprintf`)
- **ボーレート**: 自動 (USB CDC)

#### FreeRTOS構成
- **Thread0**: カメラキャプチャ → HyperRAM書き込み (200ms周期)
- **Thread1**: UDP動画ストリーミング送信
- **Thread2**: 予約(未使用)
- **Thread3**: 勾配計算・深度再構成
  - Sobelオペレータによるp/q勾配計算
  - 切替可能な深度再構成(FFTまたは簡易版)
  - 性能向上のためMVE最適化

### 基板組み立て手順

#### 1. 基板全体の確認

<p align="center">
  <img src="RA8E1Board_3.jpeg" alt="RA8E1 Board Overview" width="600">
  <br>
  <em>RA8E1開発基板全体 - 組み立て前の状態</em>
</p>

基板には以下のコネクタを実装します：
- **CON2**: DVPカメラ用2×10ピンソケット
- **CON1**: ブートモード切替用ピンヘッダ(ショートジャンパー用)
- **CON3**: Raspberry Pi互換スタッキングコネクタ(オプション)

> **Note**: CON3にRaspberry Pi用スタッキングコネクタ(2×20ピン)を実装することで，Raspberry Pi上にRA8E1基板をスタックして使用できます．写真では未実装ですが，必要に応じて半田付けしてください．

#### 2. カメラモジュールの取り付け

<p align="center">
  <img src="RA8E1Board_4.jpeg" alt="RA8E1 Board with Camera" width="600">
  <br>
  <em>OV5642カメラモジュールをCON2にマウントした状態</em>
</p>

**組み立て手順**：
1. **CON2に2×10ピンソケットを半田付け** - DVPカメラインターフェース用
2. **CON1にピンヘッダを半田付け** - ショートジャンパー用(2ピン)
3. **OV5642モジュールをCON2に挿入** - カメラの向きに注意
4. **動作確認** - ショートやはんだブリッジがないか確認

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

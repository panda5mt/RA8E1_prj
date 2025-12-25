# RA8E1 Real-time Image Transmission & Depth Reconstruction System

> [日本語版はこちら / Japanese version](READMEjp.md)

A real-time image transmission and depth reconstruction system using the RA8E1 microcontroller. This project captures images from a camera, computes gradients, performs depth reconstruction using FFT or simple integration methods, and transmits the results to MATLAB via UDP for real-time display.

<p align="center">
  <img src="src/RA8E1Board_1.jpeg" alt="RA8E1 Board" width="600">
  <br>
  <em>RA8E1 Development Board - Equipped with OV5642 Camera Module, LAN8720A Ethernet PHY, IS66WVO8M8DALL OctalRAM</em>
</p>

## System Overview

- **Microcontroller**: Renesas RA8E1 (R7FA8AFDCFB) with ARM Cortex-M85 @ 200MHz
- **SIMD**: ARM Helium MVE (M-Profile Vector Extension) enabled
- **Camera**: OV5642 (YUV422 format, QVGA 320×240)
- **Memory**: OctalRAM IS66WVO8M8DALL (8MB)
- **Communication**: Ethernet UDP (Port 9000)
- **Features**: 
  - Real-time video streaming (~1-2 fps)
  - Real-time gradient computation (p/q gradients)
  - Depth reconstruction (2 methods):
    - **FFT Method**: Frankot-Chellappa algorithm (~26 seconds/frame, high quality)
    - **Simple Method**: Row integration (<1ms/frame, optimized with MVE)

## Prerequisites

### Hardware
- RA8E1 Development Board (assembled)
- Ethernet Cable (crossover or straight-through)
- USB Type-C Cable (for power, programming, and debugging)

### Software
- **Visual Studio Code** + Renesas Extension
- **LLVM Embedded Toolchain for Arm** v18.1.3
- **CMake** 3.25 or later
- **Ninja** Build System
- **Renesas Flash Programmer** (for programming)
- **MATLAB** + **DSP System Toolbox** (required)

## Quick Start Guide

Follow these 3 steps to verify operation:

### 1. Build
```bash
# Set toolchain path in cmake/llvm.cmake
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/llvm.cmake -G Ninja -B build/Debug
cmake --build build/Debug
```

### 2. Flash
1. Short CON1 and press SW1 (boot mode)
2. Use Renesas Flash Programmer to flash `build/Debug/RA8E1_prj.hex`
3. Remove CON1 short and press SW1 (normal startup)

### 3. Network Connection and Verification
1. **Connect PC and RA8E1 directly with Ethernet cable** (crossover connection, recommended)
2. IP address automatically assigned via AutoIP (169.254.x.x)
3. Check serial log via USB CDC: `[LwIP] AutoIP IP: 169.254.xxx.xxx`
4. Run `udp_photo_receiver` in MATLAB → Video display

> **Note**: Crossover connection (PC⇔RA8E1 direct) is recommended as it requires no AutoIP configuration. The system also works via router in environments with a DHCP server.

## Development Environment Setup

This project recommends development using **Visual Studio Code + Renesas Extension**.

### Required Compiler

**LLVM Embedded Toolchain for Arm**
- **Recommended Version**: v18.1.3 (Renesas specified version)
- **Download**: 
  - Windows/Intel Linux: Install FSP with e2 studio from [Renesas FSP Releases](https://github.com/renesas/fsp/releases) and select LLVM toolchain during installation
  - Other platforms: [LLVM Embedded Toolchain for Arm](https://github.com/ARM-software/LLVM-embedded-toolchain-for-Arm/releases)
- **Installation Examples**: 
  - Windows: `C:/LLVM-ET-Arm-18.1.3-Windows-x86_64/`
  - Linux: `/home/user/LLVM-ET-Arm-18.1.3-Linux-AArch64/`

#### PATH Configuration Methods

**Method 1: Edit [cmake/llvm.cmake](cmake/llvm.cmake) directly (recommended)**
```cmake
# Edit line 3 in cmake/llvm.cmake
set(ARM_TOOLCHAIN_PATH "C:/LLVM-ET-Arm-18.1.3-Windows-x86_64/bin")
# or
# set(ARM_TOOLCHAIN_PATH "/home/user/LLVM-ET-Arm-18.1.3-Linux-AArch64/bin")
```

**Method 2: Specify via CMake command line**
```bash
cmake -DARM_TOOLCHAIN_PATH="C:/LLVM-ET-Arm-18.1.3-Windows-x86_64/bin" ...
```

**For setup instructions, refer to the official Renesas video**:
- [Visual Studio Code - How to Install Renesas Extensions](https://www.bing.com/videos/riverview/relatedvideo?q=renesas+fsp+vsCode&mid=2458A3064E6E4F935C8E2458A3064E6E4F935C8E&FORM=VIRE)

## Build Instructions

### Building with Visual Studio Code

**If Renesas Extension is installed**:
- **Simply press F7 key to build** (easiest method)
- Or click the "Build" button in the status bar

For detailed setup instructions, refer to the [official Renesas video](https://www.bing.com/videos/riverview/relatedvideo?q=renesas+fsp+vsCode&mid=2458A3064E6E4F935C8E2458A3064E6E4F935C8E&FORM=VIRE).

**Notes**:
- Set toolchain path in [cmake/llvm.cmake](cmake/llvm.cmake)
- Select "ARM LLVM kit with toolchainFile" as CMake Kit
- Avoid using spaces in paths

Example:
```powershell
set ARM_LLVM_TOOLCHAIN_PATH=C:/LLVMEmbeddedToolchainForArm-18.1.3-Windows-x86_64/bin
cd "c:/Users/lynxe/Documents/GitHub/RA8E1_prj"
code .
```

### CLI Build
Configure:
```bash
cmake -DARM_TOOLCHAIN_PATH="C:/LLVMEmbeddedToolchainForArm-18.1.3-Windows-x86_64/bin" -DCMAKE_TOOLCHAIN_FILE=cmake/llvm.cmake -G Ninja -B build/Debug
```

Build:
```bash
cmake --build build/Debug
```

### Building on ARM SBC (Raspberry Pi, etc.)

On ARM SBCs like Raspberry Pi, **RASC.exe (Windows-only) does not work**. You can build without calling RASC using the following steps:

1. **Edit [cmake/GeneratedSrc.cmake](cmake/GeneratedSrc.cmake)**
2. **Comment out lines 99-129** (Pre-build step and Post-build step)

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

3. Build as usual
```bash
cmake -DARM_TOOLCHAIN_PATH="/home/user/LLVM-ET-Arm-18.1.3-Linux-AArch64/bin" -DCMAKE_TOOLCHAIN_FILE=cmake/llvm.cmake -G Ninja -B build/Debug
cmake --build build/Debug
```

## Flashing to Microcontroller

<p align="center">
  <img src="RA8E1Board_2.jpeg" alt="RA8E1 Boot Mode" width="600">
  <br>
  <em>Boot Mode Configuration - CON1 Short and SW1 Button Location</em>
</p>

### Entering Boot Mode
1. **Short CON1**: Short-circuit CON1's 2 pins with a jumper (※Silk printing is upside down in photo)
2. **Press SW1**: Press reset button (SW1) to enter boot mode
3. **Ready to Flash**: RA8 microcontroller is in programming standby mode

### Flashing with Renesas Flash Programmer
1. Download and install [Renesas Flash Programmer](https://www.renesas.com/ja/software-tool/renesas-flash-programmer-programming-gui)
2. Specify the HEX file generated by build:
   - Debug build: `build/Debug/RA8E1_prj.hex`
   - Release build: `build/Release/RA8E1_prj.hex`
3. Connection settings:
   - **Interface**: USB CDC
   - **Device**: RA8E1 (R7FA8AFDCFB)
4. After programming, **remove CON1 short and press SW1** (normal startup)

## Usage

### MATLAB Receiver
```matlab
% Start UDP reception (unlimited reception mode)
udp_photo_receiver

% Real-time video streaming display
% Stop: Ctrl+C or close image window
```

### Communication Protocol
- **Operation Mode**: Multi-frame video transmission
- **Chunk Transmission Interval**: 0ms (fastest, 1ms retry on pbuf allocation failure)
- **Frame Interval**: 2ms (configurable)
- **Frame Count**: Unlimited (total_frames = -1) or specified count
- **Chunk Size**: 512 bytes/packet
- **Total Packets**: 300 packets/frame
- **Packet Structure**: 24-byte header + 512-byte data
- **Effective Frame Rate**: ~1-2 fps (network environment dependent)

## Network Connection

### Method 1: AutoIP (Crossover Connection, Recommended)

**The easiest connection method. No router or switch configuration required.**

1. **Connect PC and RA8E1 directly with Ethernet cable**
   - Either crossover or straight-through cable (Auto MDI-X supported)
2. **IP addresses automatically assigned**
   - RA8E1: `169.254.x.x` (AutoIP)
   - PC: Windows automatically enables AutoIP
3. **Check serial log via USB CDC**
   ```
   [LwIP] DHCP timeout: AutoIP start...
   [LwIP] AutoIP IP: 169.254.xxx.xxx
   [VIDEO] Starting 1000 frame transmission: ...
   ```

### Method 2: DHCP (Via Router)

In environments with a DHCP server (home routers, etc.), IP addresses are automatically assigned.

1. **Connect RA8E1 and PC to the same router/switch**
2. **Check IP address in serial log**
   ```
   [LwIP] DHCP IP: 192.168.x.xxx
   ```

> **Tip**: Crossover connection (Method 1) is recommended as you can verify operation immediately without network configuration issues.

## Operation Verification

### 1. USB CDC Log Verification

Connect to USB CDC (virtual COM port) using TeraTerm, PuTTY, Arduino IDE Serial Monitor, etc.:

- **Baud Rate**: Auto (USB CDC)
- **Expected Log**:
  ```
  [ETH] LAN8720A Ready
  [LwIP] AutoIP IP: 169.254.xxx.xxx  (or DHCP IP)
  [VIDEO] Starting 1000 frame transmission: 153600 bytes/frame, 300 chunks/frame
  [VIDEO] F10/1000 done
  [VIDEO] F20/1000 done
  ...
  ```

### 2. MATLAB Reception Verification

```matlab
% Run udp_photo_receiver.m
udp_photo_receiver
```

**Expected Behavior**:
- Window opens and video is displayed in real-time
- Statistics displayed every 10 seconds: `Frames: 100 (1.23 fps)`

**Troubleshooting**:
- Cannot receive UDP → Allow port 9000 in Windows Firewall
- No image displayed → Verify DSP System Toolbox is installed

## Configuration Customization

### C-side Settings (main_thread1_entry.c)
```c
ctx->interval_ms = 0;           // Chunk interval (0=fastest, recommend 3-5ms)
ctx->frame_interval_ms = 2;     // Frame interval (ms)
ctx->total_frames = -1;         // -1=unlimited, number=specified frame count
```

### Depth Reconstruction Settings (main_thread3_entry.c)
```c
#define USE_DEPTH_METHOD 0      // 2=Multigrid Poisson solver, others=Simple integration
#define USE_SIMPLE_DIRECT_P 1   // 1=read p directly from HyperRAM, 0=use SRAM buffer
```

**Depth Reconstruction Modes**:
- **USE_DEPTH_METHOD = 2**: Multigrid Poisson solver
  - Processing time: ~0.5-2 seconds per frame
  - Medium quality, HyperRAM-backed workspace
  - Best when accuracy matters more than latency

- **USE_DEPTH_METHOD != 2**: Simple row-integration path
  - Processing time: <1ms per frame
  - MVE-optimized (20-25% faster with Helium intrinsics)
  - Ideal for real-time preview; lower surface fidelity

**Simple Mode Variants**:
- **USE_SIMPLE_DIRECT_P = 1**: Stream p-gradients directly from HyperRAM (fastest, minimal RAM usage)
- **USE_SIMPLE_DIRECT_P = 0**: Use legacy SRAM buffer path (useful for debugging or alternate exporters)

### MATLAB-side Settings (udp_photo_receiver.m)
```matlab
total_timeout_sec = inf;        % inf=unlimited, number=seconds limit
frame_timeout_sec = 10;         % Frame timeout
```

## Project Structure

```
RA8E1_prj/
├── src/                    # Source code
│   ├── hal_entry.c        # Main entry point
│   ├── main_thread0_entry.c # Camera capture task
│   ├── main_thread1_entry.c # UDP transmission task
│   ├── main_thread2_entry.c # Reserved task
│   ├── main_thread3_entry.c # Gradient & depth reconstruction task
│   ├── cam.c              # Camera control
│   ├── hyperram_integ.c   # OctalRAM integration
│   └── usb_cdc.h          # USB CDC communication
├── matlab/                # MATLAB receiver code
│   ├── udp_photo_receiver.m    # Main receiver function
│   ├── test_udp_simple.m      # UDP connection test
│   └── viewQVGA_YUV.m         # YUV reference decoder
├── ra_gen/                # FSP generated files
├── ra_cfg/                # FSP configuration
└── cmake/                 # CMake settings
```

## Troubleshooting

### Common Issues
1. **UDP Reception Error**: Check MATLAB DSP System Toolbox
2. **No Image Display**: Check port 9000 firewall settings
3. **Color Abnormality**: Check YUV422 format and endian settings
4. **pbuf alloc Error**: Increase `interval_ms` to 3-5ms (lwIP memory pool shortage)
5. **Frame Rate Drop**: Check network bandwidth and MATLAB processing speed

---

## Hardware Details

### Board Configuration

#### Ethernet PHY (LAN8720A)
- **PHY IC**: LAN8720A
- **Interface**: RMII (Reduced Media Independent Interface)
- **Verified**: DHCP automatic IP acquisition, AutoIP support

#### OctalRAM Connection
- **IC**: IS66WVO8M8DALL
- **Capacity**: 64Mbit (8MB)
- **Interface**: Octal SPI
- **Base Address**: `HYPERRAM_BASE_ADDR`
- **Address Conversion**: `((addr & 0xfffffff0) << 6) | (addr & 0x0f)`
- **Access Limitation**: 64-byte units recommended

#### Camera Interface (CEU)
- **Camera Module**: OV5642
- **Signal Format**: DVP (Digital Video Port)
- **Data Format**: YUV422 (YUYV)
- **Control Interface**: SCCB (I2C compatible)
- **Resolution**: QVGA (320×240)
- **Frame Size**: 153,600 bytes (320×240×2)

#### USB Communication
- **Function**: CDC (Communications Device Class)
- **Purpose**: Debug log output (`xprintf`)
- **Baud Rate**: Auto (USB CDC)

#### FreeRTOS Configuration
- **Thread0**: Camera capture → HyperRAM write (200ms cycle)
- **Thread1**: UDP video streaming transmission
- **Thread2**: Reserved (unused)
- **Thread3**: Gradient computation & depth reconstruction
  - Sobel operators for p/q gradient calculation
  - Switchable depth reconstruction (FFT or Simple)
  - MVE-optimized for performance

### Board Assembly Instructions

#### 1. Board Overview

<p align="center">
  <img src="RA8E1Board_3.jpeg" alt="RA8E1 Board Overview" width="600">
  <br>
  <em>RA8E1 Development Board Overview - Before Assembly</em>
</p>

Install the following connectors on the board:
- **CON2**: 2×10 pin socket for DVP camera
- **CON1**: Pin header for boot mode switching (for jumper)
- **CON3**: Raspberry Pi compatible stacking connector (optional)

> **Note**: By installing a Raspberry Pi stacking connector (2×20 pin) on CON3, you can stack the RA8E1 board on a Raspberry Pi. Not installed in the photo, but solder if needed.

#### 2. Camera Module Installation

<p align="center">
  <img src="RA8E1Board_4.jpeg" alt="RA8E1 Board with Camera" width="600">
  <br>
  <em>OV5642 Camera Module Mounted on CON2</em>
</p>

**Assembly Steps**:
1. **Solder 2×10 pin socket to CON2** - For DVP camera interface
2. **Solder pin header to CON1** - For jumper (2 pins)
3. **Insert OV5642 module into CON2** - Pay attention to camera orientation
4. **Operation Check** - Verify no shorts or solder bridges

## Technical Specifications

### OctalRAM Address Conversion
Support for Octal RAM-specific address format:
```c
uint32_t converted_addr = ((base_addr & 0xfffffff0) << 6) | (base_addr & 0x0f);
```

### YUV422 Image Format
- **Memory Layout**: [V0 Y1 U0 Y0] (little-endian, 4 bytes/2 pixels)
- **Color Space Conversion**: ITU-R BT.601 standard
- **MATLAB Decode**: Using `dsp.UDPReceiver`

### Packet Structure
```c
typedef struct {
    uint32_t magic_number;     // 0x12345678
    uint32_t total_size;       // 153600 bytes
    uint32_t chunk_index;      // 0-299
    uint32_t total_chunks;     // 300
    uint32_t chunk_offset;     // Offset
    uint16_t chunk_data_size;  // 512 bytes
    uint16_t checksum;         // Checksum
} udp_photo_header_t;        // 24 bytes
```

# EDID Parser

EDID (Extended Display Identification Data) 解析工具，支持 EDID 1.3/1.4 规范。

## 功能

- **EDID Base Block** — Header、Manufacturer ID、Product Code、Serial Number、Display Parameters、Color Characteristics、Established Timings、Standard Timings、Detailed Timing (DTD)
- **Monitor Descriptors** — Serial Number、ASCII String、Monitor Name、Range Limits (GTF)、Color Point、Standard Timings (9-14)
- **CTA-861 Extension** — Video Data Block (VIC)、Audio Data Block、Speaker Allocation、Vendor Specific (HDMI VSDB)、Extended Data Block
- **双向格式转换** — txt ↔ bin 自动导出

## 文件说明

| 文件 | 说明 |
|------|------|
| `edid_parser.h` | 头文件，结构体定义和 API 声明 |
| `edid_parser.c` | EDID 解析器核心实现 |
| `main.c` | 主程序，文件读取/格式检测/导出 |
| `build.bat` | 构建脚本，输出 `edid.exe` |

## 编译

```bash
.\build.bat
```

或手动：

```bash
gcc -o edid.exe main.c edid_parser.c -Wall
```

## 使用

```bash
edid.exe <edid_file>
```

支持两种输入格式：

| 格式 | 扩展名 | 说明 |
|------|--------|------|
| 十六进制文本 | `.txt` | `0x00, 0xFF, ...` 格式，自动导出 `.bin` |
| 二进制 | `.bin` / `.dat` | 原始 EDID 字节，自动导出 `.txt` |

### 示例

```bash
edid.exe edid.txt    # 解析 txt，导出 edid.bin
edid.exe edid.bin    # 解析 bin，导出 edid.txt
```

## 输出示例

```
========== EDID ==========
Version      : 1.3
Manufacturer : HEX (0x20B8)
Product Code : 0x3132
Serial Number: 0x88888800
Year         : 2024

Video Input  : 0x80 (Digital, DFP1.x=0)
Image Size   : 7 cm x 12 cm
Gamma        : 2.20
Feature      : 0x0A
  Display Type: RGB Color
  Pref Timing: Preferred timing mode

Color Chars  : R(640,348) G(287,609) B(159,72) W(290,305)
  Red   : x=0.625 y=0.339
  Green : x=0.280 y=0.594
  Blue  : x=0.155 y=0.070
  White : x=0.283 y=0.298

Descriptor 2: Monitor Range Limits
  V Freq   : 5 - 120 Hz
  H Freq   : 50 - 90 kHz
  Max Clock: 600 MHz

Descriptor 3: Monitor Name: HEXAGON

========== Block 1 ==========
Type     : CTA-861
  Audio: format=1 channels=2
  HDMI VSDB OUI: 0x000C03

========== Video Modes ==========
[00] 1920x1080 @ 50Hz PixelClock=148500 kHz
```

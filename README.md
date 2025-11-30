# Bad Apple Video Player for STM32L476RG

An embedded video and audio player that plays the famous "Bad Apple!!" music video on an STM32L476RG microcontroller with a 128x64 OLED display and stereo DAC audio output.

## Features

- **30 FPS Video Playback** - Smooth monochrome video on 128x64 SSD1306 OLED
- **32 kHz Stereo Audio** - Dual DAC output (PA4/PA5) with DMA circular buffers
- **Audio-Master Synchronization** - Video follows audio timing for perfect sync
- **Triple-Buffered Display** - Tear-free rendering with DMA transfers
- **FAT32 SD Card Support** - Custom minimal FAT32 implementation
- **Contiguous File Optimization** - Fast-path for defragmented files

## Hardware Requirements

| Component | Specification |
|-----------|---------------|
| MCU | STM32L476RG (NUCLEO-L476RG board) |
| Display | SSD1306 128x64 OLED (I2C) |
| Storage | MicroSD card (FAT32, Class 4+) |
| Audio | Headphones/amplifier on PA4/PA5 |

### Pin Connections

```
NUCLEO-L476RG Pinout:
+-------------------------------------+
|           STM32L476RG               |
+-------------------------------------+
|  OLED Display (I2C2)                |
|    PB13 -------- SCL                |
|    PB14 -------- SDA                |
|    3.3V -------- VCC                |
|    GND  -------- GND                |
+-------------------------------------+
|  SD Card (SPI3)                     |
|    PC10 -------- SCK                |
|    PC11 -------- MISO               |
|    PC12 -------- MOSI               |
|    PA9  -------- CS                 |
|    3.3V -------- VCC                |
|    GND  -------- GND                |
+-------------------------------------+
|  Audio Output (DAC)                 |
|    PA4  -------- Left Channel       |
|    PA5  -------- Right Channel      |
|    GND  -------- Audio Ground       |
+-------------------------------------+
|  Status LED                         |
|    PB3  -------- LED (built-in)     |
+-------------------------------------+
```

## Architecture

```
+----------------------------------------------------------------------+
|                        Main Loop                                     |
|  +-------------+  +-------------+  +-------------+                   |
|  | Audio Refill|  | Sync Check  |  | Video Render|                   |
|  |  (highest   |  | (get frame  |  | (to triple  |                   |
|  |  priority)  |  |  decision)  |  |   buffer)   |                   |
|  +------+------+  +------+------+  +------+------+                   |
+---------+----------------+----------------+--------------------------+
          |                |                |
          v                v                v
+-----------------+ +-----------------+ +-----------------+
|   Audio DAC     | |    A/V Sync     | |   SSD1306       |
|  +-----------+  | |                 | |  +-----------+  |
|  | DMA Ch1/2 |  | |  Audio samples  | |  | Triple    |  |
|  | Circular  |<-+-+  / samples/frame| |  | Buffer    |  |
|  | Buffer    |  | | = current frame | |  | System    |  |
|  +-----------+  | |                 | |  +-----------+  |
|       |         | |  Video frame    | |       |         |
|       v         | | - Audio frame   | |       v         |
|  +-----------+  | | = drift         | |  +-----------+  |
|  | TIM6 @    |  | |                 | |  | I2C2 DMA  |  |
|  | 32kHz     |  | | If drift > 2:   | |  | Transfer  |  |
|  | Trigger   |  | |   skip/repeat   | |  |           |  |
|  +-----------+  | +-----------------+ |  +-----------+  |
+--------+--------+                     +--------+--------+
         |                                       |
         v                                       v
    +---------+                            +---------+
    | PA4/PA5 |                            | SSD1306 |
    |  DAC    |                            |  OLED   |
    +---------+                            +---------+
```

### Key Design Decisions

1. **Audio-Master Sync**: Audio DMA runs at a fixed 32kHz rate and cannot be adjusted. Video frames are rendered, skipped, or repeated to match the audio timeline.

2. **Triple Buffering**: Three framebuffers allow simultaneous rendering (main loop), ready (completed), and transfer (DMA to display) without tearing.

3. **LEFT Channel Master**: Stereo DAC uses LEFT channel DMA callbacks for timing. RIGHT channel follows silently to avoid race conditions.

4. **Contiguous File Detection**: The FAT32 driver checks if the media file is defragmented and uses direct sector addressing for faster reads.

## Building

### Prerequisites

- STM32CubeIDE or arm-none-eabi-gcc toolchain
- STM32L4 HAL libraries
- Python 3.8+ (for media processing)
- FFmpeg (for audio extraction)
- OpenCV + PIL (for video processing)

### Firmware Build

1. Clone this repository
2. Open in STM32CubeIDE or configure your build system
3. Build with optimization `-O2` for best performance
4. Flash to NUCLEO-L476RG

### Media File Preparation

```bash
# Install Python dependencies
pip install opencv-python numpy pillow

# Process video and audio (requires BadApple.mp4 in current directory)
python tools/process_all.py

# Or run individual steps:
python tools/process_video.py   # Creates output/badapple_video.bin
python tools/process_audio.py   # Creates output/badapple_audio.raw
python tools/combine_files.py   # Creates output/badapple.bin

# Verify the output file
python tools/analyze_file.py output/badapple.bin
```

Copy `output/badapple.bin` to the root of a FAT32-formatted SD card.

## Media File Format

```
+------------------------------------------------+
| HEADER (20 bytes)                              |
+------------------------------------------------+
| [0-3]   Frame count      (uint32_t LE)         |
| [4-7]   Audio size       (uint32_t LE)         |
| [8-11]  Sample rate      (uint32_t LE) 32000   |
| [12-15] Channels         (uint32_t LE) 2       |
| [16-19] Bits per sample  (uint32_t LE) 16      |
+------------------------------------------------+
| VIDEO DATA (frame_count x 1024 bytes)          |
|   Each frame: 128x64 pixels in SSD1306 format  |
|   (8 pages x 128 columns, vertical byte order) |
+------------------------------------------------+
| AUDIO DATA (interleaved stereo PCM)            |
|   Format: [L0][R0][L1][R1]...[Ln][Rn]          |
|   16-bit signed little-endian                  |
+------------------------------------------------+
```

## Project Structure

```
bad-apple-stm32/
|-- Core/
|   |-- Inc/
|   |   |-- main.h              # Pin definitions, peripheral handles
|   |   |-- audio_dac.h         # Stereo DAC driver API
|   |   |-- av_sync.h           # A/V synchronization API
|   |   |-- buffers.h           # Triple buffer management
|   |   |-- fatfs.h             # FAT32 filesystem API
|   |   |-- media_file_reader.h # Media file parser
|   |   |-- perf.h              # DWT cycle counter utilities
|   |   |-- sd_card.h           # SD card SPI driver
|   |   |-- ssd1306.h           # OLED display driver
|   |   +-- stm32l4xx_*.h       # HAL configuration
|   +-- Src/
|       |-- main.c              # Application entry, playback loop
|       |-- audio_dac.c         # DAC DMA implementation
|       |-- av_sync.c           # Sync algorithm
|       |-- buffers.c           # Buffer allocation
|       |-- fatfs.c             # FAT32 implementation
|       |-- media_file_reader.c # File reading, format conversion
|       |-- perf.c              # Performance counter init
|       |-- sd_card.c           # SD card protocol
|       |-- ssd1306.c           # Display driver + font
|       +-- stm32l4xx_*.c       # HAL support files
|-- tools/
|   |-- process_video.py        # Video to binary converter
|   |-- process_audio.py        # Audio extractor
|   |-- combine_files.py        # File combiner
|   |-- process_all.py          # Full pipeline
|   +-- analyze_file.py         # File validator
+-- README.md
```

## Performance

| Metric | Value |
|--------|-------|
| Video frame rate | 30 FPS |
| Audio sample rate | 32 kHz |
| CPU clock | 80 MHz |
| I2C speed | 400 kHz (Fast Mode) |
| SPI speed | 10 MHz (after init) |
| SD read bandwidth | ~125 KB/s required |
| Audio buffer size | 2048 samples x 2 channels |
| Display buffer size | 1024 bytes x 3 (triple) |

## Playback Statistics

At the end of playback, the display shows:
- **Rendered**: Total video frames drawn
- **Skip**: Frames skipped (video was behind audio)
- **Rep**: Frames repeated (video was ahead of audio)
- **Refills**: Audio buffer refill count
- **Max fill**: Worst-case audio refill time (us)
- **Underruns**: Audio buffer underruns (should be 0)

## Troubleshooting

| Issue | Possible Cause | Solution |
|-------|---------------|----------|
| "SD Init... FAIL" | Wiring, card format | Check SPI connections, format as FAT32 |
| "NO FILE" | Missing file | Copy BADAPPLE.BIN to SD root |
| "FAT FAIL" | Not FAT32 | Reformat SD card as FAT32 |
| Choppy audio | SD too slow | Use Class 10 or UHS-I card |
| Video tearing | I2C issues | Check I2C pullups (4.7k ohm) |
| No audio | DAC not connected | Check PA4/PA5 connections |
| Inverted colors | Display setting | Set `INVERT = True` in process_video.py |

## License

MIT License

## Credits

- ZUN, "Bad Apple!!," in Lotus Land Story. Team Shanghai Alice, 1998.
- STM32 HAL by STMicroelectronics

## Author

David Leathers - November 2025

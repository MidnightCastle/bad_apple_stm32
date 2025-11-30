#!/usr/bin/env python3
"""
Bad Apple Video Processor for STM32L476RG + SSD1306
Converts video to 128x64 binary format optimized for OLED display

FIXED VERSION: Uses correct SSD1306 vertical page byte format
Each byte = 8 VERTICAL pixels (not horizontal!)

Target System:
- NUCLEO-L476RG @ 80 MHz
- SSD1306 OLED 128x64 via I2C2 (1 MHz)
- Target: 30 FPS playback
- Triple buffering with DMA-accelerated transfers

SSD1306 Memory Format:
- Display organized into 8 "pages" (rows of 8 pixels)
- Each page has 128 bytes (one per column)
- Each byte represents 8 VERTICAL pixels in that column
- Bit 0 = top pixel of 8-pixel column, Bit 7 = bottom pixel
- Buffer index: x + (y / 8) * 128

Author: David Leathers
Date: November 2025
Version: 2.1.0 - FIXED SSD1306 byte format
"""

import cv2
import numpy as np
import struct
import os
import sys
from PIL import Image

# ============================================================================
# CONFIGURATION
# ============================================================================

# Input/Output
VIDEO_FILE = "BadApple.mp4"
OUTPUT_FILE = os.path.join("output", "badapple_video.bin")
PREVIEW_DIR = "frames"

# Display settings (SSD1306 OLED)
OLED_WIDTH = 128
OLED_HEIGHT = 64
OLED_PAGES = OLED_HEIGHT // 8  # 8 pages of 8 pixels each
FRAMEBUFFER_SIZE = 1024  # 128 columns x 8 pages

# Video processing settings
TARGET_FPS = 30          # 30 fps for smooth playback (matches STM32 target)
THRESHOLD = 128          # Black/white threshold (0-255)
INVERT = False           # Set True if colors appear inverted

# Image enhancement
CONTRAST_BOOST = 1.2     # 1.0 = no boost, 1.5 = high boost
BRIGHTNESS_OFFSET = 0    # -50 to +50 for brightness adjustment

# Preview options
SAVE_PREVIEW = True      # Save preview images for verification
MAX_PREVIEW_FRAMES = 30  # Save 1 second of frames (30 fps)
START_PREVIEW_AT_FRAME = 900  # Start after ~30 seconds (30fps x 30)
PREVIEW_SCALE = 4        # Scale factor for preview images

# ============================================================================
# IMAGE PROCESSING
# ============================================================================

def enhance_contrast(frame, factor=1.2, brightness=0):
    """
    Boost contrast and adjust brightness for better black/white separation
    
    Args:
        frame: Grayscale image (0-255)
        factor: Contrast multiplier (1.0 = no change, >1.0 = more contrast)
        brightness: Brightness offset (-50 to +50)
    
    Returns:
        Enhanced grayscale image
    """
    img = frame.astype(float)
    
    # Apply brightness offset first
    img = img + brightness
    
    # Apply contrast around midpoint (128)
    img = (img - 128) * factor + 128
    
    # Clip to valid range
    img = np.clip(img, 0, 255)
    
    return img.astype(np.uint8)


def frame_to_ssd1306_format(frame):
    """
    Convert grayscale frame to SSD1306 vertical page format
    
    FIXED: Uses correct SSD1306 memory layout:
    - 8 pages of 8 vertical pixels each
    - Each byte represents 8 VERTICAL pixels in a column
    - Bit 0 = top pixel of 8-pixel column
    - Bit 7 = bottom pixel of 8-pixel column
    - Buffer index: x + (y // 8) * 128
    
    Args:
        frame: 128x64 grayscale image (0-255)
    
    Returns:
        1024 bytes of packed binary data in SSD1306 format
    """
    # Apply threshold to create binary image
    _, binary = cv2.threshold(frame, THRESHOLD, 1, cv2.THRESH_BINARY)
    
    # Invert if requested
    if INVERT:
        binary = 1 - binary
    
    # Pack pixels into bytes (SSD1306 vertical page format)
    frame_bytes = bytearray(FRAMEBUFFER_SIZE)
    
    for page in range(OLED_PAGES):  # 8 pages
        for x in range(OLED_WIDTH):  # 128 columns
            byte_val = 0
            for bit in range(8):  # 8 vertical pixels per byte
                y = page * 8 + bit
                if y < OLED_HEIGHT and binary[y, x]:
                    # Bit 0 = top pixel (y=page*8+0)
                    # Bit 7 = bottom pixel (y=page*8+7)
                    byte_val |= (1 << bit)
            
            # Calculate buffer index: x + page * 128
            buffer_idx = x + page * OLED_WIDTH
            frame_bytes[buffer_idx] = byte_val
    
    return bytes(frame_bytes)


def frame_to_binary_old(frame):
    """
    OLD VERSION - WRONG FORMAT (horizontal byte packing)
    Kept for reference only - DO NOT USE
    """
    _, binary = cv2.threshold(frame, THRESHOLD, 255, cv2.THRESH_BINARY)
    if INVERT:
        binary = 255 - binary
    
    frame_bytes = bytearray()
    for y in range(OLED_HEIGHT):
        for x in range(0, OLED_WIDTH, 8):
            byte_val = 0
            for bit in range(8):
                if x + bit < OLED_WIDTH:
                    pixel = binary[y, x + bit]
                    if pixel > 127:
                        byte_val |= (1 << (7 - bit))
            frame_bytes.append(byte_val)
    
    return bytes(frame_bytes)


def save_preview_image(frame, frame_num, scale=4):
    """
    Save preview image for quality verification
    
    Args:
        frame: Grayscale frame (0-255)
        frame_num: Frame number for filename
        scale: Upscale factor (nearest neighbor)
    """
    img = Image.fromarray(frame)
    img = img.resize((OLED_WIDTH * scale, OLED_HEIGHT * scale), Image.NEAREST)
    filename = os.path.join(PREVIEW_DIR, f"frame_{frame_num:04d}.png")
    img.save(filename)


def verify_ssd1306_format(frame_bytes, original_binary):
    """
    Verify the conversion is correct by decoding and comparing
    
    Args:
        frame_bytes: Packed SSD1306 format data
        original_binary: Original binary image (0/1 values)
    
    Returns:
        bool: True if conversion is correct
    """
    # Decode the packed format
    decoded = np.zeros((OLED_HEIGHT, OLED_WIDTH), dtype=np.uint8)
    
    for page in range(OLED_PAGES):
        for x in range(OLED_WIDTH):
            byte_val = frame_bytes[x + page * OLED_WIDTH]
            for bit in range(8):
                y = page * 8 + bit
                if y < OLED_HEIGHT:
                    decoded[y, x] = 1 if (byte_val & (1 << bit)) else 0
    
    # Compare with original
    return np.array_equal(decoded, original_binary)


# ============================================================================
# VIDEO PROCESSING
# ============================================================================

def process_video():
    """
    Main video processing function
    
    Reads video file, processes frames to 128x64 binary format,
    and writes output file with header.
    
    Returns:
        bool: True if successful, False otherwise
    """
    print("=" * 70)
    print("BAD APPLE VIDEO PROCESSOR v2.1.0 (FIXED SSD1306 FORMAT)")
    print("Target: STM32L476RG + SSD1306 OLED (128x64)")
    print("=" * 70)
    print()
    print("IMPORTANT: This version uses VERTICAL PAGE format for SSD1306")
    print("  - Each byte = 8 VERTICAL pixels (not horizontal!)")
    print("  - Bit 0 = top of 8-pixel column")
    print("  - Buffer index: x + (y/8) * 128")
    print()
    
    # ========================================================================
    # VALIDATE INPUT
    # ========================================================================
    
    if not os.path.exists(VIDEO_FILE):
        print(f"ERROR: Video file not found: {VIDEO_FILE}")
        print("Looking in current directory:", os.getcwd())
        print("\nVideo files found:")
        found_videos = False
        for f in os.listdir("."):
            if f.lower().endswith(('.mp4', '.avi', '.mov', '.mkv', '.webm')):
                print(f"  - {f}")
                found_videos = True
        if not found_videos:
            print("  (none)")
        return False
    
    # Create output directories
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)
    
    # ========================================================================
    # OPEN VIDEO
    # ========================================================================
    
    cap = cv2.VideoCapture(VIDEO_FILE)
    if not cap.isOpened():
        print(f"ERROR: Cannot open video file: {VIDEO_FILE}")
        print("This may be due to:")
        print("  - Corrupted video file")
        print("  - Missing codec support")
        print("  - Insufficient permissions")
        return False
    
    # ========================================================================
    # GET VIDEO PROPERTIES
    # ========================================================================
    
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    video_fps = cap.get(cv2.CAP_PROP_FPS)
    duration = total_frames / video_fps if video_fps > 0 else 0
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    
    print(f"[INPUT] Input Video Information:")
    print(f"  File:       {VIDEO_FILE}")
    print(f"  Resolution: {width}x{height}")
    print(f"  Frame rate: {video_fps:.2f} fps")
    print(f"  Duration:   {int(duration//60)}:{int(duration%60):02d}")
    print(f"  Total:      {total_frames:,} frames")
    print()
    
    # ========================================================================
    # CALCULATE PROCESSING PARAMETERS
    # ========================================================================
    
    frame_skip = max(1, round(video_fps / TARGET_FPS))
    expected_frames = total_frames // frame_skip
    expected_duration = expected_frames / TARGET_FPS
    
    print(f"[OUTPUT] Output Video Configuration:")
    print(f"  Resolution: {OLED_WIDTH}x{OLED_HEIGHT}")
    print(f"  Frame rate: {TARGET_FPS} fps")
    print(f"  Frame skip: every {frame_skip} frame(s)")
    print(f"  Expected:   {expected_frames:,} frames")
    print(f"  Duration:   {int(expected_duration//60)}:{int(expected_duration%60):02d}")
    print(f"  Format:     SSD1306 vertical page (8 pages x 128 columns)")
    print()
    
    # ========================================================================
    # ESTIMATE OUTPUT SIZE
    # ========================================================================
    
    estimated_size = expected_frames * FRAMEBUFFER_SIZE + 4
    
    print(f"[SIZE] Output File Information:")
    print(f"  Frame size:     {FRAMEBUFFER_SIZE} bytes (8 pages x 128 cols)")
    print(f"  Estimated size: {estimated_size:,} bytes ({estimated_size/1024:.1f} KB)")
    print()
    
    # ========================================================================
    # PROCESS FRAMES
    # ========================================================================
    
    frames_data = []
    frame_idx = 0
    processed_count = 0
    preview_saved = 0
    verified_count = 0
    
    print("Processing frames...")
    print("Progress: [", end="", flush=True)
    progress_width = 50
    last_progress_percent = -1
    
    while cap.isOpened():
        ret, frame = cap.read()
        if not ret:
            break
        
        # Skip frames to achieve target FPS
        if frame_idx % frame_skip == 0:
            # Convert to grayscale
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            
            # Resize to OLED resolution with good quality
            resized = cv2.resize(gray, (OLED_WIDTH, OLED_HEIGHT), 
                               interpolation=cv2.INTER_AREA)
            
            # Enhance contrast and brightness
            if CONTRAST_BOOST != 1.0 or BRIGHTNESS_OFFSET != 0:
                resized = enhance_contrast(resized, CONTRAST_BOOST, BRIGHTNESS_OFFSET)
            
            # Convert to SSD1306 format (FIXED: vertical page format)
            frame_bytes = frame_to_ssd1306_format(resized)
            frames_data.append(frame_bytes)
            
            # Verify first few frames
            if processed_count < 5:
                _, binary = cv2.threshold(resized, THRESHOLD, 1, cv2.THRESH_BINARY)
                if INVERT:
                    binary = 1 - binary
                if verify_ssd1306_format(frame_bytes, binary):
                    verified_count += 1
            
            # Save preview
            if SAVE_PREVIEW and preview_saved < MAX_PREVIEW_FRAMES and \
               processed_count >= START_PREVIEW_AT_FRAME:
                save_preview_image(resized, processed_count)
                preview_saved += 1
            
            processed_count += 1
            
            # Update progress bar
            if expected_frames > 0:
                progress_percent = int((processed_count / expected_frames) * progress_width)
                if progress_percent != last_progress_percent:
                    print("=" * (progress_percent - last_progress_percent), 
                          end="", flush=True)
                    last_progress_percent = progress_percent
        
        frame_idx += 1
    
    print("]")
    cap.release()
    
    print(f"\n[OK] Processed {len(frames_data)} frames")
    print(f"[OK] Verified {verified_count}/5 frames (format check)")
    
    if len(frames_data) == 0:
        print("ERROR: No frames processed!")
        return False
    
    if verified_count < 5:
        print("WARNING: Some frames failed format verification!")
    
    # ========================================================================
    # WRITE OUTPUT FILE
    # ========================================================================
    
    print(f"\n[WRITE] Writing to {OUTPUT_FILE}...")
    
    with open(OUTPUT_FILE, 'wb') as f:
        # Write header: frame count (4 bytes, little-endian)
        f.write(struct.pack('<I', len(frames_data)))
        
        # Write all frame data
        for frame_data in frames_data:
            f.write(frame_data)
    
    # ========================================================================
    # FINAL STATISTICS
    # ========================================================================
    
    actual_size = os.path.getsize(OUTPUT_FILE)
    actual_duration = len(frames_data) / TARGET_FPS
    bitrate_kbps = (actual_size * 8) / actual_duration / 1000
    
    print()
    print("=" * 70)
    print("[SUCCESS] PROCESSING COMPLETE!")
    print("=" * 70)
    print(f"Output file:  {OUTPUT_FILE}")
    print(f"Frames:       {len(frames_data)}")
    print(f"Frame size:   {len(frames_data[0])} bytes")
    print(f"Total size:   {actual_size:,} bytes ({actual_size/1024:.1f} KB)")
    print(f"Duration:     {int(actual_duration//60)}:{int(actual_duration%60):02d}")
    print(f"Bitrate:      {bitrate_kbps:.1f} kbps")
    print(f"Actual FPS:   {len(frames_data)/actual_duration:.1f}")
    print(f"Format:       SSD1306 vertical page (FIXED)")
    
    if SAVE_PREVIEW:
        print(f"\n[PREVIEW] Preview images: {preview_saved} files saved in {PREVIEW_DIR}/")
    
    print("\n[NEXT] Next steps:")
    print("  1. Review preview images to verify quality")
    print("  2. Run: python process_audio.py")
    print("  3. Run: python combine_files.py")
    print("  4. Copy badapple.bin to SD card root directory")
    print()
    
    return True


# ============================================================================
# MAIN ENTRY POINT
# ============================================================================

if __name__ == "__main__":
    try:
        success = process_video()
        if success:
            print("[OK] Video processing successful!")
            sys.exit(0)
        else:
            print("[ERROR] Video processing failed!")
            sys.exit(1)
    except KeyboardInterrupt:
        print("\n\n[WARNING] Processing interrupted by user")
        sys.exit(130)
    except Exception as e:
        print(f"\n[ERROR] FATAL ERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

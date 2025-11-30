#!/usr/bin/env python3
"""
Bad Apple Audio Processor for STM32L476RG
Extracts audio as 16-bit stereo PCM at 32 kHz for DAC playback

Target System:
- NUCLEO-L476RG @ 80 MHz
- Dual DAC (PA4/PA5) with DMA circular buffers
- Target: 32 kHz sampling rate, 16-bit stereo
- Triple buffering, 512 samples per buffer

Author: David Leathers
Date: November 2025
Version: 2.0.0
"""

import subprocess
import os
import sys
import struct

# ============================================================================
# CONFIGURATION
# ============================================================================

# Input/Output
VIDEO_FILE = "BadApple.mp4"
OUTPUT_FILE = "output/badapple_audio.raw"

# Audio settings (matches STM32 hardware)
SAMPLE_RATE = 32000      # 32 kHz (matches AUDIO_SAMPLE_RATE in buffers.h)
BITS_PER_SAMPLE = 16     # 16-bit signed
CHANNELS = 2             # Stereo (matches dual DAC hardware)

# STM32 buffer configuration (for reference)
STM32_BUFFER_SAMPLES = 512  # Audio buffer size
STM32_BUFFER_MS = (STM32_BUFFER_SAMPLES * 1000) / SAMPLE_RATE  # ~16 ms

# ============================================================================
# AUDIO METADATA DETECTION
# ============================================================================

def detect_audio_channels(video_file):
    """
    Detect if source audio is mono or stereo using ffprobe
    
    Args:
        video_file: Path to video file
    
    Returns:
        int: Number of channels (1=mono, 2=stereo), or None if detection fails
    """
    try:
        cmd = [
            'ffprobe',
            '-v', 'error',
            '-select_streams', 'a:0',
            '-show_entries', 'stream=channels',
            '-of', 'default=noprint_wrappers=1:nokey=1',
            video_file
        ]
        
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        
        if result.returncode == 0 and result.stdout.strip():
            channels = int(result.stdout.strip())
            return channels
        
        return None
        
    except (subprocess.TimeoutExpired, ValueError, FileNotFoundError):
        return None


def get_audio_duration(video_file):
    """
    Get audio duration in seconds using ffprobe
    
    Args:
        video_file: Path to video file
    
    Returns:
        float: Duration in seconds, or None if detection fails
    """
    try:
        cmd = [
            'ffprobe',
            '-v', 'error',
            '-select_streams', 'a:0',
            '-show_entries', 'stream=duration',
            '-of', 'default=noprint_wrappers=1:nokey=1',
            video_file
        ]
        
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        
        if result.returncode == 0 and result.stdout.strip():
            duration = float(result.stdout.strip())
            return duration
        
        return None
        
    except (subprocess.TimeoutExpired, ValueError, FileNotFoundError):
        return None


# ============================================================================
# AUDIO PROCESSING
# ============================================================================

def process_audio():
    """
    Extract and convert audio to 16-bit stereo PCM at 32 kHz
    
    Automatically detects if source is mono or stereo and handles conversion.
    For mono sources, duplicates the channel to create stereo.
    
    Returns:
        bool: True if successful, False otherwise
    """
    print("=" * 70)
    print("BAD APPLE AUDIO PROCESSOR v2.0.1")
    print("Target: STM32L476RG Dual DAC (16-bit Stereo @ 32 kHz)")
    print("=" * 70)
    print()
    
    # ========================================================================
    # VALIDATE INPUT
    # ========================================================================
    
    if not os.path.exists(VIDEO_FILE):
        print(f"ERROR: Video file not found: {VIDEO_FILE}")
        return False
    
    os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)
    
    # ========================================================================
    # DETECT AUDIO PROPERTIES
    # ========================================================================
    
    print(f"[INPUT] Input: {VIDEO_FILE}")
    print()
    
    source_channels = detect_audio_channels(VIDEO_FILE)
    duration = get_audio_duration(VIDEO_FILE)
    
    if source_channels:
        channel_str = "MONO" if source_channels == 1 else "STEREO"
        print(f"[DETECT] Source audio: {channel_str} ({source_channels} channel(s))")
        
        if source_channels == 1:
            print("         -> Will duplicate mono to stereo for dual DAC")
    else:
        print("[WARNING] Could not detect source channels (will attempt stereo extraction)")
        source_channels = 2  # Assume stereo
    
    if duration:
        print(f"[DETECT] Duration: {int(duration//60)}:{int(duration%60):02d}")
    
    print()
    
    # ========================================================================
    # CONFIGURE OUTPUT
    # ========================================================================
    
    print(f"[OUTPUT] Output Configuration:")
    print(f"  Format:      {BITS_PER_SAMPLE}-bit PCM")
    print(f"  Sample rate: {SAMPLE_RATE} Hz")
    print(f"  Channels:    {CHANNELS} (stereo)")
    print(f"  Byte order:  Little-endian")
    print(f"  Interleaved: L-R-L-R-L-R...")
    print()
    print(f"[BUFFER] STM32 Buffer Configuration:")
    print(f"  Buffer size: {STM32_BUFFER_SAMPLES} samples")
    print(f"  Buffer time: {STM32_BUFFER_MS:.1f} ms")
    print(f"  Buffers:     3 (triple buffering)")
    print()
    
    # ========================================================================
    # BUILD FFMPEG COMMAND
    # ========================================================================
    
    # Base command
    cmd = [
        'ffmpeg',
        '-i', VIDEO_FILE,
        '-ar', str(SAMPLE_RATE),     # Sample rate
        '-ac', str(CHANNELS),         # Force to stereo (auto-converts mono)
        '-f', 's16le',                # Format: signed 16-bit little-endian
        '-acodec', 'pcm_s16le',       # Codec: PCM signed 16-bit LE
        '-y',                         # Overwrite output
        OUTPUT_FILE
    ]
    
    print("[FFMPEG] Running FFmpeg...")
    print("Command: " + " ".join(cmd))
    print()
    
    # ========================================================================
    # EXECUTE FFMPEG
    # ========================================================================
    
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=300  # 5 minute timeout
        )
        
        if result.returncode != 0:
            print("ERROR: FFmpeg failed!")
            print("\nFFmpeg stderr:")
            print(result.stderr)
            return False
        
    except subprocess.TimeoutExpired:
        print("ERROR: FFmpeg timed out (>5 minutes)")
        return False
    except FileNotFoundError:
        print("ERROR: FFmpeg not found!")
        print("\nPlease install FFmpeg:")
        print("  - Windows: Download from https://ffmpeg.org/download.html")
        print("  - macOS:   brew install ffmpeg")
        print("  - Linux:   sudo apt install ffmpeg")
        return False
    
    # ========================================================================
    # VALIDATE OUTPUT
    # ========================================================================
    
    if not os.path.exists(OUTPUT_FILE):
        print("ERROR: Output file not created!")
        return False
    
    file_size = os.path.getsize(OUTPUT_FILE)
    
    if file_size == 0:
        print("ERROR: Output file is empty!")
        return False
    
    # Calculate statistics
    bytes_per_sample = (BITS_PER_SAMPLE // 8) * CHANNELS
    total_samples = file_size // bytes_per_sample
    duration_sec = total_samples / SAMPLE_RATE
    
    print("=" * 70)
    print("[SUCCESS] AUDIO PROCESSING COMPLETE!")
    print("=" * 70)
    print(f"Output file:  {OUTPUT_FILE}")
    print(f"File size:    {file_size:,} bytes ({file_size/1024/1024:.2f} MB)")
    print(f"Total samples:{total_samples:,} sample pairs")
    print(f"Duration:     {int(duration_sec//60)}:{int(duration_sec%60):02d}")
    print(f"Sample rate:  {SAMPLE_RATE} Hz")
    print(f"Bit depth:    {BITS_PER_SAMPLE} bits")
    print(f"Channels:     {CHANNELS} (stereo)")
    print(f"Data rate:    {SAMPLE_RATE * bytes_per_sample / 1024:.1f} KB/s")
    print()
    
    # ========================================================================
    # VERIFY DATA INTEGRITY
    # ========================================================================
    
    # Check for silence (all zeros)
    with open(OUTPUT_FILE, 'rb') as f:
        # Read first 1KB and last 1KB
        first_kb = f.read(1024)
        f.seek(-1024, 2)
        last_kb = f.read(1024)
    
    if all(b == 0 for b in first_kb):
        print("[WARNING] First 1KB is silent (all zeros)")
    if all(b == 0 for b in last_kb):
        print("[WARNING] Last 1KB is silent (all zeros)")
    
    # Calculate expected size
    if duration:
        expected_samples = int(duration * SAMPLE_RATE)
        expected_size = expected_samples * bytes_per_sample
        size_diff_percent = abs(file_size - expected_size) / expected_size * 100
        
        if size_diff_percent > 5:
            print(f"[WARNING] File size differs from expected by {size_diff_percent:.1f}%")
            print(f"  Expected: {expected_size:,} bytes")
            print(f"  Actual:   {file_size:,} bytes")
    
    print()
    print("[NEXT] Next steps:")
    print("  1. Verify audio quality (optional: play with audacity)")
    print("  2. Run: python combine_files.py")
    print("  3. Copy badapple.bin to SD card root directory")
    print()
    
    return True


# ============================================================================
# MAIN ENTRY POINT
# ============================================================================

if __name__ == "__main__":
    try:
        success = process_audio()
        if success:
            print("[OK] Audio processing successful!")
            sys.exit(0)
        else:
            print("[ERROR] Audio processing failed!")
            sys.exit(1)
    except KeyboardInterrupt:
        print("\n\n[WARNING] Processing interrupted by user")
        sys.exit(130)
    except Exception as e:
        print(f"\n[ERROR] FATAL ERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

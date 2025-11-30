#!/usr/bin/env python3
"""
Bad Apple File Combiner for STM32L476RG
Combines video and audio into single binary file for SD card

File Format v2.0:
+------------------------------------------------------------+
| HEADER (20 bytes)                                          |
+------------------------------------------------------------+
| Offset  0: Frame count        (4 bytes uint32 LE)          |
| Offset  4: Audio size (bytes) (4 bytes uint32 LE)          |
| Offset  8: Sample rate (Hz)   (4 bytes uint32 LE)          |
| Offset 12: Channels           (4 bytes uint32 LE)          |
| Offset 16: Bits per sample    (4 bytes uint32 LE)          |
+------------------------------------------------------------+
| VIDEO DATA (frame_count x 1024 bytes)                      |
+------------------------------------------------------------+
| AUDIO DATA (interleaved stereo, int16_t)                   |
|   Format: [L0][R0][L1][R1]...[Ln][Rn]                      |
+------------------------------------------------------------+

Author: David Leathers
Date: November 2025
Version: 2.0.0
"""

import struct
import os
import sys

# ============================================================================
# CONFIGURATION
# ============================================================================

VIDEO_FILE = "output/badapple_video.bin"
AUDIO_FILE = "output/badapple_audio.raw"
OUTPUT_FILE = "output/badapple.bin"

# Audio parameters (must match process_audio.py)
SAMPLE_RATE = 32000      # 32 kHz
CHANNELS = 2             # Stereo
BITS_PER_SAMPLE = 16     # 16-bit

# Video parameters (must match process_video.py)
VIDEO_FPS = 30           # 30 FPS target

# File format version
FORMAT_VERSION = 2       # Version 2.0 with stereo support

# ============================================================================
# HEADER STRUCTURE
# ============================================================================

HEADER_SIZE = 20         # Total header size in bytes
FRAMEBUFFER_SIZE = 1024  # Each video frame is 1024 bytes

# Header field offsets
OFFSET_FRAME_COUNT = 0
OFFSET_AUDIO_SIZE = 4
OFFSET_SAMPLE_RATE = 8
OFFSET_CHANNELS = 12
OFFSET_BITS_PER_SAMPLE = 16

# ============================================================================
# VALIDATION
# ============================================================================

def validate_video_file(video_data):
    """
    Validate video file structure
    
    Args:
        video_data: Raw video file bytes
    
    Returns:
        tuple: (is_valid, frame_count, error_message)
    """
    if len(video_data) < 4:
        return False, 0, "Video file too small (< 4 bytes)"
    
    # Parse frame count from header
    frame_count = struct.unpack('<I', video_data[0:4])[0]
    
    if frame_count == 0:
        return False, 0, "Frame count is zero"
    
    if frame_count > 10000:
        return False, 0, f"Frame count suspiciously high ({frame_count})"
    
    # Check file size matches expected
    expected_size = 4 + (frame_count * FRAMEBUFFER_SIZE)
    actual_size = len(video_data)
    
    if actual_size != expected_size:
        return False, frame_count, \
               f"Size mismatch: expected {expected_size}, got {actual_size}"
    
    return True, frame_count, None


def validate_audio_file(audio_data):
    """
    Validate audio file structure
    
    Args:
        audio_data: Raw audio file bytes
    
    Returns:
        tuple: (is_valid, error_message)
    """
    bytes_per_sample = (BITS_PER_SAMPLE // 8) * CHANNELS
    
    if len(audio_data) == 0:
        return False, "Audio file is empty"
    
    if len(audio_data) % bytes_per_sample != 0:
        return False, f"Audio size not aligned to sample boundary " \
                      f"(size={len(audio_data)}, bytes/sample={bytes_per_sample})"
    
    # Check for all zeros (silence)
    if all(b == 0 for b in audio_data[:1024]):
        print("[WARNING] First 1KB of audio is silent")
    
    return True, None


# ============================================================================
# FILE COMBINATION
# ============================================================================

def combine_files():
    """
    Combine video and audio into single binary file
    
    Returns:
        bool: True if successful, False otherwise
    """
    print("=" * 70)
    print("BAD APPLE FILE COMBINER v2.0.1")
    print("Creating final SD card file with stereo audio")
    print("=" * 70)
    print()
    
    # ========================================================================
    # CHECK INPUT FILES
    # ========================================================================
    
    if not os.path.exists(VIDEO_FILE):
        print(f"ERROR: Video file not found: {VIDEO_FILE}")
        print("\nPlease run: python process_video.py")
        return False
    
    if not os.path.exists(AUDIO_FILE):
        print(f"ERROR: Audio file not found: {AUDIO_FILE}")
        print("\nPlease run: python process_audio.py")
        return False
    
    # ========================================================================
    # READ VIDEO FILE
    # ========================================================================
    
    print(f"[VIDEO] Reading: {VIDEO_FILE}")
    
    with open(VIDEO_FILE, 'rb') as f:
        video_data = f.read()
    
    # Validate video
    valid, frame_count, error = validate_video_file(video_data)
    if not valid:
        print(f"ERROR: Invalid video file - {error}")
        return False
    
    # Extract video frames (skip 4-byte header)
    video_frames = video_data[4:]
    
    print(f"  Frames:      {frame_count}")
    print(f"  Frame size:  {FRAMEBUFFER_SIZE} bytes")
    print(f"  Video size:  {len(video_frames):,} bytes ({len(video_frames)/1024:.1f} KB)")
    
    # Calculate video duration
    video_duration = frame_count / VIDEO_FPS
    print(f"  Duration:    {int(video_duration//60)}:{int(video_duration%60):02d} @ {VIDEO_FPS} fps")
    
    # ========================================================================
    # READ AUDIO FILE
    # ========================================================================
    
    print()
    print(f"[AUDIO] Reading: {AUDIO_FILE}")
    
    with open(AUDIO_FILE, 'rb') as f:
        audio_data = f.read()
    
    # Validate audio
    valid, error = validate_audio_file(audio_data)
    if not valid:
        print(f"ERROR: Invalid audio file - {error}")
        return False
    
    # Calculate audio statistics
    bytes_per_sample = (BITS_PER_SAMPLE // 8) * CHANNELS
    total_samples = len(audio_data) // bytes_per_sample
    audio_duration = total_samples / SAMPLE_RATE
    data_rate = SAMPLE_RATE * bytes_per_sample / 1024
    
    print(f"  Size:        {len(audio_data):,} bytes ({len(audio_data)/1024/1024:.2f} MB)")
    print(f"  Samples:     {total_samples:,} sample pairs")
    print(f"  Duration:    {int(audio_duration//60)}:{int(audio_duration%60):02d}")
    print(f"  Sample rate: {SAMPLE_RATE} Hz")
    print(f"  Channels:    {CHANNELS} (stereo)")
    print(f"  Bit depth:   {BITS_PER_SAMPLE} bits")
    print(f"  Data rate:   {data_rate:.1f} KB/s")
    
    # ========================================================================
    # CHECK AUDIO/VIDEO SYNC
    # ========================================================================
    
    print()
    print(f"[SYNC] Synchronization Check:")
    
    duration_diff = abs(video_duration - audio_duration)
    
    print(f"  Video:       {video_duration:.2f} seconds")
    print(f"  Audio:       {audio_duration:.2f} seconds")
    print(f"  Difference:  {duration_diff:.2f} seconds")
    
    if duration_diff > 0.5:
        print(f"  [WARNING] Video/audio duration differs by {duration_diff:.2f}s")
        print(f"            This may cause sync issues!")
    elif duration_diff > 0.1:
        print(f"  [WARNING] Minor difference of {duration_diff:.2f}s (may be acceptable)")
    else:
        print(f"  [OK] Good sync (difference < 0.1s)")
    
    # ========================================================================
    # CREATE COMBINED FILE
    # ========================================================================
    
    print()
    print(f"[OUTPUT] Creating combined file: {OUTPUT_FILE}")
    
    with open(OUTPUT_FILE, 'wb') as f:
        # Write header (20 bytes total)
        f.write(struct.pack('<I', frame_count))        # Frame count
        f.write(struct.pack('<I', len(audio_data)))    # Audio size in bytes
        f.write(struct.pack('<I', SAMPLE_RATE))        # Sample rate
        f.write(struct.pack('<I', CHANNELS))           # Number of channels
        f.write(struct.pack('<I', BITS_PER_SAMPLE))    # Bits per sample
        
        # Write video data
        f.write(video_frames)
        
        # Write audio data (interleaved stereo: L-R-L-R...)
        f.write(audio_data)
    
    # ========================================================================
    # FINAL STATISTICS
    # ========================================================================
    
    total_size = os.path.getsize(OUTPUT_FILE)
    
    print()
    print("=" * 70)
    print("[SUCCESS] COMBINATION COMPLETE!")
    print("=" * 70)
    print(f"Output file:  {OUTPUT_FILE}")
    print(f"Total size:   {total_size:,} bytes ({total_size/1024/1024:.2f} MB)")
    print()
    print("File Structure:")
    print(f"  Header:     {HEADER_SIZE} bytes (format v{FORMAT_VERSION})")
    print(f"  Video:      {len(video_frames):,} bytes ({frame_count} frames)")
    print(f"  Audio:      {len(audio_data):,} bytes ({total_samples:,} samples)")
    print()
    
    # Calculate SD card performance requirements
    total_duration = max(video_duration, audio_duration)
    avg_bitrate = (total_size * 8) / total_duration / 1000  # kbps
    sd_read_rate = total_size / total_duration / 1024       # KB/s
    
    print("Playback Requirements:")
    print(f"  Average bitrate: {avg_bitrate:.1f} kbps")
    print(f"  SD read rate:    {sd_read_rate:.1f} KB/s")
    print(f"  Video bandwidth: {VIDEO_FPS * FRAMEBUFFER_SIZE / 1024:.1f} KB/s")
    print(f"  Audio bandwidth: {data_rate:.1f} KB/s")
    
    if sd_read_rate > 400:
        print(f"  [WARNING] High SD read rate ({sd_read_rate:.1f} KB/s)")
        print(f"            May require Class 10 or UHS-I card")
    else:
        print(f"  [OK] SD read rate is acceptable for Class 4+ cards")
    
    print()
    print("Next step:")
    print(f"  -> Copy {OUTPUT_FILE} to SD card root directory")
    print(f"  -> Insert SD card into STM32 board")
    print(f"  -> Power on and enjoy!")
    print()
    
    return True


# ============================================================================
# MAIN ENTRY POINT
# ============================================================================

if __name__ == "__main__":
    try:
        success = combine_files()
        if success:
            print("[OK] File combination successful!")
            sys.exit(0)
        else:
            print("[ERROR] File combination failed!")
            sys.exit(1)
    except KeyboardInterrupt:
        print("\n\n[WARNING] Processing interrupted by user")
        sys.exit(130)
    except Exception as e:
        print(f"\n[ERROR] FATAL ERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

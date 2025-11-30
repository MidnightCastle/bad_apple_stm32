#!/usr/bin/env python3
"""
Bad Apple File Analyzer
Analyzes and validates the generated media files

Author: David Leathers
Date: November 2025
Version: 2.0.0
"""

import struct
import os
import sys

# ============================================================================
# CONSTANTS
# ============================================================================

HEADER_SIZE = 20
FRAME_SIZE = 1024

# ============================================================================
# ANALYSIS FUNCTIONS
# ============================================================================

def analyze_header(filename):
    """
    Parse and display file header information
    
    Args:
        filename: Path to .bin file
    
    Returns:
        dict: Header fields, or None if invalid
    """
    if not os.path.exists(filename):
        print(f"ERROR: File not found: {filename}")
        return None
    
    file_size = os.path.getsize(filename)
    
    if file_size < HEADER_SIZE:
        print(f"ERROR: File too small ({file_size} bytes, need at least {HEADER_SIZE})")
        return None
    
    with open(filename, 'rb') as f:
        header_data = f.read(HEADER_SIZE)
    
    # Unpack header (little-endian)
    frame_count, audio_size, sample_rate, channels, bits_per_sample = \
        struct.unpack('<5I', header_data)
    
    return {
        'frame_count': frame_count,
        'audio_size': audio_size,
        'sample_rate': sample_rate,
        'channels': channels,
        'bits_per_sample': bits_per_sample,
        'file_size': file_size
    }


def validate_file(filename):
    """
    Validate file structure and consistency
    
    Args:
        filename: Path to .bin file
    
    Returns:
        tuple: (is_valid, warnings, errors)
    """
    warnings = []
    errors = []
    
    # Parse header
    header = analyze_header(filename)
    if header is None:
        return False, warnings, ["Could not read header"]
    
    # Extract values
    frame_count = header['frame_count']
    audio_size = header['audio_size']
    sample_rate = header['sample_rate']
    channels = header['channels']
    bits_per_sample = header['bits_per_sample']
    file_size = header['file_size']
    
    # Validate frame count
    if frame_count == 0:
        errors.append("Frame count is zero")
    elif frame_count > 10000:
        warnings.append(f"Frame count very high ({frame_count})")
    
    # Validate sample rate
    if sample_rate == 0:
        errors.append("Sample rate is zero")
    elif sample_rate not in [8000, 11025, 16000, 22050, 32000, 44100, 48000]:
        warnings.append(f"Non-standard sample rate ({sample_rate} Hz)")
    
    # Validate channels
    if channels not in [1, 2]:
        errors.append(f"Invalid channel count ({channels})")
    
    # Validate bit depth
    if bits_per_sample not in [8, 16, 24, 32]:
        errors.append(f"Invalid bit depth ({bits_per_sample})")
    
    # Validate audio size alignment
    if channels in [1, 2] and bits_per_sample in [8, 16, 24, 32]:
        bytes_per_sample = (bits_per_sample // 8) * channels
        if audio_size % bytes_per_sample != 0:
            errors.append(f"Audio size not aligned to sample boundary "
                         f"({audio_size} % {bytes_per_sample} != 0)")
    
    # Validate file size
    expected_size = HEADER_SIZE + (frame_count * FRAME_SIZE) + audio_size
    if file_size != expected_size:
        errors.append(f"File size mismatch: expected {expected_size:,}, got {file_size:,}")
    
    # Calculate durations
    video_duration = frame_count / 30.0  # Assuming 30 FPS
    audio_samples = audio_size // ((bits_per_sample // 8) * channels)
    audio_duration = audio_samples / sample_rate if sample_rate > 0 else 0
    
    duration_diff = abs(video_duration - audio_duration)
    if duration_diff > 0.5:
        warnings.append(f"Video/audio duration differs by {duration_diff:.2f}s")
    
    # Check for data sections
    video_size = frame_count * FRAME_SIZE
    if HEADER_SIZE + video_size > file_size:
        errors.append("Video data extends beyond file")
    if HEADER_SIZE + video_size + audio_size > file_size:
        errors.append("Audio data extends beyond file")
    
    is_valid = len(errors) == 0
    return is_valid, warnings, errors


def sample_frames(filename, num_samples=5):
    """
    Sample and analyze video frames
    
    Args:
        filename: Path to .bin file
        num_samples: Number of frames to sample
    
    Returns:
        list: Statistics for each sampled frame
    """
    header = analyze_header(filename)
    if header is None:
        return []
    
    frame_count = header['frame_count']
    
    # Choose frames to sample (evenly distributed)
    frame_indices = [int(i * frame_count / num_samples) 
                     for i in range(num_samples)]
    
    results = []
    
    with open(filename, 'rb') as f:
        for idx in frame_indices:
            offset = HEADER_SIZE + (idx * FRAME_SIZE)
            f.seek(offset)
            frame_data = f.read(FRAME_SIZE)
            
            if len(frame_data) != FRAME_SIZE:
                continue
            
            # Calculate statistics
            total_pixels = FRAME_SIZE * 8
            white_pixels = sum(bin(byte).count('1') for byte in frame_data)
            black_pixels = total_pixels - white_pixels
            
            # Check for all black or all white
            all_black = white_pixels == 0
            all_white = black_pixels == 0
            
            results.append({
                'frame': idx,
                'white_pixels': white_pixels,
                'black_pixels': black_pixels,
                'white_percent': (white_pixels / total_pixels) * 100,
                'all_black': all_black,
                'all_white': all_white
            })
    
    return results


def sample_audio(filename, num_samples=5):
    """
    Sample and analyze audio data
    
    Args:
        filename: Path to .bin file
        num_samples: Number of positions to sample
    
    Returns:
        list: Statistics for each sampled position
    """
    header = analyze_header(filename)
    if header is None:
        return []
    
    audio_size = header['audio_size']
    channels = header['channels']
    bits_per_sample = header['bits_per_sample']
    
    # Calculate audio section offset
    video_size = header['frame_count'] * FRAME_SIZE
    audio_offset = HEADER_SIZE + video_size
    
    # Sample positions (evenly distributed)
    bytes_per_sample = (bits_per_sample // 8) * channels
    total_samples = audio_size // bytes_per_sample
    
    sample_indices = [int(i * total_samples / num_samples) 
                      for i in range(num_samples)]
    
    results = []
    
    with open(filename, 'rb') as f:
        for idx in sample_indices:
            offset = audio_offset + (idx * bytes_per_sample)
            f.seek(offset)
            sample_data = f.read(bytes_per_sample)
            
            if len(sample_data) != bytes_per_sample:
                continue
            
            # Parse samples based on format
            if bits_per_sample == 16 and channels == 2:
                # 16-bit stereo
                left = struct.unpack('<h', sample_data[0:2])[0]
                right = struct.unpack('<h', sample_data[2:4])[0]
                
                results.append({
                    'sample': idx,
                    'left': left,
                    'right': right,
                    'left_percent': (left / 32768.0) * 100,
                    'right_percent': (right / 32768.0) * 100
                })
    
    return results


# ============================================================================
# MAIN ANALYSIS
# ============================================================================

def analyze_file(filename):
    """
    Complete file analysis with detailed output
    
    Args:
        filename: Path to .bin file
    """
    print("=" * 70)
    print(f"BAD APPLE FILE ANALYZER v2.0.1")
    print(f"Analyzing: {filename}")
    print("=" * 70)
    print()
    
    # ========================================================================
    # HEADER ANALYSIS
    # ========================================================================
    
    header = analyze_header(filename)
    if header is None:
        return
    
    print("[HEADER] FILE HEADER")
    print("-" * 70)
    print(f"Frame count:      {header['frame_count']:,}")
    print(f"Audio size:       {header['audio_size']:,} bytes ({header['audio_size']/1024/1024:.2f} MB)")
    print(f"Sample rate:      {header['sample_rate']:,} Hz")
    print(f"Channels:         {header['channels']} ({'mono' if header['channels']==1 else 'stereo'})")
    print(f"Bits per sample:  {header['bits_per_sample']}")
    print(f"File size:        {header['file_size']:,} bytes ({header['file_size']/1024/1024:.2f} MB)")
    print()
    
    # ========================================================================
    # VALIDATION
    # ========================================================================
    
    print("[VALIDATION] VALIDATION")
    print("-" * 70)
    
    is_valid, warnings, errors = validate_file(filename)
    
    if errors:
        print("[ERROR] ERRORS:")
        for error in errors:
            print(f"  - {error}")
    else:
        print("[OK] No errors found")
    
    if warnings:
        print("\n[WARNING] WARNINGS:")
        for warning in warnings:
            print(f"  - {warning}")
    else:
        print("[OK] No warnings")
    
    print()
    
    # ========================================================================
    # CALCULATED VALUES
    # ========================================================================
    
    print("[STATS] CALCULATED VALUES")
    print("-" * 70)
    
    # Video
    video_size = header['frame_count'] * FRAME_SIZE
    video_duration = header['frame_count'] / 30.0
    video_fps = 30
    
    print(f"Video section:")
    print(f"  Size:        {video_size:,} bytes ({video_size/1024:.1f} KB)")
    print(f"  Duration:    {int(video_duration//60)}:{int(video_duration%60):02d}")
    print(f"  Frame rate:  {video_fps} FPS")
    
    # Audio
    bytes_per_sample = (header['bits_per_sample'] // 8) * header['channels']
    total_samples = header['audio_size'] // bytes_per_sample
    audio_duration = total_samples / header['sample_rate']
    data_rate = header['sample_rate'] * bytes_per_sample / 1024
    
    print(f"\nAudio section:")
    print(f"  Total samples:   {total_samples:,}")
    print(f"  Duration:        {int(audio_duration//60)}:{int(audio_duration%60):02d}")
    print(f"  Data rate:       {data_rate:.1f} KB/s")
    
    # Sync
    duration_diff = abs(video_duration - audio_duration)
    print(f"\nSynchronization:")
    print(f"  Duration diff:   {duration_diff:.3f} seconds")
    if duration_diff < 0.1:
        print(f"  Status:          [OK] Excellent")
    elif duration_diff < 0.5:
        print(f"  Status:          [OK] Good")
    else:
        print(f"  Status:          [WARNING] May have sync issues")
    
    print()
    
    # ========================================================================
    # FRAME SAMPLING
    # ========================================================================
    
    print("[FRAMES] VIDEO FRAME SAMPLING")
    print("-" * 70)
    
    frame_samples = sample_frames(filename, num_samples=5)
    
    if frame_samples:
        print(f"{'Frame':<10} {'White':<12} {'Black':<12} {'White %':<10} {'Status'}")
        print("-" * 70)
        
        for sample in frame_samples:
            status = ""
            if sample['all_black']:
                status = "[WARNING] All black"
            elif sample['all_white']:
                status = "[WARNING] All white"
            elif sample['white_percent'] < 5 or sample['white_percent'] > 95:
                status = "[WARNING] Very dark/bright"
            else:
                status = "[OK] Good"
            
            print(f"{sample['frame']:<10} "
                  f"{sample['white_pixels']:<12} "
                  f"{sample['black_pixels']:<12} "
                  f"{sample['white_percent']:<10.1f} "
                  f"{status}")
    else:
        print("Could not sample frames")
    
    print()
    
    # ========================================================================
    # AUDIO SAMPLING
    # ========================================================================
    
    print("[AUDIO] AUDIO SAMPLE ANALYSIS")
    print("-" * 70)
    
    audio_samples = sample_audio(filename, num_samples=5)
    
    if audio_samples:
        print(f"{'Sample':<12} {'Left':<10} {'Right':<10} {'Left %':<10} {'Right %':<10} {'Status'}")
        print("-" * 70)
        
        for sample in audio_samples:
            # Check for issues
            status = "[OK] Good"
            if sample['left'] == 0 and sample['right'] == 0:
                status = "[WARNING] Silent"
            elif abs(sample['left']) < 100 and abs(sample['right']) < 100:
                status = "[WARNING] Very quiet"
            elif abs(sample['left']) > 30000 or abs(sample['right']) > 30000:
                status = "[WARNING] Near clipping"
            
            print(f"{sample['sample']:<12} "
                  f"{sample['left']:<10} "
                  f"{sample['right']:<10} "
                  f"{sample['left_percent']:<10.1f} "
                  f"{sample['right_percent']:<10.1f} "
                  f"{status}")
    else:
        print("Could not sample audio")
    
    print()
    
    # ========================================================================
    # SUMMARY
    # ========================================================================
    
    print("=" * 70)
    if is_valid and not warnings:
        print("[SUCCESS] FILE ANALYSIS COMPLETE - NO ISSUES FOUND")
    elif is_valid:
        print("[WARNING] FILE ANALYSIS COMPLETE - WARNINGS PRESENT")
    else:
        print("[ERROR] FILE ANALYSIS COMPLETE - ERRORS FOUND")
    print("=" * 70)
    print()


# ============================================================================
# MAIN ENTRY POINT
# ============================================================================

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python analyze_file.py <filename.bin>")
        print()
        print("Example: python analyze_file.py output/badapple.bin")
        sys.exit(1)
    
    filename = sys.argv[1]
    
    try:
        analyze_file(filename)
        sys.exit(0)
    except KeyboardInterrupt:
        print("\n\nAnalysis interrupted by user")
        sys.exit(130)
    except Exception as e:
        print(f"\nFATAL ERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

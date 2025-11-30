#!/usr/bin/env python3
"""
Bad Apple - Complete Processing Pipeline
Runs all three processing steps in sequence

Author: David Leathers
Date: November 2025
Version: 2.0.0
"""

import subprocess
import sys
import os
import time

def print_header(title):
    """Print a formatted header"""
    print("\n" + "=" * 70)
    print(f"  {title}")
    print("=" * 70 + "\n")

def run_script(script_name, description):
    """
    Run a Python script and handle errors
    
    Args:
        script_name: Name of the script to run
        description: Description for display
    
    Returns:
        bool: True if successful, False otherwise
    """
    print_header(f"STEP: {description}")
    
    if not os.path.exists(script_name):
        print(f"ERROR: Script not found: {script_name}")
        return False
    
    try:
        result = subprocess.run(
            [sys.executable, script_name],
            check=True
        )
        return True
        
    except subprocess.CalledProcessError as e:
        print(f"\nERROR: {script_name} failed with exit code {e.returncode}")
        return False
    except KeyboardInterrupt:
        print(f"\n\nProcessing interrupted by user")
        return False
    except Exception as e:
        print(f"\nERROR: Unexpected error: {e}")
        return False

def main():
    """Run complete processing pipeline"""
    start_time = time.time()
    
    print_header("BAD APPLE - COMPLETE PROCESSING PIPELINE v2.0.1")
    
    print("This script will:")
    print("  1. Process video -> 30 FPS, 128x64 binary")
    print("  2. Process audio -> 32 kHz, 16-bit stereo")
    print("  3. Combine files -> Single .bin for SD card")
    print()
    
    input("Press ENTER to start processing... ")
    
    # Step 1: Process video
    if not run_script("process_video.py", "Video Processing (30 FPS)"):
        print("\n[ERROR] Pipeline failed at video processing")
        return False
    
    print("\n[OK] Video processing complete!")
    time.sleep(1)
    
    # Step 2: Process audio
    if not run_script("process_audio.py", "Audio Processing (32 kHz Stereo)"):
        print("\n[ERROR] Pipeline failed at audio processing")
        return False
    
    print("\n[OK] Audio processing complete!")
    time.sleep(1)
    
    # Step 3: Combine files
    if not run_script("combine_files.py", "File Combination"):
        print("\n[ERROR] Pipeline failed at file combination")
        return False
    
    print("\n[OK] File combination complete!")
    
    # Success!
    elapsed = time.time() - start_time
    
    print_header("[SUCCESS] PROCESSING COMPLETE!")
    
    print(f"Total time: {int(elapsed//60)}:{int(elapsed%60):02d}")
    print()
    print("Output file: output/badapple.bin")
    print()
    print("Next steps:")
    print("  1. Copy badapple.bin to SD card root directory")
    print("  2. Insert SD card into STM32 NUCLEO-L476RG")
    print("  3. Connect OLED display (I2C2: PB13/PB14)")
    print("  4. Connect audio output (DAC: PA4/PA5)")
    print("  5. Power on and enjoy!")
    print()
    
    return True

if __name__ == "__main__":
    try:
        success = main()
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print("\n\n[WARNING] Processing interrupted by user")
        sys.exit(130)
    except Exception as e:
        print(f"\n[ERROR] FATAL ERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

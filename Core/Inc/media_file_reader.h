/**
 * @file    media_file_reader.h
 * @brief   Bad Apple media file reader
 * @author  David Leathers
 * @date    November 2025
 * 
 * Reads custom binary format containing video frames and audio data.
 * 
 * File Format:
 *   - Header: 20 bytes
 *       [0-3]   frame_count (uint32_t LE)
 *       [4-7]   audio_size in bytes (uint32_t LE)
 *       [8-11]  sample_rate in Hz (uint32_t LE)
 *       [12-15] channels (uint32_t LE)
 *       [16-19] bits_per_sample (uint32_t LE)
 *   - Video: frame_count * 1024 bytes (1024 bytes per frame)
 *   - Audio: audio_size bytes (16-bit stereo interleaved PCM)
 * 
 * Usage:
 *   1. Find file with FAT_FindFile()
 *   2. Media_Open() with file info
 *   3. Media_ReadFrameAt() for video frames
 *   4. Media_ReadAudioStereo() for audio data
 *   5. Media_Close() when done
 */

#ifndef MEDIA_FILE_READER_H
#define MEDIA_FILE_READER_H

#include "fatfs.h"
#include "buffers.h"
#include <stdint.h>
#include <stdbool.h>

/* ========================== Configuration ========================== */

#define MEDIA_HEADER_SIZE       20      // Header size in bytes
#define MEDIA_FRAME_SIZE        1024    // Video frame size (128x64 / 8)
#define MEDIA_DEFAULT_VOLUME    50      // Default volume percentage (0-100)

/* ========================== Types ========================== */

typedef struct {
    // File metadata (from header)
    uint32_t frame_count;       // Total video frames
    uint32_t audio_size;        // Audio data size in bytes
    uint32_t sample_rate;       // Audio sample rate (Hz)
    uint32_t channels;          // Audio channels (1 or 2)
    uint32_t bits_per_sample;   // Bits per sample (typically 16)
    
    // File location
    uint32_t first_cluster;     // Starting cluster on SD
    uint32_t file_size;         // Total file size in bytes
    FAT_Volume *vol;            // Mounted volume
    
    // Calculated offsets
    uint32_t video_offset;      // Byte offset to video data
    uint32_t audio_offset;      // Byte offset to audio data
    
    // Playback position
    uint32_t current_frame;     // Current video frame index
    uint32_t current_sample;    // Current audio sample index
    
    // Playback settings
    uint8_t volume_percent;     // Volume 0-100
    
    // State
    bool is_open;               // File successfully opened
    
    // Cluster cache for sequential reads
    uint32_t cached_cluster;        // Last accessed cluster
    uint32_t cached_cluster_index;  // Index of cached cluster
    
    // Contiguous file optimization
    bool is_contiguous;         // File clusters are sequential
    uint32_t first_sector;      // First sector (if contiguous)
} MediaFile;

/* ========================== Core API ========================== */

/**
 * @brief Open media file for playback
 * @param media     Handle to initialize
 * @param vol       Mounted FAT volume
 * @param file_info File info from FAT_FindFile()
 * @return FAT_OK on success
 * 
 * Reads file header, calculates offsets, optionally enables
 * contiguous fast-path if file is not fragmented.
 */
FAT_Status Media_Open(MediaFile *media, FAT_Volume *vol, const FAT_FileInfo *file_info);

/**
 * @brief Close media file
 * @param media Handle
 */
void Media_Close(MediaFile *media);

/**
 * @brief Set playback volume
 * @param media   Handle
 * @param percent Volume percentage (0-100)
 */
void Media_SetVolume(MediaFile *media, uint8_t percent);

/* ========================== Video API ========================== */

/**
 * @brief Read video frame at specific index
 * @param media        Handle
 * @param frame_number Frame index (0-based)
 * @param buffer       Destination buffer (must be MEDIA_FRAME_SIZE bytes)
 * @return FAT_OK on success
 */
FAT_Status Media_ReadFrameAt(MediaFile *media, uint32_t frame_number, uint8_t *buffer);

/* ========================== Audio API ========================== */

/**
 * @brief Read stereo audio samples
 * @param media Handle
 * @param left  Left channel buffer (12-bit unsigned for DAC)
 * @param right Right channel buffer (12-bit unsigned for DAC)
 * @param count Number of samples to read (max AUDIO_BUFFER_SAMPLES)
 * @return FAT_OK on success
 * 
 * Reads interleaved 16-bit signed PCM, converts to 12-bit unsigned,
 * applies volume scaling, and deinterleaves to separate L/R buffers.
 * 
 * If end of audio is reached, remaining samples are filled with silence.
 */
FAT_Status Media_ReadAudioStereo(MediaFile *media, uint16_t *left, uint16_t *right, uint32_t count);

/* ========================== Query API ========================== */

/**
 * @brief Check if file clusters are contiguous
 * @param media Handle
 * @return true if contiguous (fast-path enabled)
 */
static inline bool Media_IsContiguous(const MediaFile *media) {
    return media && media->is_contiguous;
}

/**
 * @brief Get total duration in seconds
 * @param media Handle
 * @param fps   Frames per second
 * @return Duration in seconds
 */
static inline uint32_t Media_GetDurationSeconds(const MediaFile *media, uint32_t fps) {
    if (!media || fps == 0) return 0;
    return media->frame_count / fps;
}

/**
 * @brief Get audio sample count
 * @param media Handle
 * @return Total stereo samples (audio_size / 4 for 16-bit stereo)
 */
static inline uint32_t Media_GetSampleCount(const MediaFile *media) {
    if (!media) return 0;
    return media->audio_size / 4;  // 4 bytes per stereo sample
}

#endif // MEDIA_FILE_READER_H

/**
 * @file    media_file_reader.c
 * @brief   Bad Apple media file reader implementation
 * @author  David Leathers
 * @date    November 2025
 */

#include "media_file_reader.h"
#include "sd_card.h"
#include <string.h>

/* ========================== Private Constants ========================== */

// Maximum samples per read (matches audio buffer size)
#define MAX_AUDIO_READ_SAMPLES  AUDIO_BUFFER_SAMPLES

// DAC midpoint for silence (12-bit)
#define DAC_SILENCE             2048

// Multi-block read limit
#define MAX_MULTIBLOCK_COUNT    16

/* ========================== Private Data ========================== */

// Static buffer for bulk audio reads (stereo interleaved)
static int16_t s_audio_buffer[MAX_AUDIO_READ_SAMPLES * 2] __attribute__((aligned(4)));

/* ========================== Private Helpers ========================== */

/**
 * @brief Read 32-bit little-endian value from buffer
 */
static inline uint32_t Read32LE(const uint8_t *buf) {
    return (uint32_t)buf[0] |
           ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[3] << 24);
}

/**
 * @brief Get cluster containing byte offset (with caching)
 */
static uint32_t Media_GetClusterAt(MediaFile *media, uint32_t byte_offset) {
    uint32_t cluster_size = FAT_GetClusterSize(media->vol);
    if (cluster_size == 0) return 0;
    
    uint32_t target_index = byte_offset / cluster_size;
    
    // Try to resume from cache
    uint32_t cluster;
    uint32_t start_index;
    
    if (media->cached_cluster != 0 && media->cached_cluster_index <= target_index) {
        cluster = media->cached_cluster;
        start_index = media->cached_cluster_index;
    } else {
        cluster = media->first_cluster;
        start_index = 0;
    }
    
    // Walk cluster chain to target
    for (uint32_t i = start_index; i < target_index && !FAT_IsEndOfChain(cluster); i++) {
        cluster = FAT_GetNextCluster(media->vol, cluster);
    }
    
    // Update cache
    media->cached_cluster = cluster;
    media->cached_cluster_index = target_index;
    
    return cluster;
}

/**
 * @brief Read data at arbitrary file offset (non-contiguous path)
 */
static FAT_Status Media_ReadAtFragmented(MediaFile *media, uint32_t offset, 
                                          uint8_t *buffer, uint32_t size) {
    uint32_t cluster_size = FAT_GetClusterSize(media->vol);
    if (cluster_size == 0) return FAT_ERROR;
    
    while (size > 0 && offset < media->file_size) {
        uint32_t cluster = Media_GetClusterAt(media, offset);
        if (FAT_IsEndOfChain(cluster)) break;
        
        uint32_t offset_in_cluster = offset % cluster_size;
        uint32_t sector = FAT_ClusterToSector(media->vol, cluster);
        sector += offset_in_cluster / SD_BLOCK_SIZE;
        uint32_t sector_offset = offset_in_cluster % SD_BLOCK_SIZE;
        
        if (SD_ReadBlock(media->vol->hsd, media->vol->sector_buffer, sector) != SD_OK) {
            return FAT_ERROR_READ;
        }
        
        uint32_t available = SD_BLOCK_SIZE - sector_offset;
        uint32_t to_copy = (size < available) ? size : available;
        if (offset + to_copy > media->file_size) {
            to_copy = media->file_size - offset;
        }
        
        memcpy(buffer, media->vol->sector_buffer + sector_offset, to_copy);
        buffer += to_copy;
        offset += to_copy;
        size -= to_copy;
    }
    
    return FAT_OK;
}

/**
 * @brief Read data at arbitrary file offset (contiguous fast path)
 */
static FAT_Status Media_ReadAtContiguous(MediaFile *media, uint32_t offset,
                                          uint8_t *buffer, uint32_t size) {
    while (size > 0 && offset < media->file_size) {
        uint32_t sector = media->first_sector + (offset / SD_BLOCK_SIZE);
        uint32_t sector_offset = offset % SD_BLOCK_SIZE;
        
        if (sector_offset != 0 || size < SD_BLOCK_SIZE) {
            // Unaligned or partial sector - use scratch buffer
            if (SD_ReadBlock(media->vol->hsd, media->vol->sector_buffer, sector) != SD_OK) {
                return FAT_ERROR_READ;
            }
            
            uint32_t available = SD_BLOCK_SIZE - sector_offset;
            uint32_t to_copy = (size < available) ? size : available;
            if (offset + to_copy > media->file_size) {
                to_copy = media->file_size - offset;
            }
            
            memcpy(buffer, media->vol->sector_buffer + sector_offset, to_copy);
            buffer += to_copy;
            offset += to_copy;
            size -= to_copy;
        } else {
            // Aligned sector(s) - read directly to buffer
            uint32_t sectors_available = (media->file_size - offset) / SD_BLOCK_SIZE;
            uint32_t sectors_needed = size / SD_BLOCK_SIZE;
            uint32_t count = (sectors_needed < sectors_available) ? sectors_needed : sectors_available;
            if (count > MAX_MULTIBLOCK_COUNT) count = MAX_MULTIBLOCK_COUNT;
            
            if (count > 1) {
                if (SD_ReadMultipleBlocks(media->vol->hsd, buffer, sector, count) != SD_OK) {
                    return FAT_ERROR_READ;
                }
            } else {
                if (SD_ReadBlock(media->vol->hsd, buffer, sector) != SD_OK) {
                    return FAT_ERROR_READ;
                }
            }
            
            uint32_t bytes_read = count * SD_BLOCK_SIZE;
            buffer += bytes_read;
            offset += bytes_read;
            size -= bytes_read;
        }
    }
    
    return FAT_OK;
}

/**
 * @brief Read data at file offset (auto-selects path)
 */
static FAT_Status Media_ReadAt(MediaFile *media, uint32_t offset, 
                                uint8_t *buffer, uint32_t size) {
    if (!media || !media->is_open || !buffer) return FAT_ERROR_INVALID_PARAM;
    
    if (media->is_contiguous && media->first_sector != 0) {
        return Media_ReadAtContiguous(media, offset, buffer, size);
    } else {
        return Media_ReadAtFragmented(media, offset, buffer, size);
    }
}

/**
 * @brief Check if file is contiguous and enable fast path
 */
static bool Media_CheckContiguous(MediaFile *media) {
    if (!media || !media->is_open || !media->vol) return false;
    
    FAT_Volume *vol = media->vol;
    uint32_t cluster = media->first_cluster;
    uint32_t prev = cluster;
    uint32_t count = 0;
    
    uint32_t cluster_size = FAT_GetClusterSize(vol);
    if (cluster_size == 0) return false;
    
    uint32_t expected_clusters = (media->file_size + cluster_size - 1) / cluster_size;
    
    // Walk cluster chain, checking for gaps
    while (!FAT_IsEndOfChain(cluster)) {
        count++;
        if (count > 1 && cluster != prev + 1) {
            // Non-contiguous
            media->is_contiguous = false;
            media->first_sector = 0;
            return false;
        }
        prev = cluster;
        cluster = FAT_GetNextCluster(vol, cluster);
        
        // Safety limit
        if (count > expected_clusters + 10) break;
    }
    
    // File is contiguous
    media->is_contiguous = true;
    media->first_sector = FAT_ClusterToSector(vol, media->first_cluster);
    media->cached_cluster = media->first_cluster;
    media->cached_cluster_index = 0;
    
    return true;
}

/* ========================== Public API ========================== */

FAT_Status Media_Open(MediaFile *media, FAT_Volume *vol, const FAT_FileInfo *file_info) {
    if (!media || !vol || !vol->mounted || !file_info) {
        return FAT_ERROR_INVALID_PARAM;
    }
    
    // Clear handle
    memset(media, 0, sizeof(MediaFile));
    
    // Store file location
    media->vol = vol;
    media->first_cluster = file_info->first_cluster;
    media->file_size = file_info->size;
    
    // Read header
    uint8_t header[MEDIA_HEADER_SIZE];
    uint32_t first_sector = FAT_ClusterToSector(vol, file_info->first_cluster);
    
    if (SD_ReadBlock(vol->hsd, vol->sector_buffer, first_sector) != SD_OK) {
        return FAT_ERROR_READ;
    }
    memcpy(header, vol->sector_buffer, MEDIA_HEADER_SIZE);
    
    // Parse header
    media->frame_count = Read32LE(&header[0]);
    media->audio_size = Read32LE(&header[4]);
    media->sample_rate = Read32LE(&header[8]);
    media->channels = Read32LE(&header[12]);
    media->bits_per_sample = Read32LE(&header[16]);
    
    // Calculate offsets
    media->video_offset = MEDIA_HEADER_SIZE;
    media->audio_offset = MEDIA_HEADER_SIZE + (media->frame_count * MEDIA_FRAME_SIZE);
    
    // Initialize playback state
    media->current_frame = 0;
    media->current_sample = 0;
    media->volume_percent = MEDIA_DEFAULT_VOLUME;
    
    // Mark as open
    media->is_open = true;
    
    // Try to enable contiguous fast path
    Media_CheckContiguous(media);
    
    return FAT_OK;
}

void Media_Close(MediaFile *media) {
    if (media) {
        media->is_open = false;
        media->current_frame = 0;
        media->current_sample = 0;
        media->cached_cluster = 0;
        media->cached_cluster_index = 0;
        media->is_contiguous = false;
        media->first_sector = 0;
    }
}

void Media_SetVolume(MediaFile *media, uint8_t percent) {
    if (media) {
        media->volume_percent = (percent > 100) ? 100 : percent;
    }
}

FAT_Status Media_ReadFrameAt(MediaFile *media, uint32_t frame_number, uint8_t *buffer) {
    if (!media || !media->is_open || !buffer) return FAT_ERROR_INVALID_PARAM;
    if (frame_number >= media->frame_count) return FAT_ERROR_INVALID_PARAM;
    
    uint32_t offset = media->video_offset + (frame_number * MEDIA_FRAME_SIZE);
    return Media_ReadAt(media, offset, buffer, MEDIA_FRAME_SIZE);
}

FAT_Status Media_ReadAudioStereo(MediaFile *media, uint16_t *left, uint16_t *right, uint32_t count) {
    if (!media || !media->is_open || !left || !right) return FAT_ERROR_INVALID_PARAM;
    
    // Limit to buffer size
    if (count > MAX_AUDIO_READ_SAMPLES) {
        count = MAX_AUDIO_READ_SAMPLES;
    }
    
    // Calculate total samples available
    uint32_t bytes_per_sample = 4;  // 16-bit stereo = 4 bytes
    uint32_t total_samples = media->audio_size / bytes_per_sample;
    
    // Fill with silence if past end
    if (media->current_sample >= total_samples) {
        for (uint32_t i = 0; i < count; i++) {
            left[i] = DAC_SILENCE;
            right[i] = DAC_SILENCE;
        }
        return FAT_OK;
    }
    
    // Limit to available samples
    uint32_t available = total_samples - media->current_sample;
    uint32_t to_read = (count < available) ? count : available;
    
    // Read raw audio data
    uint32_t offset = media->audio_offset + (media->current_sample * bytes_per_sample);
    uint32_t bytes_to_read = to_read * bytes_per_sample;
    
    if (Media_ReadAt(media, offset, (uint8_t*)s_audio_buffer, bytes_to_read) != FAT_OK) {
        // On error, fill with silence
        for (uint32_t i = 0; i < count; i++) {
            left[i] = DAC_SILENCE;
            right[i] = DAC_SILENCE;
        }
        return FAT_ERROR_READ;
    }
    
    // Convert: deinterleave, apply volume, convert to 12-bit unsigned
    uint8_t vol = media->volume_percent;
    
    for (uint32_t i = 0; i < to_read; i++) {
        int16_t l_raw = s_audio_buffer[i * 2];
        int16_t r_raw = s_audio_buffer[i * 2 + 1];
        
        // Apply volume (scale by percentage)
        int32_t l_scaled = ((int32_t)l_raw * vol) / 100;
        int32_t r_scaled = ((int32_t)r_raw * vol) / 100;
        
        // Convert from signed 16-bit to unsigned 12-bit for DAC
        // Input: -32768 to 32767, Output: 0 to 4095
        left[i] = (uint16_t)((l_scaled + 32768) >> 4);
        right[i] = (uint16_t)((r_scaled + 32768) >> 4);
    }
    
    // Update position
    media->current_sample += to_read;
    
    // Fill remainder with silence
    for (uint32_t i = to_read; i < count; i++) {
        left[i] = DAC_SILENCE;
        right[i] = DAC_SILENCE;
    }
    
    // Memory barrier for DMA coherency
    __DMB();
    
    return FAT_OK;
}

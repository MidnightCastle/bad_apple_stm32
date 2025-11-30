/**
 * @file    fatfs.h
 * @brief   Minimal FAT32 filesystem (read-only) for Bad Apple player
 * @author  David Leathers
 * @date    November 2025
 * 
 * Features:
 *   - FAT32 partition detection (MBR or direct)
 *   - Root directory file search
 *   - Cluster chain traversal
 *   - Read-only (no write support)
 * 
 * Limitations:
 *   - Only FAT32 (not FAT12/FAT16)
 *   - Only 512-byte sectors
 *   - Only short filenames (8.3 format)
 *   - Only root directory search (no subdirectories)
 * 
 * Usage:
 *   1. FAT_Mount() with initialized SD handle
 *   2. FAT_FindFile() to locate file
 *   3. FAT_ClusterToSector() + FAT_GetNextCluster() to read data
 */

#ifndef FATFS_H
#define FATFS_H

#include "sd_card.h"
#include <stdint.h>
#include <stdbool.h>

/* ========================== Configuration ========================== */

// Sector size - matches SD block size
#define FAT_SECTOR_SIZE         SD_BLOCK_SIZE   // 512 bytes
#define FAT_MAX_FILENAME        11              // 8.3 format without dot

// Directory entry attributes
#define FAT_ATTR_DIRECTORY      0x10    // Entry is a directory
#define FAT_ATTR_LONG_NAME      0x0F    // Long filename entry (skip these)

// Cluster chain markers
#define FAT_CLUSTER_END         0x0FFFFFF8  // >= this means end of chain

/* ========================== Types ========================== */

typedef enum {
    FAT_OK = 0,
    FAT_ERROR,
    FAT_ERROR_READ,
    FAT_ERROR_NOT_FOUND,
    FAT_ERROR_INVALID_PARAM
} FAT_Status;

// Parsed boot sector / BPB information
typedef struct {
    uint16_t bytes_per_sector;      // Should be 512
    uint8_t sectors_per_cluster;    // Usually 1-128
    uint16_t reserved_sectors;      // Before FAT
    uint8_t num_fats;               // Usually 2
    uint32_t sectors_per_fat;       // FAT32 specific
    uint32_t root_cluster;          // Root directory cluster
    uint32_t total_sectors;         // Total filesystem sectors
    uint32_t partition_lba;         // Partition start sector
    uint32_t fat_start_sector;      // First FAT sector (absolute)
    uint32_t data_start_sector;     // First data sector (absolute)
} FAT_BootSector;

// Mounted volume state
typedef struct {
    SD_Handle *hsd;                     // SD card handle
    FAT_BootSector boot;                // Parsed boot sector
    uint8_t sector_buffer[FAT_SECTOR_SIZE];  // Scratch buffer
    bool mounted;                       // Mount successful
} FAT_Volume;

// File information returned by FAT_FindFile
typedef struct {
    uint32_t first_cluster;     // Starting cluster
    uint32_t size;              // File size in bytes
    uint8_t attributes;         // File attributes
} FAT_FileInfo;

/* ========================== Core API ========================== */

/**
 * @brief Mount FAT32 filesystem
 * @param vol Volume to initialize
 * @param hsd Initialized SD card handle
 * @return FAT_OK on success
 * 
 * Reads MBR and boot sector, validates FAT32, calculates region offsets.
 */
FAT_Status FAT_Mount(FAT_Volume *vol, SD_Handle *hsd);

/**
 * @brief Find file in root directory
 * @param vol    Mounted volume
 * @param filename  Filename to find (e.g., "BADAPPLE.BIN")
 * @param info   Output: file information
 * @return FAT_OK if found, FAT_ERROR_NOT_FOUND otherwise
 * 
 * Searches root directory for matching 8.3 filename.
 * Filename is automatically converted to FAT format.
 */
FAT_Status FAT_FindFile(FAT_Volume *vol, const char *filename, FAT_FileInfo *info);

/* ========================== Cluster Operations ========================== */

/**
 * @brief Get next cluster in chain
 * @param vol     Mounted volume
 * @param cluster Current cluster number
 * @return Next cluster, or 0 on error, or >= FAT_CLUSTER_END for end-of-chain
 */
uint32_t FAT_GetNextCluster(FAT_Volume *vol, uint32_t cluster);

/**
 * @brief Convert cluster number to absolute sector number
 * @param vol     Mounted volume
 * @param cluster Cluster number (must be >= 2)
 * @return Absolute sector number for first sector of cluster
 */
uint32_t FAT_ClusterToSector(FAT_Volume *vol, uint32_t cluster);

/**
 * @brief Check if cluster value indicates end of chain
 * @param cluster Cluster value to check
 * @return true if end-of-chain or invalid
 */
static inline bool FAT_IsEndOfChain(uint32_t cluster) {
    return cluster < 2 || cluster >= FAT_CLUSTER_END;
}

/**
 * @brief Get cluster size in bytes
 * @param vol Mounted volume
 * @return Bytes per cluster
 */
static inline uint32_t FAT_GetClusterSize(const FAT_Volume *vol) {
    if (!vol || !vol->mounted) return 0;
    return (uint32_t)vol->boot.sectors_per_cluster * FAT_SECTOR_SIZE;
}

/* ========================== Utility ========================== */

/**
 * @brief Convert filename to FAT 8.3 format
 * @param input  Normal filename (e.g., "BADAPPLE.BIN")
 * @param output 11-byte buffer for FAT format (space-padded, no dot)
 * 
 * Example: "BADAPPLE.BIN" -> "BADAPPLEBIN"
 *          "TEST.TXT"     -> "TEST    TXT"
 */
void FAT_ConvertFilename(const char *input, char *output);

#endif // FATFS_H

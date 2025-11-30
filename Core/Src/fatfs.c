/**
 * @file    fatfs.c
 * @brief   Minimal FAT32 filesystem implementation
 * @author  David Leathers
 * @date    November 2025
 */

#include "fatfs.h"
#include <string.h>
#include <ctype.h>

/* ========================== Private Helpers ========================== */

// Read 16-bit little-endian value from buffer
static inline uint16_t FAT_Read16(const uint8_t *buf, uint16_t offset) {
    return (uint16_t)buf[offset] | ((uint16_t)buf[offset + 1] << 8);
}

// Read 32-bit little-endian value from buffer
static inline uint32_t FAT_Read32(const uint8_t *buf, uint16_t offset) {
    return (uint32_t)buf[offset] |
           ((uint32_t)buf[offset + 1] << 8) |
           ((uint32_t)buf[offset + 2] << 16) |
           ((uint32_t)buf[offset + 3] << 24);
}

/* ========================== Public API ========================== */

FAT_Status FAT_Mount(FAT_Volume *vol, SD_Handle *hsd) {
    if (!vol || !hsd || !hsd->initialized) {
        return FAT_ERROR_INVALID_PARAM;
    }
    
    // Clear volume state
    memset(vol, 0, sizeof(FAT_Volume));
    vol->hsd = hsd;
    
    // Read MBR (sector 0)
    if (SD_ReadBlock(hsd, vol->sector_buffer, 0) != SD_OK) {
        return FAT_ERROR_READ;
    }
    
    // Check for boot signature
    if (vol->sector_buffer[510] != 0x55 || vol->sector_buffer[511] != 0xAA) {
        return FAT_ERROR;
    }
    
    // Get partition start from MBR (first partition at offset 0x1BE)
    // partition_lba of 0 means no partition table (superfloppy format)
    vol->boot.partition_lba = FAT_Read32(vol->sector_buffer, 0x1BE + 8);
    
    // Read boot sector (Volume Boot Record)
    if (SD_ReadBlock(hsd, vol->sector_buffer, vol->boot.partition_lba) != SD_OK) {
        return FAT_ERROR_READ;
    }
    
    // Verify boot signature
    if (vol->sector_buffer[510] != 0x55 || vol->sector_buffer[511] != 0xAA) {
        return FAT_ERROR;
    }
    
    // Parse BIOS Parameter Block (BPB)
    vol->boot.bytes_per_sector = FAT_Read16(vol->sector_buffer, 11);
    vol->boot.sectors_per_cluster = vol->sector_buffer[13];
    vol->boot.reserved_sectors = FAT_Read16(vol->sector_buffer, 14);
    vol->boot.num_fats = vol->sector_buffer[16];
    vol->boot.sectors_per_fat = FAT_Read32(vol->sector_buffer, 36);
    vol->boot.root_cluster = FAT_Read32(vol->sector_buffer, 44);
    vol->boot.total_sectors = FAT_Read32(vol->sector_buffer, 32);
    
    // Validate - must be FAT32 with 512-byte sectors
    if (vol->boot.bytes_per_sector != FAT_SECTOR_SIZE) {
        return FAT_ERROR;
    }
    if (vol->boot.sectors_per_cluster == 0) {
        return FAT_ERROR;
    }
    if (vol->boot.num_fats == 0) {
        return FAT_ERROR;
    }
    
    // Calculate absolute sector addresses
    vol->boot.fat_start_sector = vol->boot.partition_lba + vol->boot.reserved_sectors;
    vol->boot.data_start_sector = vol->boot.fat_start_sector +
                                   (vol->boot.num_fats * vol->boot.sectors_per_fat);
    
    vol->mounted = true;
    return FAT_OK;
}

void FAT_ConvertFilename(const char *input, char *output) {
    if (!input || !output) return;
    
    // Initialize with spaces
    memset(output, ' ', FAT_MAX_FILENAME);
    
    int i = 0;  // Input index
    int o = 0;  // Output index
    
    // Copy name part (up to 8 characters, before dot)
    while (input[i] && input[i] != '.' && o < 8) {
        output[o++] = (char)toupper((unsigned char)input[i++]);
    }
    
    // Skip to extension
    while (input[i] && input[i] != '.') {
        i++;
    }
    if (input[i] == '.') {
        i++;  // Skip dot
    }
    
    // Copy extension (up to 3 characters)
    o = 8;  // Extension starts at position 8
    while (input[i] && o < 11) {
        output[o++] = (char)toupper((unsigned char)input[i++]);
    }
}

FAT_Status FAT_FindFile(FAT_Volume *vol, const char *filename, FAT_FileInfo *info) {
    if (!vol || !vol->mounted || !filename || !info) {
        return FAT_ERROR_INVALID_PARAM;
    }
    
    // Convert filename to FAT 8.3 format
    char fat_name[FAT_MAX_FILENAME];
    FAT_ConvertFilename(filename, fat_name);
    
    // Traverse root directory cluster chain
    uint32_t cluster = vol->boot.root_cluster;
    
    while (!FAT_IsEndOfChain(cluster)) {
        uint32_t sector = FAT_ClusterToSector(vol, cluster);
        
        // Read each sector in this cluster
        for (uint32_t s = 0; s < vol->boot.sectors_per_cluster; s++) {
            if (SD_ReadBlock(vol->hsd, vol->sector_buffer, sector + s) != SD_OK) {
                return FAT_ERROR_READ;
            }
            
            // Check each directory entry (32 bytes each, 16 per sector)
            for (int e = 0; e < 16; e++) {
                uint8_t *entry = vol->sector_buffer + (e * 32);
                
                // End of directory
                if (entry[0] == 0x00) {
                    return FAT_ERROR_NOT_FOUND;
                }
                
                // Deleted entry - skip
                if (entry[0] == 0xE5) {
                    continue;
                }
                
                // Long filename entry - skip
                if ((entry[11] & FAT_ATTR_LONG_NAME) == FAT_ATTR_LONG_NAME) {
                    continue;
                }
                
                // Compare filename
                if (memcmp(entry, fat_name, FAT_MAX_FILENAME) == 0) {
                    // Found! Extract file info
                    info->attributes = entry[11];
                    
                    // First cluster: high word at offset 20-21, low word at 26-27
                    info->first_cluster = ((uint32_t)FAT_Read16(entry, 20) << 16) |
                                          FAT_Read16(entry, 26);
                    
                    // File size at offset 28
                    info->size = FAT_Read32(entry, 28);
                    
                    return FAT_OK;
                }
            }
        }
        
        // Move to next cluster in directory chain
        cluster = FAT_GetNextCluster(vol, cluster);
    }
    
    return FAT_ERROR_NOT_FOUND;
}

uint32_t FAT_GetNextCluster(FAT_Volume *vol, uint32_t cluster) {
    if (!vol || !vol->mounted || cluster < 2) {
        return 0;
    }
    
    // Calculate FAT entry location
    // Each FAT32 entry is 4 bytes
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = vol->boot.fat_start_sector + (fat_offset / FAT_SECTOR_SIZE);
    uint32_t entry_offset = fat_offset % FAT_SECTOR_SIZE;
    
    // Read FAT sector
    if (SD_ReadBlock(vol->hsd, vol->sector_buffer, fat_sector) != SD_OK) {
        return 0;
    }
    
    // Get next cluster value (mask upper 4 bits - reserved in FAT32)
    uint32_t next = FAT_Read32(vol->sector_buffer, entry_offset) & 0x0FFFFFFF;
    
    return next;
}

uint32_t FAT_ClusterToSector(FAT_Volume *vol, uint32_t cluster) {
    if (!vol || !vol->mounted || cluster < 2) {
        return 0;
    }
    
    // Cluster 2 is at data_start_sector, each cluster is sectors_per_cluster sectors
    return vol->boot.data_start_sector +
           ((cluster - 2) * vol->boot.sectors_per_cluster);
}

#ifndef NP2DN_DOS_FILE_TRACKER_H
#define NP2DN_DOS_FILE_TRACKER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NP2DN_DOS_FILE_TRACKER_MAX_FILES 16u
#define NP2DN_DOS_FILE_NAME_MAX 32u

typedef struct NP2DN_DosFileInfo {
    uint16_t handle;
    uint16_t segment;
    uint16_t base_offset;
    uint16_t reserved;
    uint32_t file_position;
    uint32_t file_size;
    uint32_t loaded_size;
    uint32_t coverage_prefix;
    uint32_t sequence;
    uint8_t used;
    uint8_t closed;
    uint8_t mapping_seen;
    uint8_t mapping_valid;
    uint8_t size_known;
    uint8_t fully_loaded;
    uint8_t reserved_flags[2];
    char name[NP2DN_DOS_FILE_NAME_MAX];
} NP2DN_DosFileInfo;

/* DOS INT 21h calls are observed as entry/return pairs. */
void np2dn_dos_file_tracker_reset(void);
int np2dn_dos_file_tracker_begin_open(uint16_t return_cs,
                                      uint16_t return_ip,
                                      const char *name);
int np2dn_dos_file_tracker_begin_read(uint16_t return_cs,
                                      uint16_t return_ip,
                                      uint16_t handle,
                                      uint16_t buffer_segment,
                                      uint16_t buffer_offset,
                                      uint16_t requested);
int np2dn_dos_file_tracker_begin_seek(uint16_t return_cs,
                                      uint16_t return_ip,
                                      uint16_t handle,
                                      uint8_t origin);
int np2dn_dos_file_tracker_begin_close(uint16_t return_cs,
                                       uint16_t return_ip,
                                       uint16_t handle);
int np2dn_dos_file_tracker_complete(uint16_t current_cs,
                                     uint16_t current_ip,
                                     uint8_t carry,
                                     uint16_t result_ax,
                                     uint16_t result_dx);

int np2dn_dos_file_tracker_get_by_handle(uint16_t handle,
                                          NP2DN_DosFileInfo *info);
int np2dn_dos_file_tracker_get_loaded_segment(uint16_t segment,
                                              NP2DN_DosFileInfo *info);
int np2dn_dos_file_tracker_get_file(uint32_t index,
                                    NP2DN_DosFileInfo *info);

#ifdef __cplusplus
}
#endif

#endif

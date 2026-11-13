#ifndef NP2DN_DOS_FILE_TRACKER_H
#define NP2DN_DOS_FILE_TRACKER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NP2DN_DOS_FILE_TRACKER_MAX_FILES 16u
#define NP2DN_DOS_FILE_NAME_MAX 32u
#define NP2DN_DOS_FILE_HISTORY_MAX 32u

typedef struct NP2DN_DosTrackerStats {
    uint32_t int21_hits;
    uint32_t ah_3d;
    uint32_t ah_3f;
    uint32_t ah_42;
    uint32_t ah_3e;
    uint32_t ah_other;
    uint32_t begin_open;
    uint32_t begin_read;
    uint32_t begin_seek;
    uint32_t begin_close;
    uint32_t completed_ok;
    uint32_t completed_error;
    uint32_t file_count;
    uint16_t last_cs;
    uint16_t last_ip;
    uint8_t last_ah;
    uint8_t pending_type;
    uint32_t vector_checks;
    uint32_t last_cur;
    uint32_t last_vec;
    uint32_t last_vector_offset;
    uint32_t last_vector_segment;
    uint32_t last_cpu_cs;
    uint32_t last_cpu_eip;
    uint32_t virtual_8086;
} NP2DN_DosTrackerStats;

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


typedef struct NP2DN_DosFileHistoryInfo {
    NP2DN_DosFileInfo file;
    uint32_t read_count;
    uint32_t bytes_read;
    uint32_t seek_count;
    uint32_t last_file_offset;
    uint32_t last_seek_position;
    uint16_t last_buffer_segment;
    uint16_t last_buffer_offset;
    uint16_t last_requested;
    uint16_t last_received;
    uint8_t last_seek_origin;
    uint8_t reason;
} NP2DN_DosFileHistoryInfo;

enum {
    NP2DN_DOS_HISTORY_REASON_UNKNOWN = 0,
    NP2DN_DOS_HISTORY_REASON_COMPLETE,
    NP2DN_DOS_HISTORY_REASON_NO_MAPPING,
    NP2DN_DOS_HISTORY_REASON_MAPPING_INVALID,
    NP2DN_DOS_HISTORY_REASON_SIZE_UNKNOWN,
    NP2DN_DOS_HISTORY_REASON_INCOMPLETE_COVERAGE,
    NP2DN_DOS_HISTORY_REASON_ADDRESS_OUT_OF_RANGE,
    NP2DN_DOS_HISTORY_REASON_SEGMENT_MISMATCH,
    NP2DN_DOS_HISTORY_REASON_NON_CONTIGUOUS_READ
};

/* DOS INT 21h calls are observed as entry/return pairs. */
void np2dn_dos_file_tracker_reset(void);
void np2dn_dos_file_tracker_note_int21(uint8_t ah, uint16_t cs, uint16_t ip);
void np2dn_dos_file_tracker_note_vector_probe(uint32_t cur, uint32_t vec,
                                              uint16_t vector_offset,
                                              uint16_t vector_segment,
                                              uint16_t cpu_cs,
                                              uint32_t cpu_eip,
                                              uint32_t virtual_8086);
void np2dn_dos_file_tracker_get_stats(NP2DN_DosTrackerStats *stats);
uint32_t np2dn_dos_file_tracker_file_count(void);
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
uint32_t np2dn_dos_file_tracker_history_count(void);
int np2dn_dos_file_tracker_get_history(uint32_t index,
                                       NP2DN_DosFileHistoryInfo *info);

#ifdef __cplusplus
}
#endif

#endif

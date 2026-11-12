#include "np2dn_dos_file_tracker.h"

#include <string.h>

#define NP2DN_DOS_COVERAGE_BYTES 8192u

enum {
    NP2DN_DOS_PENDING_NONE = 0,
    NP2DN_DOS_PENDING_OPEN,
    NP2DN_DOS_PENDING_READ,
    NP2DN_DOS_PENDING_SEEK,
    NP2DN_DOS_PENDING_CLOSE
};

typedef struct NP2DN_DosPendingCall {
    uint16_t return_cs;
    uint16_t return_ip;
    uint16_t handle;
    uint16_t buffer_segment;
    uint16_t buffer_offset;
    uint16_t requested;
    uint8_t type;
    uint8_t seek_origin;
    uint32_t file_offset;
    char name[NP2DN_DOS_FILE_NAME_MAX];
} NP2DN_DosPendingCall;

static NP2DN_DosFileInfo s_files[NP2DN_DOS_FILE_TRACKER_MAX_FILES];
static uint8_t s_coverage[NP2DN_DOS_FILE_TRACKER_MAX_FILES]
                         [NP2DN_DOS_COVERAGE_BYTES];
static NP2DN_DosPendingCall s_pending;
static uint32_t s_sequence;

static void np2dn_dos_copy_name(char *destination, const char *source)
{
    uint32_t i;

    for (i = 0; i + 1u < NP2DN_DOS_FILE_NAME_MAX; ++i) {
        destination[i] = source[i];
        if (source[i] == '\0') {
            return;
        }
    }
    destination[NP2DN_DOS_FILE_NAME_MAX - 1u] = '\0';
}

static int np2dn_dos_find_active_handle(uint16_t handle)
{
    uint32_t i;

    for (i = 0; i < NP2DN_DOS_FILE_TRACKER_MAX_FILES; ++i) {
        if (s_files[i].used && !s_files[i].closed &&
            s_files[i].handle == handle) {
            return (int)i;
        }
    }
    return -1;
}

static int np2dn_dos_allocate_file(void)
{
    uint32_t i;
    uint32_t oldest = 0;
    uint32_t oldest_sequence = 0xffffffffu;

    for (i = 0; i < NP2DN_DOS_FILE_TRACKER_MAX_FILES; ++i) {
        if (!s_files[i].used || s_files[i].closed) {
            return (int)i;
        }
        if (s_files[i].sequence < oldest_sequence) {
            oldest_sequence = s_files[i].sequence;
            oldest = i;
        }
    }
    return (int)oldest;
}

static void np2dn_dos_update_complete(NP2DN_DosFileInfo *file)
{
    if (file->mapping_valid && file->size_known &&
        file->coverage_prefix >= file->file_size &&
        (uint32_t)file->base_offset + file->file_size <= 0x10000u) {
        file->fully_loaded = 1;
    }
    else {
        file->fully_loaded = 0;
    }
}

static void np2dn_dos_mark_coverage(uint32_t file_index,
                                    NP2DN_DosFileInfo *file,
                                    uint32_t offset, uint32_t length)
{
    uint32_t i;

    for (i = 0; i < length; ++i) {
        uint32_t position = offset + i;
        s_coverage[file_index][position >> 3] |=
            (uint8_t)(1u << (position & 7u));
    }

    while (file->coverage_prefix < file->file_size &&
           file->coverage_prefix < NP2DN_DOS_COVERAGE_BYTES * 8u) {
        uint32_t position = file->coverage_prefix;
        if ((s_coverage[file_index][position >> 3] &
             (uint8_t)(1u << (position & 7u))) == 0) {
            break;
        }
        ++file->coverage_prefix;
    }
}

void np2dn_dos_file_tracker_reset(void)
{
    memset(s_files, 0, sizeof(s_files));
    memset(s_coverage, 0, sizeof(s_coverage));
    memset(&s_pending, 0, sizeof(s_pending));
    s_sequence = 0;
}

int np2dn_dos_file_tracker_begin_open(uint16_t return_cs,
                                      uint16_t return_ip,
                                      const char *name)
{
    if (s_pending.type != NP2DN_DOS_PENDING_NONE || name == NULL) {
        return 0;
    }
    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.type = NP2DN_DOS_PENDING_OPEN;
    s_pending.return_cs = return_cs;
    s_pending.return_ip = return_ip;
    np2dn_dos_copy_name(s_pending.name, name);
    return 1;
}

int np2dn_dos_file_tracker_begin_read(uint16_t return_cs,
                                      uint16_t return_ip,
                                      uint16_t handle,
                                      uint16_t buffer_segment,
                                      uint16_t buffer_offset,
                                      uint16_t requested)
{
    int index;

    if (s_pending.type != NP2DN_DOS_PENDING_NONE) {
        return 0;
    }
    index = np2dn_dos_find_active_handle(handle);
    if (index < 0) {
        return 0;
    }

    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.type = NP2DN_DOS_PENDING_READ;
    s_pending.return_cs = return_cs;
    s_pending.return_ip = return_ip;
    s_pending.handle = handle;
    s_pending.buffer_segment = buffer_segment;
    s_pending.buffer_offset = buffer_offset;
    s_pending.requested = requested;
    s_pending.file_offset = s_files[index].file_position;
    return 1;
}

int np2dn_dos_file_tracker_begin_seek(uint16_t return_cs,
                                      uint16_t return_ip,
                                      uint16_t handle,
                                      uint8_t origin)
{
    int index;

    if (s_pending.type != NP2DN_DOS_PENDING_NONE) {
        return 0;
    }
    index = np2dn_dos_find_active_handle(handle);
    if (index < 0) {
        return 0;
    }

    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.type = NP2DN_DOS_PENDING_SEEK;
    s_pending.return_cs = return_cs;
    s_pending.return_ip = return_ip;
    s_pending.handle = handle;
    s_pending.seek_origin = origin;
    return 1;
}

int np2dn_dos_file_tracker_begin_close(uint16_t return_cs,
                                       uint16_t return_ip,
                                       uint16_t handle)
{
    int index;

    if (s_pending.type != NP2DN_DOS_PENDING_NONE) {
        return 0;
    }
    index = np2dn_dos_find_active_handle(handle);
    if (index < 0) {
        return 0;
    }

    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.type = NP2DN_DOS_PENDING_CLOSE;
    s_pending.return_cs = return_cs;
    s_pending.return_ip = return_ip;
    s_pending.handle = handle;
    return 1;
}

int np2dn_dos_file_tracker_complete(uint16_t current_cs,
                                     uint16_t current_ip,
                                     uint8_t carry,
                                     uint16_t result_ax,
                                     uint16_t result_dx)
{
    NP2DN_DosPendingCall pending;
    int index;

    if (s_pending.type == NP2DN_DOS_PENDING_NONE ||
        current_cs != s_pending.return_cs ||
        current_ip != s_pending.return_ip) {
        return 0;
    }

    pending = s_pending;
    memset(&s_pending, 0, sizeof(s_pending));
    if (carry) {
        return 1;
    }

    if (pending.type == NP2DN_DOS_PENDING_OPEN) {
        index = np2dn_dos_allocate_file();
        memset(&s_files[index], 0, sizeof(s_files[index]));
        memset(s_coverage[index], 0, sizeof(s_coverage[index]));
        s_files[index].used = 1;
        s_files[index].handle = result_ax;
        s_files[index].sequence = ++s_sequence;
        if (s_files[index].sequence == 0) {
            s_files[index].sequence = ++s_sequence;
        }
        np2dn_dos_copy_name(s_files[index].name, pending.name);
        return 1;
    }

    index = np2dn_dos_find_active_handle(pending.handle);
    if (index < 0) {
        return 1;
    }

    if (pending.type == NP2DN_DOS_PENDING_READ) {
        NP2DN_DosFileInfo *file = &s_files[index];
        uint32_t bytes_read = result_ax;
        uint32_t end_offset;

        if (bytes_read > pending.requested ||
            pending.file_offset > NP2DN_DOS_COVERAGE_BYTES * 8u ||
            bytes_read > NP2DN_DOS_COVERAGE_BYTES * 8u -
                         pending.file_offset) {
            return 1;
        }
        end_offset = pending.file_offset + bytes_read;

        if (!file->mapping_seen) {
            file->mapping_seen = 1;
            if (pending.file_offset <= pending.buffer_offset) {
                file->segment = pending.buffer_segment;
                file->base_offset = (uint16_t)(
                    pending.buffer_offset - pending.file_offset);
                file->mapping_valid = 1;
            }
        }
        else if (file->mapping_valid) {
            uint32_t expected = (uint32_t)file->base_offset +
                                pending.file_offset;
            if (file->segment != pending.buffer_segment ||
                expected > 0xffffu ||
                pending.buffer_offset != expected) {
                file->mapping_valid = 0;
            }
        }

        if (bytes_read != 0) {
            if (end_offset > file->loaded_size) {
                file->loaded_size = end_offset;
            }
            if (end_offset > file->file_size) {
                file->file_size = end_offset;
            }
            if (file->mapping_valid &&
                (uint32_t)file->base_offset + end_offset <= 0x10000u) {
                np2dn_dos_mark_coverage((uint32_t)index, file,
                                        pending.file_offset, bytes_read);
            }
        }

        file->file_position = end_offset;
        if (pending.requested != 0 &&
            bytes_read < pending.requested) {
            file->file_size = end_offset;
            file->size_known = 1;
        }
        np2dn_dos_update_complete(file);
        return 1;
    }

    if (pending.type == NP2DN_DOS_PENDING_SEEK) {
        NP2DN_DosFileInfo *file = &s_files[index];
        uint32_t new_position =
            ((uint32_t)result_dx << 16) | (uint32_t)result_ax;

        file->file_position = new_position;
        if (pending.seek_origin == 2u) {
            file->file_size = new_position;
            file->size_known = 1;
            if (file->mapping_valid &&
                (uint32_t)file->base_offset + file->file_size > 0x10000u) {
                file->mapping_valid = 0;
            }
        }
        np2dn_dos_update_complete(file);
        return 1;
    }

    if (pending.type == NP2DN_DOS_PENDING_CLOSE) {
        s_files[index].closed = 1;
        return 1;
    }

    return 1;
}

int np2dn_dos_file_tracker_get_by_handle(uint16_t handle,
                                          NP2DN_DosFileInfo *info)
{
    uint32_t i;
    int found = -1;
    uint32_t newest = 0;

    if (info == NULL) {
        return 0;
    }
    for (i = 0; i < NP2DN_DOS_FILE_TRACKER_MAX_FILES; ++i) {
        if (s_files[i].used && s_files[i].handle == handle &&
            s_files[i].sequence >= newest) {
            newest = s_files[i].sequence;
            found = (int)i;
        }
    }
    if (found < 0) {
        return 0;
    }
    *info = s_files[found];
    return 1;
}

int np2dn_dos_file_tracker_get_loaded_segment(uint16_t segment,
                                              NP2DN_DosFileInfo *info)
{
    uint32_t i;
    int found = -1;
    uint32_t newest = 0;

    if (info == NULL) {
        return 0;
    }
    for (i = 0; i < NP2DN_DOS_FILE_TRACKER_MAX_FILES; ++i) {
        if (s_files[i].used && s_files[i].mapping_valid &&
            s_files[i].fully_loaded && s_files[i].segment == segment &&
            s_files[i].sequence >= newest) {
            newest = s_files[i].sequence;
            found = (int)i;
        }
    }
    if (found < 0) {
        return 0;
    }
    *info = s_files[found];
    return 1;
}

int np2dn_dos_file_tracker_get_file(uint32_t index,
                                    NP2DN_DosFileInfo *info)
{
    if (info == NULL || index >= NP2DN_DOS_FILE_TRACKER_MAX_FILES ||
        !s_files[index].used) {
        return 0;
    }
    *info = s_files[index];
    return 1;
}

#include "np2dn_dos_file_tracker.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
        return 1; \
    } \
} while (0)

int main(void)
{
    NP2DN_DosFileInfo info;

    memset(&info, 0, sizeof(info));
    np2dn_dos_file_tracker_reset();

    CHECK(np2dn_dos_file_tracker_begin_open(
              0x1000, 0x0100, "MAC_08.M"),
          "begin script open");
    CHECK(!np2dn_dos_file_tracker_begin_open(
              0x1000, 0x0101, "OTHER.M"),
          "do not overwrite pending DOS call");
    CHECK(!np2dn_dos_file_tracker_complete(
              0x1000, 0x0101, 0, 5, 0),
          "ignore a non-matching return address");
    CHECK(np2dn_dos_file_tracker_complete(
              0x1000, 0x0100, 0, 5, 0),
          "complete script open");
    CHECK(np2dn_dos_file_tracker_get_by_handle(5, &info),
          "opened file is tracked");
    CHECK(strcmp(info.name, "MAC_08.M") == 0 && info.file_position == 0,
          "opened file metadata");

    CHECK(np2dn_dos_file_tracker_begin_read(
              0x1000, 0x0200, 5, 0x4458, 0, 512),
          "begin first read");
    CHECK(np2dn_dos_file_tracker_complete(
              0x1000, 0x0200, 0, 512, 0),
          "complete first read");
    CHECK(np2dn_dos_file_tracker_get_by_handle(5, &info),
          "read metadata available");
    CHECK(info.mapping_valid && info.segment == 0x4458 &&
          info.base_offset == 0 && info.loaded_size == 512 &&
          !info.size_known && !info.fully_loaded,
          "partial file read does not claim complete bounds");

    CHECK(np2dn_dos_file_tracker_begin_seek(
              0x1000, 0x0300, 5, 2),
          "begin seek to end");
    CHECK(np2dn_dos_file_tracker_complete(
              0x1000, 0x0300, 0, 600, 0),
          "complete seek to end");
    CHECK(np2dn_dos_file_tracker_get_by_handle(5, &info) &&
          info.size_known && info.file_size == 600 &&
          !info.fully_loaded,
          "seek to end establishes size but not loaded coverage");

    CHECK(np2dn_dos_file_tracker_begin_seek(
              0x1000, 0x0310, 5, 0),
          "begin seek to start");
    CHECK(np2dn_dos_file_tracker_complete(
              0x1000, 0x0310, 0, 0, 0),
          "complete seek to start");
    CHECK(np2dn_dos_file_tracker_begin_read(
              0x1000, 0x0400, 5, 0x4458, 0, 600),
          "begin full script read");
    CHECK(np2dn_dos_file_tracker_complete(
              0x1000, 0x0400, 0, 600, 0),
          "complete full script read");
    CHECK(np2dn_dos_file_tracker_get_loaded_segment(0x4458, &info),
          "fully loaded script range is discoverable");
    CHECK(info.fully_loaded && info.coverage_prefix == 600 &&
          info.file_size == 600,
          "exact contiguous script range verified");

    CHECK(np2dn_dos_file_tracker_begin_close(
              0x1000, 0x0500, 5),
          "begin close");
    CHECK(np2dn_dos_file_tracker_complete(
              0x1000, 0x0500, 0, 0, 0),
          "complete close");
    CHECK(np2dn_dos_file_tracker_get_loaded_segment(0x4458, &info),
          "closed file remains available as loaded memory");
    CHECK(info.closed, "closed state retained");

    CHECK(np2dn_dos_file_tracker_begin_open(
              0x1000, 0x0600, "SHORT.M"),
          "begin short file open");
    CHECK(np2dn_dos_file_tracker_complete(
              0x1000, 0x0600, 0, 6, 0),
          "complete short file open");
    CHECK(np2dn_dos_file_tracker_begin_read(
              0x1000, 0x0610, 6, 0x5000, 0, 1024),
          "begin short EOF read");
    CHECK(np2dn_dos_file_tracker_complete(
              0x1000, 0x0610, 0, 400, 0),
          "complete short EOF read");
    CHECK(np2dn_dos_file_tracker_get_loaded_segment(0x5000, &info) &&
          info.file_size == 400 && info.fully_loaded,
          "short read marks exact EOF and complete buffer");

    CHECK(np2dn_dos_file_tracker_begin_open(
              0x1000, 0x0700, "NONCONTIG.M"),
          "begin non-contiguous file open");
    CHECK(np2dn_dos_file_tracker_complete(
              0x1000, 0x0700, 0, 7, 0),
          "complete non-contiguous file open");
    CHECK(np2dn_dos_file_tracker_begin_read(
              0x1000, 0x0710, 7, 0x6000, 0, 100),
          "begin first non-contiguous read");
    CHECK(np2dn_dos_file_tracker_complete(
              0x1000, 0x0710, 0, 100, 0),
          "complete first non-contiguous read");
    CHECK(np2dn_dos_file_tracker_begin_read(
              0x1000, 0x0720, 7, 0x6000, 200, 100),
          "begin displaced read");
    CHECK(np2dn_dos_file_tracker_complete(
              0x1000, 0x0720, 0, 100, 0),
          "complete displaced read");
    CHECK(np2dn_dos_file_tracker_begin_seek(
              0x1000, 0x0730, 7, 2),
          "begin non-contiguous size query");
    CHECK(np2dn_dos_file_tracker_complete(
              0x1000, 0x0730, 0, 200, 0),
          "complete non-contiguous size query");
    CHECK(!np2dn_dos_file_tracker_get_loaded_segment(0x6000, &info),
          "non-contiguous buffer is never exposed as a complete script");

    CHECK(np2dn_dos_file_tracker_begin_open(
              0x1000, 0x0800, "FAILED.M"),
          "begin failed open");
    CHECK(np2dn_dos_file_tracker_complete(
              0x1000, 0x0800, 1, 5, 0),
          "complete failed open");
    CHECK(!np2dn_dos_file_tracker_get_by_handle(5, &info),
          "failed open does not create a file record");

    printf("DOS file tracker tests passed.\n");
    return 0;
}

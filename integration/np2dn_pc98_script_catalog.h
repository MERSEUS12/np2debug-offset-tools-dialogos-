#ifndef NP2DN_PC98_SCRIPT_CATALOG_H
#define NP2DN_PC98_SCRIPT_CATALOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NP2DN_PC98_SCRIPT_TEXT_MAX 512u

enum {
    NP2DN_PC98_SCRIPT_ENTRY_TEXT_TRUNCATED = 0x01u
};

typedef struct NP2DN_PC98ScriptDialogue {
    uint32_t dialogue_id;
    uint32_t block_start;
    uint32_t target_esi;
    uint16_t text_length;
    uint8_t marker_type;
    uint8_t flags;
    uint8_t text[NP2DN_PC98_SCRIPT_TEXT_MAX];
} NP2DN_PC98ScriptDialogue;

typedef struct NP2DN_PC98ScriptCatalogStatus {
    uint32_t marker_count;
    uint32_t entry_count;
    uint32_t skipped_without_wait;
    uint32_t truncated_text_entries;
    uint8_t output_truncated;
} NP2DN_PC98ScriptCatalogStatus;

/*
 * Parse the PC-98 marker format used by the initial adapter.
 *
 * A block begins with 18 01 or 18 02 and one of the accepted preceding
 * bytes (00, 01, 45). Its jump point is the H in the first "\HA\" wait
 * token before the next block marker. \CR is converted to a newline.
 * All other text bytes remain in their original encoding.
 *
 * The caller supplies the exact readable script length. This parser never
 * reads beyond it and never assumes a 64-KiB segment is entirely script data.
 * dialogue_id preserves the ordinal of every valid block marker, including
 * blocks without a jumpable HA wait.
 */
int np2dn_pc98_script_catalog_build(
    const uint8_t *data, uint32_t length,
    NP2DN_PC98ScriptDialogue *entries, uint32_t capacity,
    NP2DN_PC98ScriptCatalogStatus *status);

#ifdef __cplusplus
}
#endif

#endif

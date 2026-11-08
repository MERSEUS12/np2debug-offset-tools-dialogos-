#ifndef NP2DN_DEBUG_API_H
#define NP2DN_DEBUG_API_H

/*
 * NP2Debug Next - stable in-process debugger facade.
 *
 * This API deliberately wraps the already working NP2Debug breakpoint,
 * dialogue, scene and fast-forward implementation. It does not replace
 * break.c and does not expose CPU internals to external clients.
 *
 * Threading: call these functions only from the emulator/UI thread.
 * External clients must submit commands through np2dn_command_queue.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NP2DN_DEBUG_API_VERSION 1

typedef struct NP2DN_DebugStatus {
    uint8_t stop_emulation;
    uint8_t single_step;
    uint8_t jump_armed;
    uint8_t scene_redirect_armed;
    uint8_t skip_waiting;
    uint8_t skip_busy;
    uint32_t jump_count;
    uint32_t scene_redirect_count;
    uint32_t current_instruction_breakpoint_address;
} NP2DN_DebugStatus;

typedef struct NP2DN_SkipStatus {
    uint8_t active;
    uint8_t finished;
    uint32_t pressed;
    uint32_t target;
} NP2DN_SkipStatus;

/* Runtime state and execution controls. */
void np2dn_debug_get_status(NP2DN_DebugStatus *status);
void np2dn_debug_resume(void);
void np2dn_debug_pause(void);
void np2dn_debug_step(void);
void np2dn_debug_step_over(void);

/* Read-only query helpers. */
uint32_t np2dn_debug_status_flags(void);
uint32_t np2dn_debug_current_csip(void);

/* Breakpoint controls. */
int np2dn_breakpoint_toggle(uint16_t segment, uint16_t offset, uint8_t flags);
int np2dn_breakpoint_toggle_linear(uint32_t address, uint8_t flags);
void np2dn_breakpoints_reset(void);

/* Script jump and scene redirection. */
void np2dn_jump_arm(uint16_t script_segment, uint32_t target_esi,
                    const uint32_t *allowed_cs_ip, uint32_t address_count);
void np2dn_jump_cancel(void);
void np2dn_scene_arm(const char *script_name);
void np2dn_scene_cancel(void);

/* Dialogue fast-forward and position tracking. */
void np2dn_skip_arm(uint16_t script_segment, uint32_t dialogue_count,
                    uint32_t wait_kind);
void np2dn_skip_cancel(void);
void np2dn_skip_get_status(NP2DN_SkipStatus *status);
uint32_t np2dn_skip_status_flags(void);
int np2dn_track_current(uint32_t *esi, int32_t *dialogue_id,
                        uint32_t *block_start, char *text, uint32_t text_max);

#ifdef __cplusplus
}
#endif

#endif

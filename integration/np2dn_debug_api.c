#include "compiler.h"
#include "break.h"
#include "cpucore.h"
#include "np2dn_debug_api.h"

void np2dn_debug_get_status(NP2DN_DebugStatus *status)
{
    UINT32 current_address = 0;
    np2break_t *current_breakpoint;

    if (status == NULL) {
        return;
    }

    status->stop_emulation = np2stopemulate;
    status->single_step = np2singlestep ? 1 : 0;
    status->jump_armed = np2jump_is_armed() ? 1 : 0;
    status->scene_redirect_armed = np2scene_is_armed() ? 1 : 0;
    status->skip_waiting = np2skip_is_waiting() ? 1 : 0;
    status->skip_busy = np2skip_is_busy() ? 1 : 0;
    status->jump_count = np2jump_count();
    status->scene_redirect_count = np2scene_redirect_count();
    /*
     * Do not call np2break_is_next() here: it is an execution hook, not a
     * read-only query. It advances scene tracking and fast-forward state.
     */
    current_breakpoint = np2break_lookup(&current_address, CPU_CS, CPU_IP);
    status->current_instruction_breakpoint_address =
        (current_breakpoint != NULL && *current_breakpoint != NP2BP_NONE)
            ? current_address : 0;
}

void np2dn_debug_resume(void)
{
    np2singlestep = FALSE;
    np2active_set(1);
}

void np2dn_debug_pause(void)
{
    np2active_set(0);
}

void np2dn_debug_step(void)
{
    np2active_step();
}

void np2dn_debug_step_over(void)
{
    np2active_step_over();
}

int np2dn_breakpoint_toggle(uint16_t segment, uint16_t offset, uint8_t flags)
{
    return np2break_toggle((UINT16)segment, (UINT16)offset, (np2break_t)flags)
        ? 1 : 0;
}

int np2dn_breakpoint_toggle_linear(uint32_t address, uint8_t flags)
{
    return np2break_toggle_real((UINT32)address, (np2break_t)flags) ? 1 : 0;
}

void np2dn_breakpoints_reset(void)
{
    np2break_reset();
}

void np2dn_jump_arm(uint16_t script_segment, uint32_t target_esi,
                    const uint32_t *allowed_cs_ip, uint32_t address_count)
{
    if (allowed_cs_ip == NULL) {
        address_count = 0;
    }
    np2jump_arm((UINT16)script_segment, (UINT32)target_esi,
                (const UINT32 *)allowed_cs_ip, (UINT)address_count);
}

void np2dn_jump_cancel(void)
{
    np2jump_disarm();
}

void np2dn_scene_arm(const char *script_name)
{
    if (script_name == NULL) {
        np2scene_cancel();
        return;
    }
    np2scene_arm(script_name);
}

void np2dn_scene_cancel(void)
{
    np2scene_cancel();
}

void np2dn_skip_arm(uint16_t script_segment, uint32_t dialogue_count,
                    uint32_t wait_kind)
{
    if (wait_kind > 2) {
        wait_kind = 0;
    }
    np2skip_arm((UINT16)script_segment, (UINT)dialogue_count, (UINT)wait_kind);
}

void np2dn_skip_cancel(void)
{
    np2skip_cancel();
}

void np2dn_skip_get_status(NP2DN_SkipStatus *status)
{
    BOOL active = FALSE;
    BOOL finished = FALSE;
    UINT pressed = 0;
    UINT target = 0;

    if (status == NULL) {
        return;
    }

    np2skip_status(&active, &pressed, &target, &finished);
    status->active = active ? 1 : 0;
    status->finished = finished ? 1 : 0;
    status->pressed = (uint32_t)pressed;
    status->target = (uint32_t)target;
}

int np2dn_track_current(uint32_t *esi, int32_t *dialogue_id,
                        uint32_t *block_start, char *text, uint32_t text_max)
{
    int id = 0;
    BOOL result;

    if (esi == NULL || block_start == NULL || text == NULL || text_max == 0) {
        return 0;
    }
    result = np2track_current((UINT32 *)esi, &id, (UINT32 *)block_start,
                              text, (UINT)text_max);
    if (dialogue_id != NULL) {
        *dialogue_id = (int32_t)id;
    }
    return result ? 1 : 0;
}

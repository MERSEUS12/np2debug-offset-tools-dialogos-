#include "compiler.h"
#include "../common/lstarray.h"
#include "break.h"
#include "cpucore.h"
#include "np2dn_debug_api.h"
#include "../i386c/cpumem.h"
#include "unasm.h"

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

uint32_t np2dn_debug_status_flags(void)
{
    NP2DN_DebugStatus status;
    uint32_t flags = 0;

    ZeroMemory(&status, sizeof(status));
    np2dn_debug_get_status(&status);
    if (status.stop_emulation) flags |= 0x01;
    if (status.single_step) flags |= 0x02;
    if (status.jump_armed) flags |= 0x04;
    if (status.scene_redirect_armed) flags |= 0x08;
    if (status.skip_waiting) flags |= 0x10;
    if (status.skip_busy) flags |= 0x20;
    return flags;
}

uint32_t np2dn_debug_current_csip(void)
{
    return (((uint32_t)CPU_CS & 0xffffUL) << 16) |
           ((uint32_t)CPU_IP & 0xffffUL);
}

static void np2dn_store_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)(value >> 8);
}

static void np2dn_store_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)((value >> 8) & 0xffu);
    dst[2] = (uint8_t)((value >> 16) & 0xffu);
    dst[3] = (uint8_t)(value >> 24);
}

/*
 * Payload (96 bytes): CS:u16, IP:u16, EIP:u32, linear:u32, length:u8,
 * instruction bytes[15], mnemonic[24], operand[44].
 * Uses the same native decoder and code-address calculation as unasm_next().
 */
int np2dn_debug_current_instruction(uint8_t *buffer, uint32_t capacity,
                                    uint32_t *length)
{
    uint8_t instruction[16];
    _UNASM decoded;
    uint32_t address;
    UINT instruction_length;

    if (buffer == NULL || length == NULL || capacity < 96u) return 0;

    ZeroMemory(instruction, sizeof(instruction));
    ZeroMemory(&decoded, sizeof(decoded));
    address = (CPU_STAT_PM ? CS_BASE : ((uint32_t)CPU_CS << 4)) + CPU_IP;
    memp_reads(address, instruction, (UINT)sizeof(instruction));
    instruction_length = unasm(&decoded, instruction,
                                (UINT)sizeof(instruction), FALSE, address);
    if (instruction_length == 0 || instruction_length > 15u ||
        decoded.mnemonic == NULL) {
        return 0;
    }

    ZeroMemory(buffer, 96u);
    np2dn_store_u16(buffer, (uint16_t)CPU_CS);
    np2dn_store_u16(buffer + 2u, (uint16_t)CPU_IP);
    np2dn_store_u32(buffer + 4u, (uint32_t)CPU_EIP);
    np2dn_store_u32(buffer + 8u, address);
    buffer[12] = (uint8_t)instruction_length;
    CopyMemory(buffer + 13u, instruction, instruction_length);

    {
        uint32_t i = 0;
        while (i < 23u && decoded.mnemonic[i] != '\0') {
            buffer[28u + i] = (uint8_t)decoded.mnemonic[i];
            ++i;
        }
    }
    CopyMemory(buffer + 52u, decoded.operand, sizeof(decoded.operand));
    *length = 96u;
    return 1;
}

/*
 * Payload (53 bytes): EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI (u32),
 * ES,CS,SS,DS,FS,GS (u16), EIP:u32, EFLAGS:u32, protected_mode:u8.
 */
int np2dn_debug_cpu_registers(uint8_t *buffer, uint32_t capacity,
                              uint32_t *length)
{
    const uint32_t general[8] = {
        (uint32_t)CPU_EAX, (uint32_t)CPU_ECX,
        (uint32_t)CPU_EDX, (uint32_t)CPU_EBX,
        (uint32_t)CPU_ESP, (uint32_t)CPU_EBP,
        (uint32_t)CPU_ESI, (uint32_t)CPU_EDI
    };
    const uint16_t segments[6] = {
        (uint16_t)CPU_ES, (uint16_t)CPU_CS, (uint16_t)CPU_SS,
        (uint16_t)CPU_DS, (uint16_t)CPU_FS, (uint16_t)CPU_GS
    };
    uint32_t i;

    if (buffer == NULL || length == NULL || capacity < 53u) return 0;
    ZeroMemory(buffer, 53u);
    for (i = 0; i < 8u; ++i) np2dn_store_u32(buffer + i * 4u, general[i]);
    for (i = 0; i < 6u; ++i) np2dn_store_u16(buffer + 32u + i * 2u, segments[i]);
    np2dn_store_u32(buffer + 44u, (uint32_t)CPU_EIP);
    np2dn_store_u32(buffer + 48u, (uint32_t)CPU_STATSAVE.cpu_regs.eflags.d);
    buffer[52] = CPU_STAT_PM ? 1u : 0u;
    *length = 53u;
    return 1;
}

int np2dn_memory_read(uint32_t address, uint32_t size,
                      uint32_t space, uint32_t *value)
{
    uint32_t i;
    uint32_t result = 0;
    if (value == NULL || (size != 1 && size != 2 && size != 4) || space > 1) return 0;
    if (address > (0xffffffffUL - (size - 1))) return 0;
    /* Bytewise reads preserve unaligned access and the core's memory map. */
    for (i = 0; i < size; ++i) {
        REG8 byte_value;
        if (space == 0) {
            byte_value = memp_read8(address + i);
        }
        else {
            /* This core exposes logical reads as a block API, not meml_read8. */
            meml_reads(address + i, &byte_value, 1);
        }
        result |= ((uint32_t)byte_value & 0xffUL) << (i * 8);
    }
    *value = result;
    return 1;
}

int np2dn_memory_read_block(uint32_t address, uint32_t length,
                            uint32_t space, uint8_t *buffer)
{
    uint32_t i;
    if (buffer == NULL || length == 0 || length > 256 || space > 1) return 0;
    if (address > (0xffffffffUL - (length - 1))) return 0;
    for (i = 0; i < length; ++i) {
        REG8 byte_value;
        if (space == 0) byte_value = memp_read8(address + i);
        else meml_reads(address + i, &byte_value, 1);
        buffer[i] = (uint8_t)byte_value;
    }
    return 1;
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

/* Enumerate active entries only; zeroed tombstones are deliberately skipped. */
uint32_t np2dn_breakpoint_count(void)
{
    UINT stored = listarray_getitems(np2breakaddrs);
    UINT i;
    uint32_t active = 0;

    for (i = 0; i < stored; ++i) {
        UINT32 *slot = (UINT32 *)listarray_getitem(np2breakaddrs, i);
        np2break_t *flags;
        if (slot == NULL || *slot == 0) continue;
        flags = np2break_lookup_real(*slot);
        if (flags != NULL && *flags != NP2BP_NONE) ++active;
    }
    return active;
}

int np2dn_breakpoint_get(uint32_t index, uint32_t *address, uint8_t *flags_out)
{
    UINT stored = listarray_getitems(np2breakaddrs);
    UINT i;
    uint32_t active_index = 0;

    if (address == NULL || flags_out == NULL) return 0;
    for (i = 0; i < stored; ++i) {
        UINT32 *slot = (UINT32 *)listarray_getitem(np2breakaddrs, i);
        np2break_t *flags;
        if (slot == NULL || *slot == 0) continue;
        flags = np2break_lookup_real(*slot);
        if (flags == NULL || *flags == NP2BP_NONE) continue;
        if (active_index == index) {
            *address = *slot;
            *flags_out = (uint8_t)*flags;
            return 1;
        }
        ++active_index;
    }
    return 0;
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

uint32_t np2dn_jump_trace_count(void)
{
    return (uint32_t)np2jump_trace_count();
}

int np2dn_jump_trace_get(uint32_t index, uint16_t *cs, uint32_t *eip,
                         uint32_t *old_esi, uint32_t *new_esi)
{
    UINT16 native_cs = 0; UINT32 native_eip = 0, native_old = 0, native_new = 0;
    if (cs == NULL || eip == NULL || old_esi == NULL || new_esi == NULL) return 0;
    if (!np2jump_trace_get((UINT)index, &native_cs, &native_eip, &native_old, &native_new)) return 0;
    *cs=(uint16_t)native_cs; *eip=(uint32_t)native_eip; *old_esi=(uint32_t)native_old; *new_esi=(uint32_t)native_new;
    return 1;
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

uint32_t np2dn_skip_status_flags(void)
{
    NP2DN_SkipStatus status;
    ZeroMemory(&status, sizeof(status));
    np2dn_skip_get_status(&status);
    return (status.active ? 0x01UL : 0) |
           (status.finished ? 0x02UL : 0);
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

uint32_t np2dn_scene_log_count(void)
{
    return (uint32_t)np2scene_log_count();
}

int np2dn_scene_log_get(uint32_t index, char name[16], uint32_t *linear,
                        uint8_t *redirected, uint16_t *caller_cs,
                        uint16_t *caller_ip, uint8_t context[16])
{
    BOOL redir = FALSE;
    UINT16 cs = 0;
    UINT16 ip = 0;

    if (name == NULL || linear == NULL || redirected == NULL ||
        caller_cs == NULL || caller_ip == NULL || context == NULL) return 0;
    if (!np2scene_log_get((UINT)index, name, (UINT32 *)linear, &redir) ||
        !np2scene_log_caller((UINT)index, &cs, &ip, context)) return 0;
    *redirected = redir ? 1 : 0;
    *caller_cs = (uint16_t)cs;
    *caller_ip = (uint16_t)ip;
    return 1;
}

#include "compiler.h"
#include "np2.h"
#include "np2dn_command_queue.h"
#include "np2dn_debug_api.h"
#include "np2dn_dialogue_core.h"
#include <string.h>

typedef struct NP2DN_QueueSlot {
    NP2DN_Command command;
    int state; /* 0 free, 1 pending, 2 executing, 3 complete, 4 abandoned */
    int32_t result;
    uint32_t payload_length;
    uint8_t payload[256];
    uint64_t sequence;
} NP2DN_QueueSlot;

static CRITICAL_SECTION s_queue_lock;
static NP2DN_QueueSlot s_slots[NP2DN_QUEUE_CAPACITY];
static uint32_t s_next_id = 1;
static uint64_t s_next_sequence = 1;
static volatile LONG s_initialized = 0;
static int s_lock_created = 0;

int np2dn_command_queue_init(void)
{
    /* The lock lives for the process lifetime: MCP producers may race shutdown. */
    if (!s_lock_created) {
        InitializeCriticalSection(&s_queue_lock);
        s_lock_created = 1;
    }
    EnterCriticalSection(&s_queue_lock);
    if (InterlockedCompareExchange(&s_initialized, 0, 0) != 0) {
        LeaveCriticalSection(&s_queue_lock);
        return 1;
    }
    ZeroMemory(s_slots, sizeof(s_slots));
    s_next_id = 1;
    s_next_sequence = 1;
    InterlockedExchange(&s_initialized, 1);
    LeaveCriticalSection(&s_queue_lock);
    return 1;
}

void np2dn_command_queue_shutdown(void)
{
    if (!s_lock_created) return;
    EnterCriticalSection(&s_queue_lock);
    InterlockedExchange(&s_initialized, 0);
    ZeroMemory(s_slots, sizeof(s_slots));
    LeaveCriticalSection(&s_queue_lock);
}

uint32_t np2dn_command_submit(const NP2DN_Command *command)
{
    uint32_t i, id = 0;
    if (InterlockedCompareExchange(&s_initialized, 0, 0) == 0 ||
        command == NULL || command->type < NP2DN_CMD_RESUME ||
        command->type > NP2DN_CMD_DIALOGUE_STATUS_QUERY ||
        command->address_count > NP2DN_MAX_JUMP_ADDRESSES) return 0;

    EnterCriticalSection(&s_queue_lock);
    if (InterlockedCompareExchange(&s_initialized, 0, 0) == 0) {
        LeaveCriticalSection(&s_queue_lock);
        return 0;
    }
    for (i = 0; i < NP2DN_QUEUE_CAPACITY; ++i) {
        if (s_slots[i].state == 0) {
            s_slots[i].command = *command;
            s_slots[i].command.script_name[NP2DN_SCRIPT_NAME_MAX - 1] = '\0';
            id = s_next_id++;
            if (id == 0) id = s_next_id++;
            s_slots[i].command.id = id;
            s_slots[i].state = 1;
            s_slots[i].result = 0;
            s_slots[i].sequence = s_next_sequence++;
            break;
        }
    }
    /*
     * Wake the UI thread after enqueueing. When emulation is paused it can be
     * blocked in GetMessage(), so queue pumping alone cannot service MCP work.
     * Keep the queue lock until PostMessage succeeds to prevent the pump from
     * racing a failed wake and executing a command reported as rejected.
     */
    if (id != 0 &&
        (g_hWndMain == NULL || !IsWindow(g_hWndMain) ||
         !PostMessage(g_hWndMain, WM_NP2DN_QUEUE_WAKE, 0, 0))) {
        for (i = 0; i < NP2DN_QUEUE_CAPACITY; ++i) {
            if (s_slots[i].state == 1 && s_slots[i].command.id == id) {
                ZeroMemory(&s_slots[i], sizeof(s_slots[i]));
                break;
            }
        }
        id = 0;
    }
    LeaveCriticalSection(&s_queue_lock);
    return id;
}


void np2dn_command_cancel(uint32_t command_id)
{
    uint32_t i;
    if (command_id == 0 || !s_lock_created) return;
    EnterCriticalSection(&s_queue_lock);
    for (i = 0; i < NP2DN_QUEUE_CAPACITY; ++i) {
        if (s_slots[i].state != 0 && s_slots[i].command.id == command_id) {
            if (s_slots[i].state == 2) {
                /* The emulator thread owns this command; discard its result on completion. */
                s_slots[i].state = 4;
            }
            else {
                ZeroMemory(&s_slots[i], sizeof(s_slots[i]));
            }
            break;
        }
    }
    LeaveCriticalSection(&s_queue_lock);
}

int np2dn_command_poll_memory(uint32_t command_id, int32_t *result,
                              uint32_t *length, uint8_t *buffer,
                              uint32_t buffer_capacity)
{
    uint32_t i;
    int found = 0;
    if (InterlockedCompareExchange(&s_initialized, 0, 0) == 0 ||
        command_id == 0 || result == NULL || length == NULL || buffer == NULL) return 0;
    EnterCriticalSection(&s_queue_lock);
    if (InterlockedCompareExchange(&s_initialized, 0, 0) == 0) {
        LeaveCriticalSection(&s_queue_lock);
        return 0;
    }
    for (i = 0; i < NP2DN_QUEUE_CAPACITY; ++i) {
        if (s_slots[i].state != 0 && s_slots[i].command.id == command_id) {
            if (s_slots[i].state == 3) {
                if (s_slots[i].payload_length > buffer_capacity) {
                    LeaveCriticalSection(&s_queue_lock);
                    return 0;
                }
                *result = s_slots[i].result;
                *length = s_slots[i].payload_length;
                if (*length) CopyMemory(buffer, s_slots[i].payload, *length);
                ZeroMemory(&s_slots[i], sizeof(s_slots[i]));
                found = 1;
            }
            break;
        }
    }
    LeaveCriticalSection(&s_queue_lock);
    return found;
}

int np2dn_command_poll(uint32_t command_id, int32_t *result)
{
    uint32_t i;
    int found = 0;
    if (InterlockedCompareExchange(&s_initialized, 0, 0) == 0 ||
        command_id == 0 || result == NULL) return 0;

    EnterCriticalSection(&s_queue_lock);
    if (InterlockedCompareExchange(&s_initialized, 0, 0) == 0) {
        LeaveCriticalSection(&s_queue_lock);
        return 0;
    }
    for (i = 0; i < NP2DN_QUEUE_CAPACITY; ++i) {
        if (s_slots[i].state != 0 && s_slots[i].command.id == command_id) {
            if (s_slots[i].state == 3) {
                *result = s_slots[i].result;
                ZeroMemory(&s_slots[i], sizeof(s_slots[i]));
                found = 1;
            }
            break;
        }
    }
    LeaveCriticalSection(&s_queue_lock);
    return found;
}

static int32_t np2dn_execute_command(const NP2DN_Command *cmd)
{
    switch (cmd->type) {
    case NP2DN_CMD_RESUME: np2dn_debug_resume(); return 1;
    case NP2DN_CMD_PAUSE: np2dn_debug_pause(); return 1;
    case NP2DN_CMD_STEP: np2dn_debug_step(); return 1;
    case NP2DN_CMD_STEP_OVER: np2dn_debug_step_over(); return 1;
    case NP2DN_CMD_BREAKPOINT_TOGGLE:
        return np2dn_breakpoint_toggle((uint16_t)cmd->a, (uint16_t)cmd->b, (uint8_t)cmd->c);
    case NP2DN_CMD_BREAKPOINT_TOGGLE_LINEAR:
        return np2dn_breakpoint_toggle_linear(cmd->a, (uint8_t)cmd->b);
    case NP2DN_CMD_BREAKPOINTS_RESET: np2dn_breakpoints_reset(); return 1;
    case NP2DN_CMD_JUMP_ARM:
        np2dn_jump_arm((uint16_t)cmd->a, cmd->b, cmd->allowed_cs_ip, cmd->address_count);
        return 1;
    case NP2DN_CMD_JUMP_CANCEL: np2dn_jump_cancel(); return 1;
    case NP2DN_CMD_SCENE_ARM:
        np2dn_scene_arm(cmd->script_name[0] ? cmd->script_name : NULL);
        return 1;
    case NP2DN_CMD_SCENE_CANCEL: np2dn_scene_cancel(); return 1;
    case NP2DN_CMD_SKIP_ARM:
        np2dn_skip_arm((uint16_t)cmd->a, cmd->b, cmd->c);
        return 1;
    case NP2DN_CMD_SKIP_CANCEL: np2dn_skip_cancel(); return 1;
    case NP2DN_CMD_STATUS_QUERY: return (int32_t)np2dn_debug_status_flags();
    case NP2DN_CMD_CURRENT_CSIP: return (int32_t)np2dn_debug_current_csip();
    case NP2DN_CMD_SKIP_STATUS_QUERY: return (int32_t)np2dn_skip_status_flags();
    case NP2DN_CMD_MEMORY_READ:
    case NP2DN_CMD_MEMORY_READ_BLOCK:
    case NP2DN_CMD_DIALOGUE_CURRENT:
    case NP2DN_CMD_SCENE_HISTORY:
    case NP2DN_CMD_JUMP_TRACE:
    case NP2DN_CMD_CURRENT_INSTRUCTION:
    case NP2DN_CMD_CPU_REGISTERS:
    case NP2DN_CMD_BREAKPOINTS_LIST:
        return -2; /* Payload queries are executed by the pump on the emulator thread. */
    default: return -1;
    }
}

void np2dn_command_queue_pump(void)
{
    uint32_t processed;

    if (InterlockedCompareExchange(&s_initialized, 0, 0) == 0) return;

    /* Bound work per UI tick so a busy MCP client cannot starve Windows messages. */
    for (processed = 0; processed < NP2DN_QUEUE_CAPACITY; ++processed) {
        uint32_t i, selected = NP2DN_QUEUE_CAPACITY;
        uint64_t oldest = UINT64_MAX;
        NP2DN_Command command;
        int32_t result;
        uint8_t payload[256];
        uint32_t payload_length = 0;

        EnterCriticalSection(&s_queue_lock);
        if (InterlockedCompareExchange(&s_initialized, 0, 0) == 0) {
            LeaveCriticalSection(&s_queue_lock);
            break;
        }
        for (i = 0; i < NP2DN_QUEUE_CAPACITY; ++i) {
            if (s_slots[i].state == 1 && s_slots[i].sequence < oldest) {
                oldest = s_slots[i].sequence;
                selected = i;
            }
        }
        if (selected == NP2DN_QUEUE_CAPACITY) {
            LeaveCriticalSection(&s_queue_lock);
            break;
        }
        s_slots[selected].state = 2;
        command = s_slots[selected].command;
        LeaveCriticalSection(&s_queue_lock);

        if (command.type == NP2DN_CMD_MEMORY_READ) {
            uint32_t value = 0;
            if (np2dn_memory_read(command.a, command.b, command.c, &value)) {
                payload[0] = (uint8_t)(value & 0xff);
                payload[1] = (uint8_t)((value >> 8) & 0xff);
                payload[2] = (uint8_t)((value >> 16) & 0xff);
                payload[3] = (uint8_t)((value >> 24) & 0xff);
                result = 1;
                payload_length = 4;
            }
            else result = -1;
        }
        else if (command.type == NP2DN_CMD_MEMORY_READ_BLOCK) {
            if (np2dn_memory_read_block(command.a, command.b, command.c, payload)) {
                result = 1;
                payload_length = command.b;
            }
            else result = -1;
        }
        else if (command.type == NP2DN_CMD_DIALOGUE_CURRENT) {
            uint32_t esi = 0;
            uint32_t block_start = 0;
            int32_t dialogue_id = -1;
            char dialogue_text[244];
            uint32_t text_length = 0;

            ZeroMemory(dialogue_text, sizeof(dialogue_text));
            ZeroMemory(payload, sizeof(payload));
            if (np2dn_track_current(&esi, &dialogue_id, &block_start,
                                    dialogue_text, (uint32_t)sizeof(dialogue_text))) {
                payload[0] = (uint8_t)(esi & 0xff);
                payload[1] = (uint8_t)((esi >> 8) & 0xff);
                payload[2] = (uint8_t)((esi >> 16) & 0xff);
                payload[3] = (uint8_t)((esi >> 24) & 0xff);
                payload[4] = (uint8_t)((uint32_t)dialogue_id & 0xff);
                payload[5] = (uint8_t)(((uint32_t)dialogue_id >> 8) & 0xff);
                payload[6] = (uint8_t)(((uint32_t)dialogue_id >> 16) & 0xff);
                payload[7] = (uint8_t)(((uint32_t)dialogue_id >> 24) & 0xff);
                payload[8] = (uint8_t)(block_start & 0xff);
                payload[9] = (uint8_t)((block_start >> 8) & 0xff);
                payload[10] = (uint8_t)((block_start >> 16) & 0xff);
                payload[11] = (uint8_t)((block_start >> 24) & 0xff);
                while (text_length < 243u && dialogue_text[text_length] != '\0') {
                    payload[12 + text_length] = (uint8_t)dialogue_text[text_length];
                    text_length++;
                }
                payload[12 + text_length] = 0;
                payload_length = 13 + text_length;
                result = 1;
            }
            else {
                result = 0;
            }
        }
        else if (command.type == NP2DN_CMD_SCENE_HISTORY) {
            uint32_t total = np2dn_scene_log_count();
            uint32_t offset = command.a;
            uint32_t requested = command.b;
            uint32_t returned = 0;
            uint32_t i;

            ZeroMemory(payload, sizeof(payload));
            if (offset > 16u || requested < 1u || requested > 5u) {
                result = -1;
            }
            else {
                if (offset < total) {
                    returned = total - offset;
                    if (returned > requested) returned = requested;
                }
                payload[0] = (uint8_t)(total & 0xff);
                payload[1] = (uint8_t)((total >> 8) & 0xff);
                payload[2] = (uint8_t)((total >> 16) & 0xff);
                payload[3] = (uint8_t)((total >> 24) & 0xff);
                payload[4] = (uint8_t)(offset & 0xff);
                payload[5] = (uint8_t)((offset >> 8) & 0xff);
                payload[6] = (uint8_t)((offset >> 16) & 0xff);
                payload[7] = (uint8_t)((offset >> 24) & 0xff);
                payload[8] = (uint8_t)(returned & 0xff);
                payload[9] = (uint8_t)((returned >> 8) & 0xff);
                payload[10] = (uint8_t)((returned >> 16) & 0xff);
                payload[11] = (uint8_t)((returned >> 24) & 0xff);

                result = 1;
                for (i = 0; i < returned; ++i) {
                    char name[16];
                    uint32_t linear = 0;
                    uint8_t redirected = 0;
                    uint16_t caller_cs = 0;
                    uint16_t caller_ip = 0;
                    uint8_t context[16];
                    uint32_t pos = 12u + (i * 41u);
                    uint32_t j;

                    ZeroMemory(name, sizeof(name));
                    ZeroMemory(context, sizeof(context));
                    if (!np2dn_scene_log_get(offset + i, name, &linear,
                                             &redirected, &caller_cs,
                                             &caller_ip, context)) {
                        result = -1;
                        payload_length = 0;
                        break;
                    }
                    CopyMemory(payload + pos, name, 16);
                    for (j = 0; j < 4; ++j)
                        payload[pos + 16u + j] = (uint8_t)((linear >> (j * 8)) & 0xff);
                    payload[pos + 20u] = redirected;
                    payload[pos + 21u] = (uint8_t)(caller_cs & 0xff);
                    payload[pos + 22u] = (uint8_t)((caller_cs >> 8) & 0xff);
                    payload[pos + 23u] = (uint8_t)(caller_ip & 0xff);
                    payload[pos + 24u] = (uint8_t)((caller_ip >> 8) & 0xff);
                    CopyMemory(payload + pos + 25u, context, 16);
                }
                if (result == 1) payload_length = 12u + (returned * 41u);
            }
        }
        else if (command.type == NP2DN_CMD_CURRENT_INSTRUCTION) {
            if (np2dn_debug_current_instruction(payload,
                    (uint32_t)sizeof(payload), &payload_length)) result = 1;
            else result = -1;
        }
        else if (command.type == NP2DN_CMD_CPU_REGISTERS) {
            if (np2dn_debug_cpu_registers(payload,
                    (uint32_t)sizeof(payload), &payload_length)) result = 1;
            else result = -1;
        }
        else if (command.type == NP2DN_CMD_BREAKPOINTS_LIST) {
            uint32_t total = np2dn_breakpoint_count();
            uint32_t offset = command.a;
            uint32_t requested = command.b;
            uint32_t returned = 0;
            uint32_t i;

            ZeroMemory(payload, sizeof(payload));
            if (requested < 1u || requested > NP2DN_BREAKPOINT_PAGE_MAX ||
                offset > total) {
                result = -1;
            }
            else {
                returned = total - offset;
                if (returned > requested) returned = requested;
                payload[0] = (uint8_t)(total & 0xffu);
                payload[1] = (uint8_t)((total >> 8) & 0xffu);
                payload[2] = (uint8_t)((total >> 16) & 0xffu);
                payload[3] = (uint8_t)(total >> 24);
                payload[4] = (uint8_t)(offset & 0xffu);
                payload[5] = (uint8_t)((offset >> 8) & 0xffu);
                payload[6] = (uint8_t)((offset >> 16) & 0xffu);
                payload[7] = (uint8_t)(offset >> 24);
                payload[8] = (uint8_t)(returned & 0xffu);
                payload[9] = (uint8_t)((returned >> 8) & 0xffu);
                payload[10] = (uint8_t)((returned >> 16) & 0xffu);
                payload[11] = (uint8_t)(returned >> 24);
                result = 1;
                for (i = 0; i < returned; ++i) {
                    uint32_t address = 0;
                    uint8_t flags = 0;
                    uint32_t pos = 12u + i * 5u;
                    uint32_t j;
                    if (!np2dn_breakpoint_get(offset + i, &address, &flags)) {
                        result = -1;
                        payload_length = 0;
                        break;
                    }
                    for (j = 0; j < 4u; ++j)
                        payload[pos + j] = (uint8_t)(address >> (j * 8u));
                    payload[pos + 4u] = flags;
                }
                if (result == 1) payload_length = 12u + returned * 5u;
            }
        }
        else if (command.type == NP2DN_CMD_JUMP_TRACE) {
            uint32_t total=np2dn_jump_trace_count(),i; ZeroMemory(payload,sizeof(payload)); if(total>12u)total=12u;
            payload[0]=(uint8_t)total;payload[1]=(uint8_t)(total>>8);payload[2]=(uint8_t)(total>>16);payload[3]=(uint8_t)(total>>24);result=1;
            for(i=0;i<total;++i){uint16_t cs=0;uint32_t eip=0,old_esi=0,new_esi=0,pos=4u+i*14u,j;
                if(!np2dn_jump_trace_get(i,&cs,&eip,&old_esi,&new_esi)){result=-1;payload_length=0;break;}
                payload[pos]=(uint8_t)cs;payload[pos+1u]=(uint8_t)(cs>>8);
                for(j=0;j<4;++j){payload[pos+2u+j]=(uint8_t)(eip>>(j*8));payload[pos+6u+j]=(uint8_t)(old_esi>>(j*8));payload[pos+10u+j]=(uint8_t)(new_esi>>(j*8));}}
            if(result==1)payload_length=4u+total*14u;
        }
        else if (command.type == NP2DN_CMD_DIALOGUE_CATALOG_REFRESH) {
            uint32_t count;
            uint32_t truncated;
            uint64_t generation;
            ZeroMemory(payload, sizeof(payload));
            if (np2dn_dialogue_catalog_refresh()) {
                count = np2dn_dialogue_catalog_count();
                generation = np2dn_dialogue_catalog_generation();
                truncated = (uint32_t)np2dn_dialogue_catalog_is_truncated();
                payload[0] = (uint8_t)count;
                payload[1] = (uint8_t)(count >> 8);
                payload[2] = (uint8_t)(count >> 16);
                payload[3] = (uint8_t)(count >> 24);
                for (i = 0; i < 8u; ++i)
                    payload[4u + i] = (uint8_t)(generation >> (i * 8u));
                payload[12] = (uint8_t)truncated;
                result = 1;
                payload_length = 13u;
            }
            else result = -1;
        }
        else if (command.type == NP2DN_CMD_DIALOGUE_CATALOG_GET) {
            NP2DN_DialogueCatalogEntry entry;
            uint32_t id_length;
            uint32_t text_length;
            uint32_t text_capacity;
            uint32_t flags;
            ZeroMemory(payload, sizeof(payload));
            ZeroMemory(&entry, sizeof(entry));
            if (np2dn_dialogue_catalog_get(command.a, &entry)) {
                id_length = (uint32_t)strlen(entry.dialogue_id);
                if (id_length > 95u) id_length = 95u;
                text_length = (uint32_t)strlen(entry.text);
                text_capacity = 256u - 6u - id_length;
                flags = entry.flags;
                if (text_length > text_capacity) {
                    text_length = text_capacity;
                    while (text_length > 0u &&
                           (((uint8_t)entry.text[text_length] & 0xc0u) == 0x80u)) {
                        --text_length;
                    }
                    flags |= NP2DN_DIALOGUE_CATALOG_ENTRY_TEXT_TRUNCATED;
                }
                payload[0] = (uint8_t)id_length;
                payload[1] = (uint8_t)text_length;
                payload[2] = (uint8_t)flags;
                payload[3] = (uint8_t)(flags >> 8);
                payload[4] = (uint8_t)(flags >> 16);
                payload[5] = (uint8_t)(flags >> 24);
                if (id_length) CopyMemory(payload + 6u, entry.dialogue_id, id_length);
                if (text_length)
                    CopyMemory(payload + 6u + id_length, entry.text, text_length);
                payload_length = 6u + id_length + text_length;
                result = 1;
            }
            else result = 0;
        }
        else if (command.type == NP2DN_CMD_DIALOGUE_JUMP_REQUEST) {
            uint64_t expected_generation =
                (uint64_t)command.a | ((uint64_t)command.b << 32);
            result = (command.script_name[0] != '\0' &&
                      expected_generation != 0 &&
                      np2dn_dialogue_jump_request(command.script_name,
                                                  expected_generation)) ? 1 : -1;
        }
        else if (command.type == NP2DN_CMD_DIALOGUE_JUMP_CANCEL) {
            np2dn_dialogue_cancel();
            result = 1;
        }
        else if (command.type == NP2DN_CMD_DIALOGUE_STATUS_QUERY) {
            NP2DN_DialogueStatus status;
            uint32_t pos = 28u;
            uint32_t adapter_length;
            uint32_t requested_length;
            uint32_t resolved_length;
            ZeroMemory(payload, sizeof(payload));
            ZeroMemory(&status, sizeof(status));
            np2dn_dialogue_get_status(&status);
            payload[0] = (uint8_t)status.state;
            payload[1] = (uint8_t)((uint32_t)status.state >> 8);
            payload[2] = (uint8_t)((uint32_t)status.state >> 16);
            payload[3] = (uint8_t)((uint32_t)status.state >> 24);
            payload[4] = (uint8_t)status.error;
            payload[5] = (uint8_t)((uint32_t)status.error >> 8);
            payload[6] = (uint8_t)((uint32_t)status.error >> 16);
            payload[7] = (uint8_t)((uint32_t)status.error >> 24);
            payload[8] = (uint8_t)status.tick_count;
            payload[9] = (uint8_t)(status.tick_count >> 8);
            payload[10] = (uint8_t)(status.tick_count >> 16);
            payload[11] = (uint8_t)(status.tick_count >> 24);
            payload[12] = (uint8_t)status.adapter_index;
            payload[13] = (uint8_t)(status.adapter_index >> 8);
            payload[14] = (uint8_t)(status.adapter_index >> 16);
            payload[15] = (uint8_t)(status.adapter_index >> 24);
            payload[16] = status.rollback_available;
            payload[17] = (uint8_t)status.before.flags;
            payload[18] = (uint8_t)(status.before.flags >> 8);
            payload[19] = (uint8_t)(status.before.flags >> 16);
            payload[20] = (uint8_t)(status.before.flags >> 24);
            payload[21] = (uint8_t)status.after.flags;
            payload[22] = (uint8_t)(status.after.flags >> 8);
            payload[23] = (uint8_t)(status.after.flags >> 16);
            payload[24] = (uint8_t)(status.after.flags >> 24);
            adapter_length = (uint32_t)strlen(status.adapter_id);
            requested_length = (uint32_t)strlen(status.requested_dialogue_id);
            resolved_length = (uint32_t)strlen(status.resolved_dialogue_id);
            if (adapter_length > 32u) adapter_length = 32u;
            if (requested_length > 95u) requested_length = 95u;
            if (resolved_length > 95u) resolved_length = 95u;
            payload[25] = (uint8_t)adapter_length;
            payload[26] = (uint8_t)requested_length;
            payload[27] = (uint8_t)resolved_length;
            if (adapter_length) {
                CopyMemory(payload + pos, status.adapter_id, adapter_length);
                pos += adapter_length;
            }
            if (requested_length) {
                CopyMemory(payload + pos, status.requested_dialogue_id,
                           requested_length);
                pos += requested_length;
            }
            if (resolved_length) {
                CopyMemory(payload + pos, status.resolved_dialogue_id,
                           resolved_length);
                pos += resolved_length;
            }
            payload_length = pos;
            result = 1;
        }
        else result = np2dn_execute_command(&command);

        EnterCriticalSection(&s_queue_lock);
        if (s_slots[selected].command.id == command.id) {
            if (s_slots[selected].state == 2) {
                s_slots[selected].result = result;
                s_slots[selected].payload_length = payload_length;
                if (payload_length) CopyMemory(s_slots[selected].payload, payload, payload_length);
                s_slots[selected].state = 3;
            }
            else if (s_slots[selected].state == 4) {
                /* Client timed out/disconnected while this command was executing. */
                ZeroMemory(&s_slots[selected], sizeof(s_slots[selected]));
            }
        }
        LeaveCriticalSection(&s_queue_lock);
    }

    /* The dialogue state machine runs on the same serialized UI thread. */
    if (np2dn_dialogue_adapter_count() != 0) {
        np2dn_dialogue_tick();
    }
}

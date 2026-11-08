#include "compiler.h"
#include "np2dn_command_queue.h"
#include "np2dn_debug_api.h"

typedef struct NP2DN_QueueSlot {
    NP2DN_Command command;
    int state; /* 0 free, 1 pending, 2 executing, 3 complete */
    int32_t result;
    uint64_t sequence;
} NP2DN_QueueSlot;

static CRITICAL_SECTION s_queue_lock;
static NP2DN_QueueSlot s_slots[NP2DN_QUEUE_CAPACITY];
static uint32_t s_next_id = 1;
static uint64_t s_next_sequence = 1;
static int s_initialized = 0;

int np2dn_command_queue_init(void)
{
    if (s_initialized) return 1;
    InitializeCriticalSection(&s_queue_lock);
    EnterCriticalSection(&s_queue_lock);
    ZeroMemory(s_slots, sizeof(s_slots));
    s_next_id = 1;
    s_next_sequence = 1;
    s_initialized = 1;
    LeaveCriticalSection(&s_queue_lock);
    return 1;
}

void np2dn_command_queue_shutdown(void)
{
    if (!s_initialized) return;
    EnterCriticalSection(&s_queue_lock);
    ZeroMemory(s_slots, sizeof(s_slots));
    s_initialized = 0;
    LeaveCriticalSection(&s_queue_lock);
    DeleteCriticalSection(&s_queue_lock);
}

uint32_t np2dn_command_submit(const NP2DN_Command *command)
{
    uint32_t i, id = 0;
    if (!s_initialized || command == NULL || command->type < NP2DN_CMD_RESUME ||
        command->type > NP2DN_CMD_SKIP_CANCEL ||
        command->address_count > NP2DN_MAX_JUMP_ADDRESSES) return 0;

    EnterCriticalSection(&s_queue_lock);
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
    LeaveCriticalSection(&s_queue_lock);
    return id;
}

int np2dn_command_poll(uint32_t command_id, int32_t *result)
{
    uint32_t i;
    int found = 0;
    if (!s_initialized || command_id == 0 || result == NULL) return 0;

    EnterCriticalSection(&s_queue_lock);
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
    default: return -1;
    }
}

void np2dn_command_queue_pump(void)
{
    if (!s_initialized) return;

    for (;;) {
        uint32_t i, selected = NP2DN_QUEUE_CAPACITY;
        uint64_t oldest = UINT64_MAX;
        NP2DN_Command command;
        int32_t result;

        EnterCriticalSection(&s_queue_lock);
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

        result = np2dn_execute_command(&command);

        EnterCriticalSection(&s_queue_lock);
        if (s_slots[selected].state == 2 &&
            s_slots[selected].command.id == command.id) {
            s_slots[selected].result = result;
            s_slots[selected].state = 3;
        }
        LeaveCriticalSection(&s_queue_lock);
    }
}

#ifndef NP2DN_COMMAND_QUEUE_H
#define NP2DN_COMMAND_QUEUE_H

#include <stdint.h>
#include "np2dn_debug_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NP2DN_QUEUE_CAPACITY 64
#define NP2DN_MAX_JUMP_ADDRESSES 8
#define NP2DN_SCRIPT_NAME_MAX 260
#define NP2DN_BREAKPOINT_PAGE_MAX 48

typedef enum NP2DN_CommandType {
    NP2DN_CMD_RESUME = 1,
    NP2DN_CMD_PAUSE,
    NP2DN_CMD_STEP,
    NP2DN_CMD_STEP_OVER,
    NP2DN_CMD_BREAKPOINT_TOGGLE,
    NP2DN_CMD_BREAKPOINT_TOGGLE_LINEAR,
    NP2DN_CMD_BREAKPOINTS_RESET,
    NP2DN_CMD_JUMP_ARM,
    NP2DN_CMD_JUMP_CANCEL,
    NP2DN_CMD_SCENE_ARM,
    NP2DN_CMD_SCENE_CANCEL,
    NP2DN_CMD_SKIP_ARM,
    NP2DN_CMD_SKIP_CANCEL,
    NP2DN_CMD_STATUS_QUERY,
    NP2DN_CMD_CURRENT_CSIP,
    NP2DN_CMD_SKIP_STATUS_QUERY,
    NP2DN_CMD_MEMORY_READ,
    NP2DN_CMD_MEMORY_READ_BLOCK,
    NP2DN_CMD_DIALOGUE_CURRENT,
    NP2DN_CMD_SCENE_HISTORY,
    NP2DN_CMD_JUMP_TRACE,
    NP2DN_CMD_CURRENT_INSTRUCTION,
    NP2DN_CMD_CPU_REGISTERS,
    NP2DN_CMD_BREAKPOINTS_LIST
} NP2DN_CommandType;

typedef struct NP2DN_Command {
    uint32_t id;
    uint32_t type;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t address_count;
    uint32_t allowed_cs_ip[NP2DN_MAX_JUMP_ADDRESSES];
    char script_name[NP2DN_SCRIPT_NAME_MAX];
} NP2DN_Command;

/* Initialize/shutdown with the emulator lifecycle. */
int np2dn_command_queue_init(void);
void np2dn_command_queue_shutdown(void);

/* Thread-safe producer API. Returns zero when the queue is full/invalid. */
uint32_t np2dn_command_submit(const NP2DN_Command *command);

/* Returns 1 when completed, 0 while pending/not found. Completed results are consumed. */
int np2dn_command_poll(uint32_t command_id, int32_t *result);
/* Poll and consume a completed block-read result; payload is copied under lock. */
int np2dn_command_poll_memory(uint32_t command_id, int32_t *result,
                              uint32_t *length, uint8_t *buffer,
                              uint32_t buffer_capacity);

/* Called by the Windows UI/emulation thread before frame execution, including while paused. */
void np2dn_command_queue_pump(void);

#ifdef __cplusplus
}
#endif

#endif

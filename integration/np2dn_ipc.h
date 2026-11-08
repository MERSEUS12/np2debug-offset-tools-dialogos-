#ifndef NP2DN_IPC_H
#define NP2DN_IPC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NP2DN_IPC_PIPE_NAME "\\\\.\\pipe\\NP2DebugNext"
#define NP2DN_IPC_MAGIC 0x4432504eUL
#define NP2DN_IPC_VERSION 1

enum {
    NP2DN_IPC_OK = 0,
    NP2DN_IPC_INVALID_REQUEST = 1,
    NP2DN_IPC_QUEUE_FULL = 2,
    NP2DN_IPC_SHUTTING_DOWN = 3
};

#pragma pack(push, 1)
typedef struct NP2DN_IpcRequest {
    uint32_t magic;
    uint32_t version;
    uint32_t type;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t address_count;
    uint32_t allowed_cs_ip[8];
    char script_name[260];
} NP2DN_IpcRequest;

typedef struct NP2DN_IpcResponse {
    uint32_t magic;
    uint32_t version;
    uint32_t status;
    uint32_t command_id;
    int32_t result;
} NP2DN_IpcResponse;
#pragma pack(pop)

/* Starts a local-only named-pipe server. Call after command_queue_init(). */
int np2dn_ipc_start(void);

/* Stops the pipe server and waits for its worker thread to exit. */
void np2dn_ipc_stop(void);

#ifdef __cplusplus
}
#endif

#endif

#include "compiler.h"
#include <stdint.h>
#include "np2dn_ipc.h"
#include "np2dn_command_queue.h"

#define NP2DN_IPC_CONNECT_RETRY_MS 10
#define NP2DN_IPC_IO_TIMEOUT_MS 15000

static HANDLE s_stop_event = NULL;
static HANDLE s_server_thread = NULL;
static volatile LONG s_running = 0;

static int np2dn_ipc_stopping(void)
{
    return (s_stop_event == NULL) ||
        (WaitForSingleObject(s_stop_event, 0) != WAIT_TIMEOUT);
}

static int np2dn_ipc_read_exact(HANDLE pipe, void *buffer, DWORD length)
{
    BYTE *out = (BYTE *)buffer;
    DWORD total = 0;
    DWORD started = GetTickCount();

    while (total < length && !np2dn_ipc_stopping()) {
        DWORD received = 0;
        if (ReadFile(pipe, out + total, length - total, &received, NULL)) {
            if (received == 0) {
                if ((DWORD)(GetTickCount() - started) >= NP2DN_IPC_IO_TIMEOUT_MS) {
                    return 0;
                }
                Sleep(1);
            }
            else {
                total += received;
            }
        }
        else {
            DWORD error = GetLastError();
            if (error == ERROR_NO_DATA || error == ERROR_PIPE_LISTENING) {
                if ((DWORD)(GetTickCount() - started) >= NP2DN_IPC_IO_TIMEOUT_MS) {
                    return 0;
                }
                Sleep(1);
            }
            else {
                return 0;
            }
        }
    }
    return total == length;
}

static int np2dn_ipc_write_exact(HANDLE pipe, const void *buffer, DWORD length)
{
    const BYTE *input = (const BYTE *)buffer;
    DWORD total = 0;
    DWORD started = GetTickCount();

    while (total < length && !np2dn_ipc_stopping()) {
        DWORD sent = 0;
        if (WriteFile(pipe, input + total, length - total, &sent, NULL)) {
            if (sent == 0) {
                if ((DWORD)(GetTickCount() - started) >= NP2DN_IPC_IO_TIMEOUT_MS) {
                    return 0;
                }
                Sleep(1);
            }
            else {
                total += sent;
            }
        }
        else {
            DWORD error = GetLastError();
            if (error == ERROR_NO_DATA || error == ERROR_PIPE_LISTENING) {
                if ((DWORD)(GetTickCount() - started) >= NP2DN_IPC_IO_TIMEOUT_MS) {
                    return 0;
                }
                Sleep(1);
            }
            else {
                return 0;
            }
        }
    }
    return total == length;
}

static void np2dn_ipc_handle_request(HANDLE pipe)
{
    NP2DN_IpcRequest request;
    NP2DN_IpcResponse response;
    NP2DN_Command command;
    uint32_t command_id;
    int32_t result = 0;
    uint32_t payload_length = 0;

    ZeroMemory(&request, sizeof(request));
    ZeroMemory(&response, sizeof(response));

    response.magic = NP2DN_IPC_MAGIC;
    response.version = NP2DN_IPC_VERSION;

    if (!np2dn_ipc_read_exact(pipe, &request, (DWORD)sizeof(request))) {
        return;
    }

    /*
     * Keep rejection reasons in response.result so the MCP client can
     * distinguish a stale binary/protocol from a malformed request.
     * 1=magic, 2=version, 3=command below range, 4=command above range,
     * 5=address_count above the protocol limit.
     */
    if (request.magic != NP2DN_IPC_MAGIC) {
        response.status = NP2DN_IPC_INVALID_REQUEST;
        response.result = 1;
    }
    else if (request.version != NP2DN_IPC_VERSION) {
        response.status = NP2DN_IPC_INVALID_REQUEST;
        response.result = 2;
    }
    else if (request.type < NP2DN_CMD_RESUME) {
        response.status = NP2DN_IPC_INVALID_REQUEST;
        response.result = 3;
    }
    else if (request.type > NP2DN_CMD_BREAKPOINTS_LIST) {
        response.status = NP2DN_IPC_INVALID_REQUEST;
        response.result = 4;
    }
    else if (request.address_count > NP2DN_MAX_JUMP_ADDRESSES) {
        response.status = NP2DN_IPC_INVALID_REQUEST;
        response.result = 5;
    }
    else {
        response.status = NP2DN_IPC_OK;
    }
    if (response.status != NP2DN_IPC_OK) {
        np2dn_ipc_write_exact(pipe, &response, (DWORD)sizeof(response));
        return;
    }

    ZeroMemory(&command, sizeof(command));
    command.type = request.type;
    command.a = request.a;
    command.b = request.b;
    command.c = request.c;
    command.address_count = request.address_count;
    CopyMemory(command.allowed_cs_ip, request.allowed_cs_ip,
               sizeof(command.allowed_cs_ip));
    CopyMemory(command.script_name, request.script_name,
               sizeof(command.script_name));
    command.script_name[NP2DN_SCRIPT_NAME_MAX - 1] = '\0';

    command_id = np2dn_command_submit(&command);
    if (command_id == 0) {
        response.status = NP2DN_IPC_QUEUE_FULL;
        np2dn_ipc_write_exact(pipe, &response, (DWORD)sizeof(response));
        return;
    }

    response.command_id = command_id;
    while (!np2dn_ipc_stopping()) {
        if (request.type == NP2DN_CMD_MEMORY_READ ||
            request.type == NP2DN_CMD_MEMORY_READ_BLOCK ||
            request.type == NP2DN_CMD_DIALOGUE_CURRENT ||
            request.type == NP2DN_CMD_SCENE_HISTORY ||
            request.type == NP2DN_CMD_JUMP_TRACE ||
            request.type == NP2DN_CMD_CURRENT_INSTRUCTION ||
            request.type == NP2DN_CMD_CPU_REGISTERS ||
            request.type == NP2DN_CMD_BREAKPOINTS_LIST) {
            if (np2dn_command_poll_memory(command_id, &result, &payload_length,
                                          response.payload, NP2DN_IPC_MAX_PAYLOAD)) {
                response.status = NP2DN_IPC_OK;
                response.result = result;
                response.payload_length = payload_length;
                np2dn_ipc_write_exact(pipe, &response, (DWORD)sizeof(response));
                return;
            }
        }
        else if (np2dn_command_poll(command_id, &result)) {
            response.status = NP2DN_IPC_OK;
            response.result = result;
            np2dn_ipc_write_exact(pipe, &response, (DWORD)sizeof(response));
            return;
        }
        Sleep(5);
    }

    response.status = NP2DN_IPC_SHUTTING_DOWN;
    np2dn_ipc_write_exact(pipe, &response, (DWORD)sizeof(response));
}

static DWORD WINAPI np2dn_ipc_server_thread(LPVOID parameter)
{
    (void)parameter;

    while (!np2dn_ipc_stopping()) {
        HANDLE pipe = CreateNamedPipeA(
            NP2DN_IPC_PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT |
                PIPE_REJECT_REMOTE_CLIENTS,
            1,
            (DWORD)sizeof(NP2DN_IpcResponse),
            (DWORD)sizeof(NP2DN_IpcRequest),
            0,
            NULL);

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(100);
            continue;
        }

        for (;;) {
            BOOL connected = ConnectNamedPipe(pipe, NULL);
            if (connected) {
                break;
            }
            else {
                DWORD error = GetLastError();
                if (error == ERROR_PIPE_CONNECTED) {
                    break;
                }
                if (error == ERROR_PIPE_LISTENING || error == ERROR_NO_DATA) {
                    if (np2dn_ipc_stopping()) {
                        CloseHandle(pipe);
                        InterlockedExchange(&s_running, 0);
                        return 0;
                    }
                    Sleep(NP2DN_IPC_CONNECT_RETRY_MS);
                    continue;
                }
                CloseHandle(pipe);
                pipe = INVALID_HANDLE_VALUE;
                break;
            }
        }

        if (pipe != INVALID_HANDLE_VALUE) {
            if (!np2dn_ipc_stopping()) {
                np2dn_ipc_handle_request(pipe);
            }
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
    }

    InterlockedExchange(&s_running, 0);
    return 0;
}

int np2dn_ipc_start(void)
{
    if (InterlockedCompareExchange(&s_running, 1, 0) != 0) {
        return 1;
    }

    s_stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (s_stop_event == NULL) {
        InterlockedExchange(&s_running, 0);
        return 0;
    }

    s_server_thread = CreateThread(NULL, 0, np2dn_ipc_server_thread, NULL, 0, NULL);
    if (s_server_thread == NULL) {
        CloseHandle(s_stop_event);
        s_stop_event = NULL;
        InterlockedExchange(&s_running, 0);
        return 0;
    }
    return 1;
}

void np2dn_ipc_stop(void)
{
    if (s_stop_event != NULL) {
        SetEvent(s_stop_event);
    }
    if (s_server_thread != NULL) {
        WaitForSingleObject(s_server_thread, INFINITE);
        CloseHandle(s_server_thread);
        s_server_thread = NULL;
    }
    if (s_stop_event != NULL) {
        CloseHandle(s_stop_event);
        s_stop_event = NULL;
    }
    InterlockedExchange(&s_running, 0);
}

#include "compiler.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "np2dn_ipc.h"
#include "np2dn_command_queue.h"

static HANDLE s_submitted;
static HANDLE s_cancelled;
static volatile LONG s_cancel_count;

uint32_t np2dn_command_submit(const NP2DN_Command *command)
{
    (void)command;
    SetEvent(s_submitted);
    return 77;
}

void np2dn_command_cancel(uint32_t command_id)
{
    if (command_id == 77) {
        InterlockedIncrement(&s_cancel_count);
        SetEvent(s_cancelled);
    }
}

int np2dn_command_poll(uint32_t command_id, int32_t *result)
{
    (void)command_id; (void)result;
    return 0; /* Deliberately leave the request pending. */
}

int np2dn_command_poll_memory(uint32_t command_id, int32_t *result,
                              uint32_t *length, uint8_t *buffer,
                              uint32_t capacity)
{
    (void)command_id; (void)result; (void)length; (void)buffer; (void)capacity;
    return 0;
}

static void reset_signals(void)
{
    ResetEvent(s_submitted);
    ResetEvent(s_cancelled);
    InterlockedExchange(&s_cancel_count, 0);
}

static HANDLE connect_client(void)
{
    DWORD started = GetTickCount();
    for (;;) {
        HANDLE pipe = CreateFileA(NP2DN_IPC_PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (pipe != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
            if (!SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) {
                CloseHandle(pipe);
                return INVALID_HANDLE_VALUE;
            }
            return pipe;
        }
        if ((DWORD)(GetTickCount() - started) > 5000) return INVALID_HANDLE_VALUE;
        if (GetLastError() != ERROR_FILE_NOT_FOUND &&
            GetLastError() != ERROR_PIPE_BUSY) return INVALID_HANDLE_VALUE;
        Sleep(10);
    }
}

static int send_pending_request(HANDLE pipe)
{
    NP2DN_IpcRequest request;
    DWORD written = 0;
    ZeroMemory(&request, sizeof(request));
    request.magic = NP2DN_IPC_MAGIC;
    request.version = NP2DN_IPC_VERSION;
    request.type = NP2DN_CMD_DIALOGUE_STATUS_QUERY;
    if (!WriteFile(pipe, &request, (DWORD)sizeof(request), &written, NULL))
        return 0;
    return written == sizeof(request);
}

static int wait_for_event(HANDLE event, DWORD timeout)
{
    return WaitForSingleObject(event, timeout) == WAIT_OBJECT_0;
}

static int test_client_disconnect(void)
{
    HANDLE client;
    int ok = 0;
    reset_signals();
    if (!np2dn_ipc_start()) return 0;
    client = connect_client();
    if (client == INVALID_HANDLE_VALUE) goto done;
    if (!send_pending_request(client)) goto close_client;
    if (!wait_for_event(s_submitted, 2000)) goto close_client;
    CloseHandle(client);
    client = INVALID_HANDLE_VALUE;
    ok = wait_for_event(s_cancelled, 2000) &&
         InterlockedCompareExchange(&s_cancel_count, 0, 0) == 1;
    goto done;
close_client:
    CloseHandle(client);
done:
    np2dn_ipc_stop();
    return ok;
}

static int test_client_reconnect(void)
{
    HANDLE first = INVALID_HANDLE_VALUE;
    HANDLE second = INVALID_HANDLE_VALUE;
    int ok = 0;

    reset_signals();
    if (!np2dn_ipc_start()) return 0;

    first = connect_client();
    if (first == INVALID_HANDLE_VALUE) goto done;
    if (!send_pending_request(first)) goto done;
    if (!wait_for_event(s_submitted, 2000)) goto done;

    CloseHandle(first);
    first = INVALID_HANDLE_VALUE;
    if (!wait_for_event(s_cancelled, 2000) ||
        InterlockedCompareExchange(&s_cancel_count, 0, 0) != 1) {
        goto done;
    }

    /* The server must create a fresh pipe instance before this client reconnects. */
    ResetEvent(s_submitted);
    ResetEvent(s_cancelled);
    second = connect_client();
    if (second == INVALID_HANDLE_VALUE) goto done;
    if (!send_pending_request(second)) goto done;
    if (!wait_for_event(s_submitted, 2000)) goto done;

    CloseHandle(second);
    second = INVALID_HANDLE_VALUE;
    ok = wait_for_event(s_cancelled, 2000) &&
         InterlockedCompareExchange(&s_cancel_count, 0, 0) == 2;

done:
    if (first != INVALID_HANDLE_VALUE) CloseHandle(first);
    if (second != INVALID_HANDLE_VALUE) CloseHandle(second);
    np2dn_ipc_stop();
    return ok;
}

static int test_command_timeout(void)
{
    HANDLE client;
    NP2DN_IpcResponse response;
    DWORD received = 0, total_received = 0;
    DWORD mode = PIPE_READMODE_BYTE;
    DWORD last_error = ERROR_SUCCESS;
    int got_response = 0, ok = 0;
    reset_signals();
    if (!np2dn_ipc_start()) return 0;
    client = connect_client();
    if (client == INVALID_HANDLE_VALUE) goto done;
    if (!send_pending_request(client)) goto close_client;
    if (!wait_for_event(s_submitted, 2000)) goto close_client;

    /*
     * Match the production MCP client: switch from nonblocking request
     * writes to blocking response reads before waiting for the server.
     * Polling a nonblocking read can lose the race with DisconnectNamedPipe
     * immediately after the server has written its buffered response.
     */
    if (!SetNamedPipeHandleState(client, &mode, NULL, NULL)) {
        last_error = GetLastError();
        goto close_client;
    }
    ZeroMemory(&response, sizeof(response));
    while (total_received < sizeof(response)) {
        received = 0;
        if (!ReadFile(client, ((BYTE *)&response) + total_received,
                      (DWORD)sizeof(response) - total_received,
                      &received, NULL)) {
            last_error = GetLastError();
            break;
        }
        if (received == 0) break;
        total_received += received;
    }
    got_response = total_received == sizeof(response);
    ok = got_response &&
         response.magic == NP2DN_IPC_MAGIC &&
         response.version == NP2DN_IPC_VERSION &&
         response.status == NP2DN_IPC_TIMEOUT &&
         response.command_id == 77 &&
         response.result == 0 &&
         response.payload_length == 0 &&
         wait_for_event(s_cancelled, 1000) &&
         InterlockedCompareExchange(&s_cancel_count, 0, 0) == 1;
    if (!ok) {
        printf("TIMEOUT DEBUG: bytes=%lu/%lu last_error=%lu status=%lu command_id=%lu result=%ld payload=%lu cancel_count=%ld\\n",
               (unsigned long)total_received,
               (unsigned long)sizeof(response),
               (unsigned long)last_error,
               (unsigned long)response.status,
               (unsigned long)response.command_id,
               (long)response.result,
               (unsigned long)response.payload_length,
               (long)InterlockedCompareExchange(&s_cancel_count, 0, 0));
    }
close_client:
    CloseHandle(client);
done:
    np2dn_ipc_stop();
    return ok;
}

static int test_shutdown_with_pending_request(void)
{
    HANDLE client;
    int ok = 0;
    reset_signals();
    if (!np2dn_ipc_start()) return 0;
    client = connect_client();
    if (client == INVALID_HANDLE_VALUE) goto done;
    if (!send_pending_request(client)) goto close_client;
    if (!wait_for_event(s_submitted, 2000)) goto close_client;
    np2dn_ipc_stop();
    ok = wait_for_event(s_cancelled, 1000) &&
         InterlockedCompareExchange(&s_cancel_count, 0, 0) == 1;
    CloseHandle(client);
    return ok;
close_client:
    CloseHandle(client);
done:
    np2dn_ipc_stop();
    return ok;
}

int main(void)
{
    int failed = 0;
    s_submitted = CreateEvent(NULL, TRUE, FALSE, NULL);
    s_cancelled = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s_submitted || !s_cancelled) {
        printf("IPC native tests: could not create events\n");
        return 2;
    }

    if (test_client_disconnect()) printf("PASS: disconnected client cancels pending command\n");
    else { printf("FAIL: disconnected client cancellation\n"); failed = 1; }

    if (test_client_reconnect()) printf("PASS: server accepts a new client after disconnect\\n");
    else { printf("FAIL: client reconnect after disconnect\\n"); failed = 1; }

    if (test_command_timeout()) printf("PASS: command timeout returns IPC_TIMEOUT and cancels pending command\n");
    else { printf("FAIL: command timeout response/cancellation\n"); failed = 1; }

    if (test_shutdown_with_pending_request()) printf("PASS: server shutdown cancels pending command\n");
    else { printf("FAIL: shutdown cancellation\n"); failed = 1; }

    CloseHandle(s_submitted);
    CloseHandle(s_cancelled);
    return failed ? 1 : 0;
}

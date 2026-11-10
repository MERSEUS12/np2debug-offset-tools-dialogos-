#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "np2.h"
#include "np2dn_command_queue.h"

HWND g_hWndMain = NULL;

static HANDLE g_started = NULL;
static HANDLE g_release = NULL;
static volatile LONG g_block_status = 0;
static volatile LONG g_status_calls = 0;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    ExitProcess(1);
}

#define CHECK(condition, message) do { if (!(condition)) fail(message); } while (0)

static LRESULT CALLBACK test_wndproc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    return DefWindowProc(hwnd, message, wp, lp);
}

static void create_test_window(void)
{
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = test_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "NP2DNQueueUnitTestWindow";
    CHECK(RegisterClassA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
          "RegisterClassA failed");
    g_hWndMain = CreateWindowExA(0, wc.lpszClassName, "queue-test", 0,
                                 0, 0, 0, 0, HWND_MESSAGE, NULL,
                                 wc.hInstance, NULL);
    CHECK(g_hWndMain != NULL, "CreateWindowExA failed");
}

static void drain_wake_messages(void)
{
    MSG msg;
    while (PeekMessageA(&msg, g_hWndMain, WM_NP2DN_QUEUE_WAKE,
                        WM_NP2DN_QUEUE_WAKE, PM_REMOVE)) {
        /* The production WndProc treats this as a wake-only message. */
    }
}

static uint32_t submit_status_query(void)
{
    NP2DN_Command command;
    ZeroMemory(&command, sizeof(command));
    command.type = NP2DN_CMD_STATUS_QUERY;
    return np2dn_command_submit(&command);
}

static void test_pending_cancel(void)
{
    uint32_t id = submit_status_query();
    int32_t result = 0;
    CHECK(id != 0, "pending-cancel submit failed");
    drain_wake_messages();
    np2dn_command_cancel(id);
    np2dn_command_queue_pump();
    CHECK(np2dn_command_poll(id, &result) == 0,
          "cancelled pending command became observable");
    CHECK(InterlockedCompareExchange(&g_status_calls, 0, 0) == 0,
          "cancelled pending command was executed");
}

static void test_completed_cancel(void)
{
    uint32_t id = submit_status_query();
    int32_t result = 0;
    CHECK(id != 0, "completed-cancel submit failed");
    drain_wake_messages();
    np2dn_command_queue_pump();
    CHECK(np2dn_command_poll(id, &result) == 1,
          "completed command result was not available");
    CHECK(result == 0x5A, "completed command returned wrong result");

    id = submit_status_query();
    CHECK(id != 0, "second completed-cancel submit failed");
    drain_wake_messages();
    np2dn_command_queue_pump();
    np2dn_command_cancel(id);
    CHECK(np2dn_command_poll(id, &result) == 0,
          "cancel after completion did not discard result");
}

static DWORD WINAPI pump_thread(LPVOID parameter)
{
    (void)parameter;
    np2dn_command_queue_pump();
    return 0;
}

static void test_executing_cancel(void)
{
    uint32_t id = submit_status_query();
    HANDLE thread;
    int32_t result = 0;
    CHECK(id != 0, "executing-cancel submit failed");
    drain_wake_messages();

    InterlockedExchange(&g_block_status, 1);
    ResetEvent(g_started);
    ResetEvent(g_release);
    thread = CreateThread(NULL, 0, pump_thread, NULL, 0, NULL);
    CHECK(thread != NULL, "CreateThread failed");
    CHECK(WaitForSingleObject(g_started, 5000) == WAIT_OBJECT_0,
          "command did not enter executing state");

    np2dn_command_cancel(id);
    SetEvent(g_release);
    CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0,
          "pump thread did not finish");
    CloseHandle(thread);
    InterlockedExchange(&g_block_status, 0);

    CHECK(np2dn_command_poll(id, &result) == 0,
          "abandoned executing command published a result");

    id = submit_status_query();
    CHECK(id != 0, "slot was not reclaimed after abandoned execution");
    drain_wake_messages();
    np2dn_command_queue_pump();
    CHECK(np2dn_command_poll(id, &result) == 1,
          "queue did not recover after abandoned execution");
}

static void test_capacity_and_reuse(void)
{
    uint32_t ids[NP2DN_QUEUE_CAPACITY];
    uint32_t i;
    for (i = 0; i < NP2DN_QUEUE_CAPACITY; ++i) {
        ids[i] = submit_status_query();
        CHECK(ids[i] != 0, "queue filled before reaching its capacity");
    }
    CHECK(submit_status_query() == 0, "queue accepted more than its capacity");
    np2dn_command_cancel(ids[NP2DN_QUEUE_CAPACITY / 2]);
    CHECK(submit_status_query() != 0, "cancelled pending slot was not reusable");
    np2dn_command_queue_shutdown();
}

int main(void)
{
    int32_t result = 0;
    g_started = CreateEventA(NULL, TRUE, FALSE, NULL);
    g_release = CreateEventA(NULL, TRUE, FALSE, NULL);
    CHECK(g_started != NULL && g_release != NULL, "CreateEventA failed");
    create_test_window();

    CHECK(np2dn_command_queue_init() == 1, "queue init failed");
    test_pending_cancel();
    test_completed_cancel();
    test_executing_cancel();
    test_capacity_and_reuse();

    CHECK(np2dn_command_queue_init() == 1, "queue re-init failed");
    {
        uint32_t id = submit_status_query();
        CHECK(id != 0, "post-shutdown submit failed");
        drain_wake_messages();
        np2dn_command_queue_pump();
        CHECK(np2dn_command_poll(id, &result) == 1,
              "queue did not work after re-init");
    }
    np2dn_command_queue_shutdown();

    DestroyWindow(g_hWndMain);
    CloseHandle(g_started);
    CloseHandle(g_release);
    puts("NP2Debug command queue unit tests: PASS");
    return 0;
}

/* Minimal stubs: tests exercise the real queue implementation, not emulator code. */
uint32_t np2dn_debug_status_flags(void)
{
    InterlockedIncrement(&g_status_calls);
    if (InterlockedCompareExchange(&g_block_status, 0, 0)) {
        SetEvent(g_started);
        WaitForSingleObject(g_release, INFINITE);
    }
    return 0x5A;
}
uint32_t np2dn_debug_current_csip(void) { return 0; }
void np2dn_debug_resume(void) {}
void np2dn_debug_pause(void) {}
void np2dn_debug_step(void) {}
void np2dn_debug_step_over(void) {}
int np2dn_breakpoint_toggle(uint16_t a, uint16_t b, uint8_t c) { (void)a;(void)b;(void)c;return 1; }
int np2dn_breakpoint_toggle_linear(uint32_t a, uint8_t b) { (void)a;(void)b;return 1; }
void np2dn_breakpoints_reset(void) {}
void np2dn_jump_arm(uint16_t a, uint32_t b, const uint32_t *c, uint32_t d) { (void)a;(void)b;(void)c;(void)d; }
void np2dn_jump_cancel(void) {}
void np2dn_scene_arm(const char *a) { (void)a; }
void np2dn_scene_cancel(void) {}
void np2dn_skip_arm(uint16_t a, uint32_t b, uint32_t c) { (void)a;(void)b;(void)c; }
void np2dn_skip_cancel(void) {}
uint32_t np2dn_skip_status_flags(void) { return 0; }
int np2dn_memory_read(uint32_t a, uint32_t b, uint32_t c, uint32_t *d) { (void)a;(void)b;(void)c;(void)d;return 0; }
int np2dn_memory_read_block(uint32_t a, uint32_t b, uint32_t c, uint8_t *d) { (void)a;(void)b;(void)c;(void)d;return 0; }
int np2dn_track_current(uint32_t *a, int32_t *b, uint32_t *c, char *d, uint32_t e) { (void)a;(void)b;(void)c;(void)d;(void)e;return 0; }
uint32_t np2dn_scene_log_count(void) { return 0; }
int np2dn_scene_log_get(uint32_t a, char b[16], uint32_t *c, uint8_t *d, uint16_t *e, uint16_t *f, uint8_t g[16]) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;return 0; }
uint32_t np2dn_jump_trace_count(void) { return 0; }
int np2dn_jump_trace_get(uint32_t a, uint16_t *b, uint32_t *c, uint32_t *d, uint32_t *e) { (void)a;(void)b;(void)c;(void)d;(void)e;return 0; }
int np2dn_debug_current_instruction(uint8_t *a, uint32_t b, uint32_t *c) { (void)a;(void)b;(void)c;return 0; }
int np2dn_debug_cpu_registers(uint8_t *a, uint32_t b, uint32_t *c) { (void)a;(void)b;(void)c;return 0; }
uint32_t np2dn_breakpoint_count(void) { return 0; }
int np2dn_breakpoint_get(uint32_t a, uint32_t *b, uint8_t *c) { (void)a;(void)b;(void)c;return 0; }

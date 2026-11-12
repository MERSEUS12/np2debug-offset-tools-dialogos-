#ifndef NP2DN_DIALOGUE_CORE_H
#define NP2DN_DIALOGUE_CORE_H

/*
 * NP2Debug Next - engine-independent dialogue jump core.
 *
 * This module deliberately contains no CPU-register, memory-layout, script
 * marker, executable-format, or renderer assumptions. Engine/game specifics
 * belong in registered adapters.
 *
 * Threading: registration, selection, requests, ticks, cancellation and
 * status reads must be serialized by the emulator/UI thread.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NP2DN_DIALOGUE_CORE_API_VERSION 1u
#define NP2DN_DIALOGUE_MAX_ADAPTERS 8u
#define NP2DN_DIALOGUE_ID_MAX 96u
#define NP2DN_DIALOGUE_SCENE_ID_MAX 96u
#define NP2DN_DIALOGUE_LOCATOR_MAX 64u
#define NP2DN_DIALOGUE_CHECKPOINT_MAX 128u

enum {
    NP2DN_DIALOGUE_CAP_OBSERVE       = 0x00000001u,
    NP2DN_DIALOGUE_CAP_ENUMERATE     = 0x00000002u,
    NP2DN_DIALOGUE_CAP_DIRECT_JUMP   = 0x00000004u,
    NP2DN_DIALOGUE_CAP_VERIFY_STATE  = 0x00000008u,
    NP2DN_DIALOGUE_CAP_VERIFY_UI     = 0x00000010u,
    NP2DN_DIALOGUE_CAP_ROLLBACK      = 0x00000020u
};

typedef enum NP2DN_DialogueState {
    NP2DN_DIALOGUE_STATE_IDLE = 0,
    NP2DN_DIALOGUE_STATE_OBSERVING,
    NP2DN_DIALOGUE_STATE_RESOLVING,
    NP2DN_DIALOGUE_STATE_VALIDATING,
    NP2DN_DIALOGUE_STATE_PREPARING,
    NP2DN_DIALOGUE_STATE_APPLYING,
    NP2DN_DIALOGUE_STATE_VERIFYING,
    NP2DN_DIALOGUE_STATE_ROLLING_BACK,
    NP2DN_DIALOGUE_STATE_SUCCEEDED,
    NP2DN_DIALOGUE_STATE_FAILED,
    NP2DN_DIALOGUE_STATE_CANCELLED
} NP2DN_DialogueState;

typedef enum NP2DN_DialogueResult {
    NP2DN_DIALOGUE_RESULT_ERROR = -1,
    NP2DN_DIALOGUE_RESULT_PENDING = 0,
    NP2DN_DIALOGUE_RESULT_DONE = 1
} NP2DN_DialogueResult;

typedef enum NP2DN_DialogueError {
    NP2DN_DIALOGUE_ERROR_NONE = 0,
    NP2DN_DIALOGUE_ERROR_NO_ADAPTER,
    NP2DN_DIALOGUE_ERROR_INVALID_ARGUMENT,
    NP2DN_DIALOGUE_ERROR_BUSY,
    NP2DN_DIALOGUE_ERROR_OBSERVE_FAILED,
    NP2DN_DIALOGUE_ERROR_TARGET_NOT_FOUND,
    NP2DN_DIALOGUE_ERROR_VALIDATION_FAILED,
    NP2DN_DIALOGUE_ERROR_PREPARE_FAILED,
    NP2DN_DIALOGUE_ERROR_APPLY_FAILED,
    NP2DN_DIALOGUE_ERROR_VERIFY_FAILED,
    NP2DN_DIALOGUE_ERROR_ROLLBACK_FAILED,
    NP2DN_DIALOGUE_ERROR_ADAPTER_CONTRACT
} NP2DN_DialogueError;

/*
 * The snapshot is engine-neutral. Adapters populate only fields they can
 * identify reliably. A zero generation means the adapter has no generation
 * counter. Text is intentionally kept outside the transition contract.
 */
typedef struct NP2DN_DialogueSnapshot {
    char dialogue_id[NP2DN_DIALOGUE_ID_MAX];
    char scene_id[NP2DN_DIALOGUE_SCENE_ID_MAX];
    uint64_t generation;
    uint32_t flags;
} NP2DN_DialogueSnapshot;

/*
 * A target locator is opaque to the core. locator_kind defines its meaning
 * only within the adapter identified by adapter_id. It must never be treated
 * as a universal file offset, virtual address, CPU register, or script pointer.
 */
typedef struct NP2DN_DialogueTarget {
    char dialogue_id[NP2DN_DIALOGUE_ID_MAX];
    char scene_id[NP2DN_DIALOGUE_SCENE_ID_MAX];
    uint32_t locator_kind;
    uint32_t locator_size;
    uint8_t locator[NP2DN_DIALOGUE_LOCATOR_MAX];
    uint32_t flags;
} NP2DN_DialogueTarget;

typedef struct NP2DN_DialogueAdapter {
    uint32_t api_version;
    const char *adapter_id;
    uint32_t capabilities;

    int (*observe)(void *context, NP2DN_DialogueSnapshot *snapshot);
    int (*resolve)(void *context, const char *dialogue_id,
                   NP2DN_DialogueTarget *target);
    int (*validate)(void *context,
                    const NP2DN_DialogueSnapshot *current,
                    const NP2DN_DialogueTarget *target);
    int (*prepare)(void *context,
                   const NP2DN_DialogueSnapshot *current,
                   const NP2DN_DialogueTarget *target,
                   uint8_t checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX]);
    NP2DN_DialogueResult (*apply)(
        void *context, const NP2DN_DialogueTarget *target,
        uint8_t checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX]);
    NP2DN_DialogueResult (*poll)(
        void *context, const NP2DN_DialogueTarget *target,
        uint8_t checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX]);
    /*
     * DONE must mean the target is active and presented, not merely that a
     * pointer/register changed. If presentation cannot be observed, the
     * adapter must not advertise VERIFY_UI and must report only what it can
     * actually prove through its own verification policy.
     */
    NP2DN_DialogueResult (*verify)(
        void *context, const NP2DN_DialogueTarget *target,
        NP2DN_DialogueSnapshot *observed);
    NP2DN_DialogueResult (*rollback)(
        void *context,
        uint8_t checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX]);
    void (*cancel)(void *context,
                   uint8_t checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX]);
} NP2DN_DialogueAdapter;

typedef struct NP2DN_DialogueStatus {
    NP2DN_DialogueState state;
    NP2DN_DialogueError error;
    uint32_t tick_count;
    uint32_t adapter_index;
    char adapter_id[NP2DN_DIALOGUE_ID_MAX];
    char requested_dialogue_id[NP2DN_DIALOGUE_ID_MAX];
    char resolved_dialogue_id[NP2DN_DIALOGUE_ID_MAX];
    NP2DN_DialogueSnapshot before;
    NP2DN_DialogueSnapshot after;
    uint8_t rollback_available;
} NP2DN_DialogueStatus;

/* Register adapters once during initialization; duplicate IDs are rejected. */
int np2dn_dialogue_register_adapter(const NP2DN_DialogueAdapter *adapter,
                                    void *context);
int np2dn_dialogue_select_adapter(const char *adapter_id);
uint32_t np2dn_dialogue_adapter_count(void);

/* Request a jump. Work is performed incrementally by tick(). */
int np2dn_dialogue_jump_request(const char *dialogue_id);
void np2dn_dialogue_tick(void);
void np2dn_dialogue_cancel(void);
void np2dn_dialogue_get_status(NP2DN_DialogueStatus *status);

#ifdef __cplusplus
}
#endif

#endif

// break.h - Emulation activity control
// ------
// Author: Nmlgc

#ifdef __cplusplus
extern "C" {
#endif

enum {
	NP2BREAK_RESUME		= 0x00,
	NP2BREAK_MAIN		= 0x01,
	NP2BREAK_DEBUG		= 0x02
};

typedef enum {
	NP2BP_NONE		= 0x00,
	NP2BP_READ		= 0x01,
	NP2BP_WRITE		= 0x02,
	NP2BP_EXECUTE		= 0x04,
	NP2BP_ONESHOT		= 0x08
} np2break_t;

/// Globals
/// -------
extern	UINT8	np2stopemulate;
extern	BOOL 	np2singlestep;

// Dense list of flags for every possible address, used for quickly halting
// execution if necessary without having to traverse a linked list.
extern	np2break_t	np2breakflags[];
// Sparse linked list of all addresses with active breakpoints, used in the UI
// or for more naive checks.
extern	LISTARRAY	np2breakaddrs;
/// -------

/// Activity
/// --------
void np2active_renewal(UINT8 breakflag);
void np2active_set(int active);
// Sets the single-step flag and resumes emulation.
// Next CPU loop will perform one instruction only, then pause the emulation again
void np2active_step();
void np2active_step_over();
/// --------

/// Breakpoints
/// -----------
void np2break_create();

// These are guaranteed to return a valid, non-NULL pointer.
np2break_t* np2break_lookup_real(UINT32 addr);
np2break_t* np2break_lookup(UINT32 *addr_if_hit, UINT16 seg, UINT16 off);

BOOL np2break_toggle_real(UINT32 addr, np2break_t flag);
BOOL np2break_toggle(UINT16 seg, UINT16 off, np2break_t flag);

// Returns the address of the breakpoint hit at the next instruction, or 0 for none
UINT32 np2break_is_next();

void np2break_reset();
void np2break_destroy();
/// -----------

/// Jump tool hook
/// -----------------
// Armed by the "Jump to dialogue" tool.  The jump is applied once, the first time
// the script interpreter (ES = script segment) is waiting at a "\HA" command
// (ES:ESI points to "HA\").  If naddr > 0, execution must also be at one of the
// given linear CS:IP addresses.
#define	NP2JUMP_MAXADDR	8
void np2jump_arm(UINT16 es, UINT32 esi, const UINT32 *addrs, UINT naddr);
void np2jump_disarm(void);
BOOL np2jump_is_armed(void);
UINT np2jump_count(void);
// Trace of what changes ESI after the jump was applied (up to NP2JUMP_TRACEMAX
// entries, only while ES == script segment): instruction address, old and new ESI.
#define	NP2JUMP_TRACEMAX	12
UINT np2jump_trace_count(void);

// Scene redirect (v8): intercepts the DOS "open file" call (INT 21h, AH=3Dh).  Every file the
// game opens is logged with the memory address of its name.  When a scene is armed, the next scene
// script (*.BIN except SYSTEM.BIN and START.BIN) the game opens is replaced by the armed one.
#define	NP2SCENE_LOGMAX	16
void np2scene_arm(const char *name);
void np2scene_cancel(void);
BOOL np2scene_is_armed(void);
UINT np2scene_redirect_count(void);
UINT np2scene_log_count(void);
BOOL np2scene_log_get(UINT idx, char *name, UINT32 *linear, BOOL *redir);
BOOL np2scene_log_caller(UINT idx, UINT16 *cs, UINT16 *ip, UINT8 *ctx);	// who called INT 21h (return address) + the 16 code bytes before it

// Fast-forward mode (v7): each time the interpreter (ES = script segment) waits at a "\HA"
// command, the emulator presses Enter by itself (with no-wait speed) until [count] dialogues
// have been skipped; it then stops at the next wait so that dialogue is on screen.
// wait_kind: 0 = start counting now, 1 = after the ESI jump (np2jump_arm) has been applied,
// 2 = keep pressing Enter (without counting) until the scene redirect happens, then count.
void np2skip_arm(UINT16 es, UINT count, UINT wait_kind);
void np2skip_cancel(void);
void np2skip_status(BOOL *active, UINT *pressed, UINT *target, BOOL *finished);
BOOL np2skip_is_waiting(void);								// TRUE while armed but the game is at a map / menu (normal speed, it resumes at the next dialogue)
void np2skip_set_mash(BOOL on);							// TRUE: also press Enter now and then outside dialogues (tries to pass menus / maps)
BOOL np2skip_is_busy(void);										// TRUE while the fast-forward is running (the screen is not drawn)
void np2skip_arm_offset(UINT16 es, UINT32 stopoff, UINT wait_kind);	// stop at the first "\HA" wait whose position is > stopoff

// Position tracker (v9): remembers the last "\HA" wait of the script interpreter.
// "Dialogue ID" = number of dialogue blocks (marker 18 01 / 18 02) before that position, 0-based.
void np2track_setseg(UINT16 es);
BOOL np2track_current(UINT32 *esi, int *id, UINT32 *blockstart, char *text, UINT textmax);
BOOL np2scene_last_script(char *name);						// last scene script (*.BIN) the game opened
BOOL np2jump_trace_get(UINT idx, UINT16 *cs, UINT32 *eip, UINT32 *oldv, UINT32 *newv);
/// -----------------

/// Helper
/// ------
typedef struct _UNASM_t _UNASM, *UNASM;

// Disassembles the instruction at CS:IP
UINT unasm_next(UNASM una);
/// ------

#ifdef __cplusplus
}
#endif

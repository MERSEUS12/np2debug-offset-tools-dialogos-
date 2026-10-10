// break.c - Emulation activity control
// -------
// Author: Nmlgc

#include "compiler.h"
#include "np2.h"
#include "mousemng.h"
#include "scrnmng.h"
#include "soundmng.h"
#include "sysmng.h"
#include "unasm.h"
#include "keystat.h"
#include "cpucore.h"
#include "break.h"
#include "unasmdef.tbl"
#include "viewer.h"
#ifdef WIN32
#include "viewmenu.h"
#include "viewstat.h"
#else
#define	viewmenu_all_debug_toggle(running)
#define	viewstat_all_breakpoint(type, addr)
#endif

/// Globals
/// =======
UINT8	np2stopemulate = 0;
BOOL 	np2singlestep = 0;
np2break_t	np2breakflags[sizeof(mem)];
LISTARRAY	np2breakaddrs = NULL;
/// =======

/// Jump tool hook
/// =======
static BOOL		np2jump_armed = FALSE;
static UINT16	np2jump_es = 0;
static UINT32	np2jump_esi = 0;
static UINT32	np2jump_addrs[NP2JUMP_MAXADDR];
static UINT		np2jump_naddr = 0;
static UINT		np2jump_done = 0;
static BOOL		np2jump_watch = FALSE;
static UINT32	np2jump_watchesi = 0;
static UINT16	np2jump_prevcs = 0;
static UINT32	np2jump_preveip = 0;
static UINT		np2jump_ntrace = 0;
static struct {
	UINT16	cs;
	UINT32	eip;
	UINT32	oldv;
	UINT32	newv;
} np2jump_trace[NP2JUMP_TRACEMAX];

void np2jump_arm(UINT16 es, UINT32 esi, const UINT32 *addrs, UINT naddr)
{
	UINT i;

	if (naddr > NP2JUMP_MAXADDR) {
		naddr = NP2JUMP_MAXADDR;
	}
	for (i = 0; i < naddr; i++) {
		np2jump_addrs[i] = addrs[i];
	}
	np2jump_naddr = naddr;
	np2jump_es = es;
	np2jump_esi = esi;
	np2jump_watch = FALSE;
	np2jump_ntrace = 0;
	np2jump_armed = TRUE;
}

void np2jump_disarm(void)
{
	np2jump_armed = FALSE;
	np2jump_watch = FALSE;
}

BOOL np2jump_is_armed(void)
{
	return np2jump_armed;
}

UINT np2jump_count(void)
{
	return np2jump_done;
}

UINT np2jump_trace_count(void)
{
	return np2jump_ntrace;
}

BOOL np2jump_trace_get(UINT idx, UINT16 *cs, UINT32 *eip, UINT32 *oldv, UINT32 *newv)
{
	if (idx >= np2jump_ntrace) {
		return FALSE;
	}
	*cs = np2jump_trace[idx].cs;
	*eip = np2jump_trace[idx].eip;
	*oldv = np2jump_trace[idx].oldv;
	*newv = np2jump_trace[idx].newv;
	return TRUE;
}
/// Scene redirect (v8): intercepts DOS "open file" (INT 21h, AH=3Dh)
/// =======
// Every time the game opens a file, the name is logged together with the memory address
// of the file name string (this is the buffer that holds the scene name).  If a target scene
// is armed, the name of the next scene script (*.BIN, except SYSTEM.BIN / START.BIN) the game
// opens is overwritten in place with the target, so the game loads that scene instead.
static char		np2scene_target[16] = "";
static BOOL		np2scene_armed = FALSE;
static UINT		np2scene_done = 0;
static UINT		np2scene_nlog = 0;
static struct {
	char	name[16];
	UINT32	linear;
	BOOL	redir;
} np2scene_log[NP2SCENE_LOGMAX];

static char np2scene_up(char c)
{
	if ((c >= 'a') && (c <= 'z')) {
		c = (char)(c - 'a' + 'A');
	}
	return c;
}

static BOOL np2scene_same(const char *a, const char *b)
{
	while ((*a) && (*b)) {
		if (np2scene_up(*a) != np2scene_up(*b)) {
			return FALSE;
		}
		a++;
		b++;
	}
	return (*a == 0) && (*b == 0);
}

static BOOL np2scene_isscript(const char *n)
{
	UINT len = 0;

	while (n[len]) {
		len++;
	}
	if (len < 5) {
		return FALSE;
	}
	if ((np2scene_up(n[len - 4]) != '.') || (np2scene_up(n[len - 3]) != 'B') ||
		(np2scene_up(n[len - 2]) != 'I') || (np2scene_up(n[len - 1]) != 'N')) {
		return FALSE;
	}
	return !(np2scene_same(n, "SYSTEM.BIN") || np2scene_same(n, "START.BIN"));
}

void np2scene_arm(const char *name)
{
	UINT i;

	for (i = 0; (i < 15) && (name[i]); i++) {
		np2scene_target[i] = np2scene_up(name[i]);
	}
	np2scene_target[i] = '\0';
	np2scene_armed = (i != 0);
}

void np2scene_cancel(void)
{
	np2scene_armed = FALSE;
}

BOOL np2scene_is_armed(void)
{
	return np2scene_armed;
}

UINT np2scene_redirect_count(void)
{
	return np2scene_done;
}

UINT np2scene_log_count(void)
{
	return (np2scene_nlog < NP2SCENE_LOGMAX) ? np2scene_nlog : NP2SCENE_LOGMAX;
}

// idx 0 = the most recent file opened
BOOL np2scene_log_get(UINT idx, char *name, UINT32 *linear, BOOL *redir)
{
	UINT n = np2scene_log_count();
	UINT pos;
	UINT i;

	if (idx >= n) {
		return FALSE;
	}
	pos = (np2scene_nlog - 1 - idx) % NP2SCENE_LOGMAX;
	for (i = 0; i < 16; i++) {
		name[i] = np2scene_log[pos].name[i];
	}
	*linear = np2scene_log[pos].linear;
	*redir = np2scene_log[pos].redir;
	return TRUE;
}

static void np2scene_check(void)
{
	UINT32	cur;
	UINT32	vec;
	UINT32	lin;
	UINT32	base;
	UINT	i;
	UINT	len;
	UINT	slot;
	BOOL	redir;
	char	full[80];

	cur = ((UINT32)(UINT16)CPU_CS << 4) + (CPU_EIP & 0xffff);
	vec = ((UINT32)memp_read16(0x86) << 4) + (UINT32)memp_read16(0x84);
	if ((cur != vec) || (CPU_AH != 0x3d)) {
		return;
	}
	lin = ((UINT32)(UINT16)CPU_DS << 4) + (UINT32)(UINT16)CPU_DX;
	len = 0;
	while (len < 79) {
		UINT8 c = memp_read8(lin + len);
		if (!c) {
			break;
		}
		full[len++] = (c >= 0x20) && (c < 0x7f) ? (char)c : '?';
	}
	full[len] = '\0';
	base = 0;										// start of the name after the last '\' or ':'
	for (i = 0; i < len; i++) {
		if ((full[i] == '\\') || (full[i] == ':')) {
			base = i + 1;
		}
	}

	redir = FALSE;
	if ((np2scene_armed) && (np2scene_isscript(full + base)) &&
		(!np2scene_same(full + base, np2scene_target))) {
		UINT tlen = 0;

		while (np2scene_target[tlen]) {
			tlen++;
		}
		if (tlen <= 12) {							// a DOS 8.3 name always fits the usual buffers
			memp_writes(lin + base, np2scene_target, tlen + 1);		// includes the final 0
			redir = TRUE;
			np2scene_armed = FALSE;
			np2scene_done++;
		}
	}

	slot = np2scene_nlog % NP2SCENE_LOGMAX;
	for (i = 0; i < 15; i++) {
		np2scene_log[slot].name[i] = (full[base + i]) ? full[base + i] : '\0';
		if (!full[base + i]) {
			break;
		}
	}
	np2scene_log[slot].name[(i < 15) ? i : 15] = '\0';
	np2scene_log[slot].linear = lin + base;
	np2scene_log[slot].redir = redir;
	np2scene_nlog++;
}
/// =======

/// Fast-forward (skip) mode
/// =======
#define	NP2SKIP_KEYDOWN_TIME	200000		// instructions Enter stays pressed
#define	NP2SKIP_RETRY_TIME		1500000		// instructions before pressing again if the game ignored the key
static BOOL		np2skip_active = FALSE;
static BOOL		np2skip_finished = FALSE;
static UINT		np2skip_wait = 0;			// 0 = counting, 1 = until the ESI jump, 2 = until the scene redirect
static UINT		np2skip_jumpbase = 0;
static UINT		np2skip_scenebase = 0;
static UINT16	np2skip_es = 0;
static UINT		np2skip_target = 0;
static UINT		np2skip_pressed = 0;
static UINT32	np2skip_lastesi = 0xffffffff;
static BOOL		np2skip_keydown = FALSE;
static UINT		np2skip_keytimer = 0;
static UINT		np2skip_retry = 0;
static UINT8	np2skip_savednowait = 0;

void np2skip_arm(UINT16 es, UINT count, UINT wait_kind)
{
	np2skip_cancel();
	np2skip_es = es;
	np2skip_target = count;
	np2skip_pressed = 0;
	np2skip_lastesi = 0xffffffff;
	np2skip_keydown = FALSE;
	np2skip_keytimer = 0;
	np2skip_retry = 0;
	np2skip_finished = FALSE;
	np2skip_wait = wait_kind;
	np2skip_jumpbase = np2jump_done;
	np2skip_scenebase = np2scene_done;
	np2skip_active = ((count > 0) || (wait_kind == 2));
#ifdef WIN32
	if (np2skip_active) {
		np2skip_savednowait = np2oscfg.NOWAIT;
		np2oscfg.NOWAIT = 1;
	}
#endif
}

void np2skip_cancel(void)
{
	if (np2skip_keydown) {
		keystat_senddata(0x9c);		// release Enter
		np2skip_keydown = FALSE;
	}
#ifdef WIN32
	if (np2skip_active) {
		np2oscfg.NOWAIT = np2skip_savednowait;
	}
#endif
	np2skip_active = FALSE;
}

void np2skip_status(BOOL *active, UINT *pressed, UINT *target, BOOL *finished)
{
	*active = np2skip_active;
	*pressed = np2skip_pressed;
	*target = np2skip_target;
	*finished = np2skip_finished;
}

static void np2skip_press(void)
{
	if (np2skip_keydown) {
		keystat_senddata(0x9c);		// release first: a clean break + make pair
	}
	keystat_senddata(0x1c);			// Enter down
	np2skip_keydown = TRUE;
	np2skip_keytimer = NP2SKIP_KEYDOWN_TIME;
}

// called once per emulated instruction (from np2break_is_next)
static void np2skip_step(void)
{
	UINT32 sp;

	if (!np2skip_active) {
		return;
	}
	if ((np2skip_wait == 1) && (np2jump_done == np2skip_jumpbase)) {
		return;							// the ESI jump has not been applied yet
	}
	if ((np2skip_wait == 2) && (np2scene_done != np2skip_scenebase)) {
		np2skip_lastesi = 0xffffffff;	// the scene was redirected: start counting in the new scene
		np2skip_wait = 0;
	}
	if (np2skip_wait == 1) {
		np2skip_wait = 0;
	}
	if (np2skip_keydown) {
		if (np2skip_keytimer) {
			np2skip_keytimer--;
		}
		if (!np2skip_keytimer) {
			keystat_senddata(0x9c);		// Enter up
			np2skip_keydown = FALSE;
			np2skip_retry = NP2SKIP_RETRY_TIME;
		}
	}
	else if (np2skip_retry) {
		np2skip_retry--;
	}
	if (((UINT16)CPU_ES != np2skip_es) || (CPU_ESI >= 0x10000)) {
		return;
	}
	sp = ((UINT32)np2skip_es << 4) + CPU_ESI;
	if ((memp_read8(sp) != 'H') || (memp_read8(sp + 1) != 'A') ||
		(memp_read8(sp + 2) != '\\')) {
		return;
	}
	if (CPU_ESI != np2skip_lastesi) {			// a new wait for a key
		np2skip_lastesi = CPU_ESI;
		if (np2skip_wait == 2) {				// still in the old scene: keep pressing Enter, no counting
			np2skip_press();
			return;
		}
		if (np2skip_pressed >= np2skip_target) {
			np2skip_finished = TRUE;
			np2skip_cancel();
			return;
		}
		np2skip_pressed++;
		np2skip_press();
	}
	else if ((!np2skip_keydown) && (!np2skip_retry) &&
			((np2skip_pressed) || (np2skip_wait == 2))) {
		np2skip_press();						// same wait, the game ignored the key
	}
}
/// =======

void np2active_renewal(UINT8 breakflag) {										// ver0.30

	if (breakflag & (~NP2BREAK_MAIN)) {
		np2stopemulate = 2;
		soundmng_disable(SNDPROC_MASTER);
	}
	else if (breakflag & NP2BREAK_MAIN) {
		if (np2oscfg.background & 1) {
			np2stopemulate = 1;
		}
		else {
			np2stopemulate = 0;
		}
		if (np2oscfg.background) {
			soundmng_disable(SNDPROC_MASTER);
		}
		else {
			soundmng_enable(SNDPROC_MASTER);
		}
	}
	else {
		np2stopemulate = 0;
		soundmng_enable(SNDPROC_MASTER);
	}
	viewmenu_all_debug_toggle(!np2stopemulate);
	sysmng_updatecaption(0);
}

void np2active_set(int active)
{
	if (active) {
		scrnmng_update();
		keystat_allrelease();
		mousemng_enable(MOUSEPROC_BG);
	}
	else {
		mousemng_disable(MOUSEPROC_BG);
	}
	np2active_renewal(active ? NP2BREAK_RESUME : NP2BREAK_DEBUG);
}

void np2active_step()
{	
	if(!np2stopemulate)	{
		return;
	}
	np2active_set(1);
	np2singlestep = 1;
}

void np2active_step_over()	{

	UINT step;
	_UNASM una;

	if(!np2stopemulate)	{
		return;
	}
	step = unasm_next(&una);
	if(
		!strcmp(una.mnemonic, "call") ||
		!strcmp(una.mnemonic, "int")
	)	{
		np2break_toggle(CPU_CS, CPU_IP + step, NP2BP_EXECUTE | NP2BP_ONESHOT);
		np2active_set(1);
	} else {
		np2active_step();
	}
}

/// Breakpoints
/// -----------
void np2break_create()
{
	if(!np2breakaddrs) {
		np2breakaddrs = listarray_new(sizeof(UINT32), 16);
	}
}

static BOOL np2breakaddr_lookup_callback(void *vpItem, void *vpArg)
{
	return *((UINT32*)vpArg) == *((UINT32*)vpItem);
}

UINT32* np2breakaddr_lookup(UINT32 addr)
{
	return (UINT32*)listarray_enum(
		np2breakaddrs, np2breakaddr_lookup_callback, &addr
	);
}

np2break_t* np2break_lookup_real(UINT32 addr)
{
	if(addr > NELEMENTS(np2breakflags)) {
		// TODO: What do we even want to happen in this case?
		static np2break_t null_bp = NP2BP_NONE;
		return &null_bp;
	}
	return &np2breakflags[addr];
}

np2break_t* np2break_lookup(UINT32 *addr_if_hit, UINT16 seg, UINT16 off)
{
	UINT32 addr = (seg << 4) + off;
	np2break_t *ret = np2break_lookup_real(addr);
	if(*ret != NP2BP_NONE && addr_if_hit) {
		*addr_if_hit = addr;
	}
	return ret;
}

BOOL np2break_toggle_real(UINT32 addr, np2break_t flag)
{
	// LISTARRAY doesn't support element deletion.
	// We work around that here by zeroing out the elements that should be deleted.
	// Once a new element should be appended, we check for these zeroed entries
	// in order to keep the list from becoming larger and larger over time.
	UINT32* addr_slot = np2breakaddr_lookup(addr);
	np2break_t *flag_slot = np2break_lookup_real(addr);
	if(addr_slot && *flag_slot != NP2BP_NONE) {
		// Disable
		memset(addr_slot, 0, sizeof(*addr_slot));
		*flag_slot = NP2BP_NONE;
		return(TRUE);
	}
	
	// Look for a zeroed element
	addr_slot = np2breakaddr_lookup(0);
	if(!addr_slot) {
		// Nothing found, append a new one
		addr_slot = (UINT32*)listarray_append(np2breakaddrs, NULL);
		if(!addr_slot) {
			return(FALSE);
		}
	}
	// Set this element
	*addr_slot = addr;
	*flag_slot |= flag;
	return(TRUE);
}

BOOL np2break_toggle(UINT16 seg, UINT16 off, np2break_t flag)
{
	return np2break_toggle_real((seg << 4) + off, flag);
}

static UINT8 is_mem_type(const UNASM_MEMINFO *mi)	{

	if(!mi->off)	{
		return FALSE;
	}
	switch(mi->type)	{
		case OP_MEM:
		case OP_EA:
		case OP_PEA:
		case OP1_STR:
			return TRUE;
	}
	return FALSE;
}

// This is kept in here to catch edge cases not yet covered by unasm
static UINT32 np2break_memory_write_naive()	{

	static UINT bp_num = 0;
	static UINT8* probe = NULL;
	UINT bp_num_new = listarray_getitems(np2breakaddrs);
	UINT i;
	if(bp_num_new > bp_num)	{
		probe = (UINT8*)realloc(probe, bp_num_new);
		bp_num = bp_num_new;
	}
	for(i = 0; i < bp_num; i++)	{
		UINT32* bp = (UINT32*)listarray_getitem(np2breakaddrs, i);
		if(!bp)	{
			bp_num = i;
			return 0;
		}
		if(*bp && *np2break_lookup_real(*bp) & NP2BP_WRITE) {
			UINT8 cur_val = mem[*bp];
			if(probe[i] != cur_val)	{
				probe[i] = cur_val;
				return *bp;
			}
		}
	}
	return 0;
}

UINT32 np2break_is_next()	{

	_UNASM una;
	UINT32 addr = 0;
	np2break_t type = NP2BP_NONE;

	np2scene_check();
	np2skip_step();

	if (np2jump_watch) {
		if ((UINT16)CPU_ES == np2jump_es && CPU_ESI != np2jump_watchesi) {
			if (np2jump_ntrace < NP2JUMP_TRACEMAX) {
				np2jump_trace[np2jump_ntrace].cs = np2jump_prevcs;
				np2jump_trace[np2jump_ntrace].eip = np2jump_preveip;
				np2jump_trace[np2jump_ntrace].oldv = np2jump_watchesi;
				np2jump_trace[np2jump_ntrace].newv = CPU_ESI;
				np2jump_ntrace++;
			}
			np2jump_watchesi = CPU_ESI;
		}
		np2jump_prevcs = (UINT16)CPU_CS;
		np2jump_preveip = CPU_EIP;
		if (np2jump_ntrace >= NP2JUMP_TRACEMAX) {
			np2jump_watch = FALSE;
		}
	}

	if (np2jump_armed && (UINT16)CPU_ES == np2jump_es && CPU_ESI < 0x10000) {
		UINT32 sp = ((UINT32)np2jump_es << 4) + CPU_ESI;
		if (memp_read8(sp) == 'H' && memp_read8(sp + 1) == 'A' &&
			memp_read8(sp + 2) == '\\') {
			UINT32 cur = ((UINT32)CPU_CS << 4) + CPU_EIP;
			UINT i;
			BOOL ok = (np2jump_naddr == 0);
			for (i = 0; i < np2jump_naddr; i++) {
				if (np2jump_addrs[i] == cur) {
					ok = TRUE;
				}
			}
			if (ok) {
				CPU_ESI = np2jump_esi;
				np2jump_armed = FALSE;
				np2jump_done++;
				np2jump_watch = TRUE;
				np2jump_watchesi = CPU_ESI;
				np2jump_prevcs = (UINT16)CPU_CS;
				np2jump_preveip = CPU_EIP;
			}
		}
	}

#ifdef DEBUG
	addr = np2break_memory_write_naive();
	if(addr) {
		viewstat_all_breakpoint(NP2BP_WRITE, addr);
		return addr;
	}
#endif
	if(!listarray_getitems(np2breakaddrs)) {
		return 0;
	}

	unasm_next(&una);
	if(strcmp(una.mnemonic, "lea") && strcmp(una.mnemonic, "les"))	{
		if(is_mem_type(&una.meminf[MI_READ]))	{
			type |= *np2break_lookup(
				&addr, una.meminf[MI_READ].seg, una.meminf[MI_READ].off
			);
#ifndef DEBUG
		} else if(is_mem_type(&una.meminf[MI_WRITE]))	{
			type |= *np2break_lookup(
				&addr, una.meminf[MI_WRITE].seg, una.meminf[MI_WRITE].off
			);
#endif
		}
	}
	if(type == NP2BP_NONE)	{
		type |= *np2break_lookup(&addr, CPU_CS, CPU_EIP);
	}
	if(type != NP2BP_NONE)	{
		if(type & NP2BP_ONESHOT)	{
			np2break_toggle_real(addr, 0);
		}
		viewstat_all_breakpoint(type, addr);
	}
	return addr;
}

void np2break_reset()
{
	memset(np2breakflags, 0, sizeof(np2breakflags));
	listarray_clr(np2breakaddrs);
}

void np2break_destroy()
{
	listarray_destroy(np2breakaddrs);
}
/// -----------

/// Helper
/// ------
UINT unasm_next(UNASM una)	{

	UINT32 addr;
	UINT8 ins[16];

	addr = (CPU_STAT_PM ? CS_BASE : (CPU_CS<<4))+CPU_IP;
	memp_reads(addr, ins, 16);
	return unasm(una, ins, 16, FALSE, addr);
}
/// ------

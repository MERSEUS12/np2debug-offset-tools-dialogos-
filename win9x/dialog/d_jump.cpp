// D_JUMP.CPP - "Jump to dialogue" tool (Tools menu)
// ------
// While the game waits on a dialogue, the script interpreter keeps its read
// position in ESI (segment ES).  This tool
//   1. (optional) loads a .BIN file from a folder of the host PC into the
//      script buffer of the game (segment "Script segment"), and
//   2. sets ESI to the chosen entry offset, so the interpreter continues
//      from there when the game gets the next key press.
// ESI only holds the script pointer while the CPU runs inside the script
// interpreter, so the jump is applied by a hook (see break.c): the first time
// the game waits at a "\HA" command (ES = script segment, ES:ESI -> "HA\"),
// ESI is replaced once.  Optionally the hook can be limited to a list of
// CS:IP addresses.
// Optionally it also writes the offset into a pointer variable in memory.
// All values are remembered in np2jump.ini (next to the executable).

#include	"compiler.h"
#include	"resource.h"
#include	"np2.h"
#include	"dialog.h"
#include	"pccore.h"
#include	"cpucore.h"
#include	"break.h"

#define	JUMP_DEFSEG		0x4458

typedef struct {
	TCHAR	dir[MAX_PATH];
	TCHAR	bin[64];
	UINT32	off;
	INT32	adj;
	UINT32	seg;
	UINT32	ptr;
	UINT	is32;
	TCHAR	hook[96];
} JUMPCFG;

static const TCHAR s_sect[] = TEXT("jump");

static void jump_inipath(TCHAR *path, int len) {

	TCHAR	*p;

	GetModuleFileName(NULL, path, len);
	p = _tcsrchr(path, _T('\\'));
	if (p) {
		p++;
	}
	else {
		p = path;
	}
	lstrcpy(p, TEXT("np2jump.ini"));
}

// "1A2B3" (linear) or "3317:0A10" (segment:offset); optional leading '-'
static INT32 jump_parse(const TCHAR *s) {

	const TCHAR	*colon;
	INT32		sign;

	while (*s == _T(' ')) {
		s++;
	}
	sign = 1;
	if (*s == _T('-')) {
		sign = -1;
		s++;
	}
	colon = _tcschr(s, _T(':'));
	if (colon) {
		UINT32 seg = (UINT32)_tcstoul(s, NULL, 16);
		UINT32 off = (UINT32)_tcstoul(colon + 1, NULL, 16);
		return sign * (INT32)((seg << 4) + off);
	}
	return sign * (INT32)_tcstoul(s, NULL, 16);
}

static void jump_sethex(HWND hWnd, int item, UINT32 value) {

	TCHAR	work[32];

	wsprintf(work, TEXT("%X"), value);
	SetDlgItemText(hWnd, item, work);
}

static INT32 jump_gethex(HWND hWnd, int item) {

	TCHAR	work[64];

	work[0] = 0;
	GetDlgItemText(hWnd, item, work, 64);
	return jump_parse(work);
}

static void jump_inistr(const TCHAR *path, const TCHAR *key, const TCHAR *def,
														TCHAR *dst, int len) {

	GetPrivateProfileString(s_sect, key, def, dst, len, path);
}

static void jump_load(JUMPCFG *cfg) {

	TCHAR	path[MAX_PATH];
	TCHAR	work[32];

	jump_inipath(path, MAX_PATH);
	jump_inistr(path, TEXT("dir"), TEXT(""), cfg->dir, MAX_PATH);
	jump_inistr(path, TEXT("bin"), TEXT(""), cfg->bin, 64);
	jump_inistr(path, TEXT("off"), TEXT("0"), work, 32);
	cfg->off = (UINT32)jump_parse(work);
	jump_inistr(path, TEXT("adj"), TEXT("0"), work, 32);
	cfg->adj = jump_parse(work);
	jump_inistr(path, TEXT("seg"), TEXT("4458"), work, 32);
	cfg->seg = (UINT32)jump_parse(work);
	if (!cfg->seg) {
		cfg->seg = JUMP_DEFSEG;
	}
	jump_inistr(path, TEXT("ptr"), TEXT("0"), work, 32);
	cfg->ptr = (UINT32)jump_parse(work);
	cfg->is32 = (UINT)GetPrivateProfileInt(s_sect, TEXT("ptr32"), 0, path);
	jump_inistr(path, TEXT("hooks"), TEXT(""), cfg->hook, 96);
}

static void jump_save(const JUMPCFG *cfg) {

	TCHAR	path[MAX_PATH];
	TCHAR	work[32];

	jump_inipath(path, MAX_PATH);
	WritePrivateProfileString(s_sect, TEXT("dir"), cfg->dir, path);
	WritePrivateProfileString(s_sect, TEXT("bin"), cfg->bin, path);
	wsprintf(work, TEXT("%X"), cfg->off);
	WritePrivateProfileString(s_sect, TEXT("off"), work, path);
	if (cfg->adj < 0) {
		wsprintf(work, TEXT("-%X"), (UINT32)(-cfg->adj));
	}
	else {
		wsprintf(work, TEXT("%X"), (UINT32)cfg->adj);
	}
	WritePrivateProfileString(s_sect, TEXT("adj"), work, path);
	wsprintf(work, TEXT("%X"), cfg->seg);
	WritePrivateProfileString(s_sect, TEXT("seg"), work, path);
	wsprintf(work, TEXT("%X"), cfg->ptr);
	WritePrivateProfileString(s_sect, TEXT("ptr"), work, path);
	WritePrivateProfileString(s_sect, TEXT("ptr32"),
							cfg->is32 ? TEXT("1") : TEXT("0"), path);
	WritePrivateProfileString(s_sect, TEXT("hooks"), cfg->hook, path);
}

static void jump_collect(HWND hWnd, JUMPCFG *cfg) {

	GetDlgItemText(hWnd, IDC_JUMP_DIR, cfg->dir, MAX_PATH);
	GetDlgItemText(hWnd, IDC_JUMP_BIN, cfg->bin, 64);
	cfg->off = (UINT32)jump_gethex(hWnd, IDC_JUMP_OFF);
	cfg->adj = jump_gethex(hWnd, IDC_JUMP_ADD);
	cfg->seg = (UINT32)jump_gethex(hWnd, IDC_JUMP_SEG);
	cfg->ptr = (UINT32)jump_gethex(hWnd, IDC_JUMP_PTR);
	cfg->is32 = (IsDlgButtonChecked(hWnd, IDC_JUMP_32) == BST_CHECKED) ? 1 : 0;
	GetDlgItemText(hWnd, IDC_JUMP_HOOK, cfg->hook, 96);
}

// "SSSS:OOOO SSSS:OOOO ..." (separated by spaces, commas or semicolons)
// -> list of linear addresses.  Returns how many were found.
static UINT jump_parsehooks(const TCHAR *s, UINT32 *addrs, UINT max) {

	UINT		n;
	const TCHAR	*colon;
	TCHAR		tok[32];
	UINT		len;

	n = 0;
	while ((*s) && (n < max)) {
		while ((*s == _T(' ')) || (*s == _T(',')) || (*s == _T(';'))) {
			s++;
		}
		if (!*s) {
			break;
		}
		len = 0;
		while ((*s) && (*s != _T(' ')) && (*s != _T(',')) && (*s != _T(';')) &&
														(len < 31)) {
			tok[len++] = *s++;
		}
		tok[len] = 0;
		colon = _tcschr(tok, _T(':'));
		if (colon) {
			UINT32 seg = (UINT32)_tcstoul(tok, NULL, 16);
			UINT32 off = (UINT32)_tcstoul(colon + 1, NULL, 16);
			addrs[n++] = (seg << 4) + off;
		}
	}
	return n;
}

// shows ES, ESI, the next bytes of the script and the size of the buffer
static void jump_state(HWND hWnd) {

	TCHAR	work[1100];
	TCHAR	part[64];
	UINT32	esi;
	UINT32	es;
	UINT32	seg;
	UINT32	addr;
	UINT	i;

	esi = i386core.s.cpu_regs.reg[CPU_ESI_INDEX].d;
	es = (UINT32)i386core.s.cpu_regs.sreg[CPU_ES_INDEX];
	seg = (UINT32)jump_gethex(hWnd, IDC_JUMP_SEG);
	wsprintf(work, TEXT("CS:EIP=%04X:%08X  ES=%04X  ESI=%08X\r\n"),
			(UINT)i386core.s.cpu_regs.sreg[CPU_CS_INDEX],
			(UINT)i386core.s.cpu_regs.eip.d, es, esi);
	wsprintf(part, TEXT("Hook armed: %s   Jumps applied: %u\r\n"),
			np2jump_is_armed() ? TEXT("yes (waiting for the game to reach \\HA)") : TEXT("no"),
			np2jump_count());
	lstrcat(work, part);
	if (seg) {
		addr = (seg << 4) + esi;
		lstrcat(work, TEXT("Bytes at script segment:ESI: "));
		for (i = 0; i < 10; i++) {
			wsprintf(part, TEXT("%02X "), (UINT)memp_read8(addr + i));
			lstrcat(work, part);
		}
		lstrcat(work, TEXT("\r\n"));
		if (seg >= 0x100) {
			addr = (seg - 1) << 4;
			if ((memp_read8(addr) == 'M') || (memp_read8(addr) == 'Z')) {
				wsprintf(part, TEXT("Buffer size: %u bytes"),
									(UINT)memp_read16(addr + 3) * 16);
				lstrcat(work, part);
			}
		}
	}
	if (np2jump_trace_count()) {
		UINT16	tcs;
		UINT32	teip, told, tnew;
		UINT	ti;

		lstrcat(work, TEXT("After the jump, ESI changed:\r\n"));
		for (ti = 0; np2jump_trace_get(ti, &tcs, &teip, &told, &tnew); ti++) {
			wsprintf(part, TEXT("  %04X:%04X  ESI %X -> %X\r\n"),
											(UINT)tcs, (UINT)teip, told, tnew);
			lstrcat(work, part);
		}
	}
	if (es != seg) {
		lstrcat(work, TEXT("\r\nES is not the script segment at this instant; that is normal. ")
					TEXT("Jump applies by itself when the game waits at \\HA."));
	}
	SetDlgItemText(hWnd, IDC_JUMP_INFO, work);
}

static bool jump_loadbin(HWND hWnd, const JUMPCFG *cfg) {

	TCHAR	path[MAX_PATH + 80];
	HANDLE	fh;
	DWORD	size;
	DWORD	got;
	UINT32	base;
	UINT32	mcb;
	BYTE	*buf;
	int		len;

	if ((!cfg->dir[0]) || (cfg->seg < 0x100)) {
		MessageBox(hWnd, TEXT("Enter the folder with the .BIN files and the script segment."),
							TEXT("Jump to dialogue"), MB_OK | MB_ICONWARNING);
		return false;
	}
	lstrcpy(path, cfg->dir);
	len = lstrlen(path);
	if ((len) && (path[len - 1] != _T('\\')) && (path[len - 1] != _T('/'))) {
		lstrcat(path, TEXT("\\"));
	}
	lstrcat(path, cfg->bin);

	fh = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
											FILE_ATTRIBUTE_NORMAL, NULL);
	if (fh == INVALID_HANDLE_VALUE) {
		MessageBox(hWnd, path, TEXT("Cannot open file"), MB_OK | MB_ICONWARNING);
		return false;
	}
	size = GetFileSize(fh, NULL);
	if ((!size) || (size == INVALID_FILE_SIZE) || (size > 0x40000)) {
		CloseHandle(fh);
		MessageBox(hWnd, TEXT("The file is empty or too big."),
							TEXT("Jump to dialogue"), MB_OK | MB_ICONWARNING);
		return false;
	}

	base = cfg->seg << 4;
	mcb = base - 16;
	if ((memp_read8(mcb) == 'M') || (memp_read8(mcb) == 'Z')) {
		UINT32 cap = (UINT32)memp_read16(mcb + 3) << 4;
		if (size > cap) {
			TCHAR msg[160];
			CloseHandle(fh);
			wsprintf(msg, TEXT("The file (%u bytes) is bigger than the script buffer (%u bytes)."),
														(UINT)size, (UINT)cap);
			MessageBox(hWnd, msg, TEXT("Jump to dialogue"), MB_OK | MB_ICONWARNING);
			return false;
		}
	}
	else if (MessageBox(hWnd,
			TEXT("There is no memory block header before that segment. Load anyway?"),
			TEXT("Jump to dialogue"), MB_YESNO | MB_ICONWARNING) != IDYES) {
		CloseHandle(fh);
		return false;
	}

	buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size);
	if (!buf) {
		CloseHandle(fh);
		return false;
	}
	got = 0;
	if ((!ReadFile(fh, buf, size, &got, NULL)) || (got != size)) {
		HeapFree(GetProcessHeap(), 0, buf);
		CloseHandle(fh);
		MessageBox(hWnd, TEXT("Error reading the file."),
							TEXT("Jump to dialogue"), MB_OK | MB_ICONWARNING);
		return false;
	}
	CloseHandle(fh);
	memp_writes(base, buf, (UINT)size);
	HeapFree(GetProcessHeap(), 0, buf);
	return true;
}

static bool jump_apply(HWND hWnd) {

	JUMPCFG	cfg;
	UINT32	target;
	UINT32	addrs[NP2JUMP_MAXADDR];
	UINT	naddr;

	jump_collect(hWnd, &cfg);
	if (cfg.seg < 0x100) {
		MessageBox(hWnd, TEXT("Enter the script segment (4458 for this game)."),
							TEXT("Jump to dialogue"), MB_OK | MB_ICONWARNING);
		return false;
	}
	if (cfg.bin[0]) {
		if (!jump_loadbin(hWnd, &cfg)) {
			return false;
		}
	}
	target = (UINT32)((INT32)cfg.off + cfg.adj);
	naddr = jump_parsehooks(cfg.hook, addrs, NP2JUMP_MAXADDR);
	np2jump_arm((UINT16)cfg.seg, target, addrs, naddr);
	if (cfg.ptr) {
		if (cfg.is32) {
			memp_write32(cfg.ptr, target);
		}
		else {
			memp_write16(cfg.ptr, (REG16)target);
		}
	}
	jump_save(&cfg);
	if (np2stopemulate) {
		np2active_set(1);
	}
	return true;
}

LRESULT CALLBACK JumpDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	JUMPCFG	cfg;

	switch (msg) {
		case WM_INITDIALOG:
			jump_load(&cfg);
			SetDlgItemText(hWnd, IDC_JUMP_DIR, cfg.dir);
			SetDlgItemText(hWnd, IDC_JUMP_BIN, cfg.bin);
			jump_sethex(hWnd, IDC_JUMP_OFF, cfg.off);
			if (cfg.adj < 0) {
				TCHAR neg[32];
				wsprintf(neg, TEXT("-%X"), (UINT32)(-cfg.adj));
				SetDlgItemText(hWnd, IDC_JUMP_ADD, neg);
			}
			else {
				jump_sethex(hWnd, IDC_JUMP_ADD, (UINT32)cfg.adj);
			}
			jump_sethex(hWnd, IDC_JUMP_SEG, cfg.seg);
			jump_sethex(hWnd, IDC_JUMP_PTR, cfg.ptr);
			SetDlgItemText(hWnd, IDC_JUMP_HOOK, cfg.hook);
			CheckDlgButton(hWnd, IDC_JUMP_32, cfg.is32 ? BST_CHECKED : BST_UNCHECKED);
			jump_state(hWnd);
			SendDlgItemMessage(hWnd, IDC_JUMP_OFF, EM_SETSEL, 0, -1);
			SetFocus(GetDlgItem(hWnd, IDC_JUMP_OFF));
			return FALSE;

		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case IDC_JUMP_READ:
					jump_state(hWnd);
					return TRUE;

				case IDC_JUMP_CUR:
					{
						TCHAR cur[32];
						wsprintf(cur, TEXT("%04X:%04X"),
								(UINT)i386core.s.cpu_regs.sreg[CPU_CS_INDEX],
								(UINT)(i386core.s.cpu_regs.eip.d & 0xffff));
						SetDlgItemText(hWnd, IDC_JUMP_HOOK, cur);
					}
					return TRUE;

				case IDOK:
					if (jump_apply(hWnd)) {
						EndDialog(hWnd, IDOK);
					}
					return TRUE;

				case IDCANCEL:
					EndDialog(hWnd, IDCANCEL);
					return TRUE;
			}
			break;
	}
	return FALSE;
}

void dialog_jump(HWND hWnd) {

	DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_JUMPDLG), hWnd,
													(DLGPROC)JumpDialogProc);
}

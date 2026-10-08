// D_JUMP.CPP - "Jump to dialogue" tool (Tools menu)
// ------
// Writes (offset + add) into the game's script pointer, so that the next
// dialogue the script engine reads is the one at that offset of the .BIN.
// The pointer address, its size and the added base are remembered in
// np2jump.ini (next to the executable).

#include	"compiler.h"
#include	"resource.h"
#include	"np2.h"
#include	"dialog.h"
#include	"pccore.h"
#include	"cpucore.h"

typedef struct {
	UINT32	ptr;	// linear (physical) address of the script pointer
	UINT32	add;	// added to the offset typed by the user (e.g. .BIN base)
	UINT	is32;	// 0: 16-bit pointer, 1: 32-bit pointer
	UINT32	last;	// last offset used
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

// Accepts "1A2B3" (linear address) or "3317:0A10" (segment:offset)
static UINT32 jump_parse(const TCHAR *s) {

	const TCHAR	*colon;

	colon = _tcschr(s, _T(':'));
	if (colon) {
		UINT32 seg = (UINT32)_tcstoul(s, NULL, 16);
		UINT32 off = (UINT32)_tcstoul(colon + 1, NULL, 16);
		return (seg << 4) + off;
	}
	return (UINT32)_tcstoul(s, NULL, 16);
}

static UINT32 jump_readini(const TCHAR *path, const TCHAR *key) {

	TCHAR	work[32];

	GetPrivateProfileString(s_sect, key, TEXT(""), work, 32, path);
	return jump_parse(work);
}

static void jump_load(JUMPCFG *cfg) {

	TCHAR	path[MAX_PATH];

	jump_inipath(path, MAX_PATH);
	cfg->ptr = jump_readini(path, TEXT("ptr"));
	cfg->add = jump_readini(path, TEXT("add"));
	cfg->is32 = (UINT)GetPrivateProfileInt(s_sect, TEXT("ptr32"), 0, path);
	cfg->last = jump_readini(path, TEXT("last"));
}

static void jump_savekey(const TCHAR *path, const TCHAR *key, UINT32 value) {

	TCHAR	work[32];

	wsprintf(work, TEXT("%X"), value);
	WritePrivateProfileString(s_sect, key, work, path);
}

static void jump_save(const JUMPCFG *cfg) {

	TCHAR	path[MAX_PATH];

	jump_inipath(path, MAX_PATH);
	jump_savekey(path, TEXT("ptr"), cfg->ptr);
	jump_savekey(path, TEXT("add"), cfg->add);
	jump_savekey(path, TEXT("last"), cfg->last);
	WritePrivateProfileString(s_sect, TEXT("ptr32"),
							cfg->is32 ? TEXT("1") : TEXT("0"), path);
}

static void jump_sethex(HWND hWnd, int item, UINT32 value) {

	TCHAR	work[32];

	wsprintf(work, TEXT("%X"), value);
	SetDlgItemText(hWnd, item, work);
}

static UINT32 jump_gethex(HWND hWnd, int item) {

	TCHAR	work[32];

	work[0] = 0;
	GetDlgItemText(hWnd, item, work, 32);
	return jump_parse(work);
}

static void jump_showcurrent(HWND hWnd) {

	TCHAR	work[96];
	UINT32	ptr;
	UINT32	value;

	ptr = jump_gethex(hWnd, IDC_JUMP_PTR);
	if (!ptr) {
		SetDlgItemText(hWnd, IDC_JUMP_INFO,
									TEXT("Enter the pointer address first."));
		return;
	}
	if (IsDlgButtonChecked(hWnd, IDC_JUMP_32) == BST_CHECKED) {
		value = memp_read32(ptr);
	}
	else {
		value = memp_read16(ptr);
	}
	wsprintf(work, TEXT("Current pointer value: %X  (minus add: %X)"),
									value, value - jump_gethex(hWnd, IDC_JUMP_ADD));
	SetDlgItemText(hWnd, IDC_JUMP_INFO, work);
}

static bool jump_apply(HWND hWnd) {

	JUMPCFG	cfg;
	UINT32	off;
	UINT32	value;

	cfg.last = jump_gethex(hWnd, IDC_JUMP_OFF);
	cfg.ptr = jump_gethex(hWnd, IDC_JUMP_PTR);
	cfg.add = jump_gethex(hWnd, IDC_JUMP_ADD);
	cfg.is32 = (IsDlgButtonChecked(hWnd, IDC_JUMP_32) == BST_CHECKED) ? 1 : 0;

	if (!cfg.ptr) {
		MessageBox(hWnd, TEXT("Enter the address of the script pointer."),
									TEXT("Jump to dialogue"), MB_OK | MB_ICONWARNING);
		return false;
	}
	off = cfg.last;
	value = off + cfg.add;
	if (cfg.is32) {
		memp_write32(cfg.ptr, value);
	}
	else {
		memp_write16(cfg.ptr, (REG16)value);
	}
	jump_save(&cfg);
	return true;
}

LRESULT CALLBACK JumpDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	JUMPCFG	cfg;

	switch (msg) {
		case WM_INITDIALOG:
			jump_load(&cfg);
			jump_sethex(hWnd, IDC_JUMP_OFF, cfg.last);
			jump_sethex(hWnd, IDC_JUMP_PTR, cfg.ptr);
			jump_sethex(hWnd, IDC_JUMP_ADD, cfg.add);
			CheckDlgButton(hWnd, IDC_JUMP_32, cfg.is32 ? BST_CHECKED : BST_UNCHECKED);
			SendDlgItemMessage(hWnd, IDC_JUMP_OFF, EM_SETSEL, 0, -1);
			SetFocus(GetDlgItem(hWnd, IDC_JUMP_OFF));
			return FALSE;

		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case IDC_JUMP_READ:
					jump_showcurrent(hWnd);
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

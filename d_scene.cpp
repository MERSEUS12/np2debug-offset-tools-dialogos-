// D_SCENE.CPP - "Go to scene" tool (Debug menu)
// ------
// Intercepts the DOS "open file" call of the game (see np2scene_* in break.c).
//   1. Arms the target scene (e.g. H14_R07.BIN).
//   2. The emulator presses Enter by itself, at no-wait speed, until the game opens its next
//      scene script; that open is redirected to the target scene.
//   3. In the new scene it keeps pressing Enter until N dialogues have been skipped and
//      stops at the next one.
// The log of the files the game opened (with the memory address of each name) is shown in
// the information box; that address is the buffer where the game keeps the scene name.
// Values are remembered in np2jump.ini (section [scene]).  Name empty and N = 0 + "Go" cancels.

#include	"compiler.h"
#include	"resource.h"
#include	"np2.h"
#include	"dialog.h"
#include	"pccore.h"
#include	"cpucore.h"
#include	"break.h"

static const TCHAR s_scene[] = TEXT("scene");

static void scene_inipath(TCHAR *path, int len) {

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

static void scene_info(HWND hWnd) {

	TCHAR	work[1400];
	TCHAR	part[96];
	char	name[16];
	UINT32	lin;
	BOOL	redir;
	UINT	i;
	BOOL	sk_act, sk_fin;
	UINT	sk_press, sk_target;

	wsprintf(work, TEXT("Scene hook armed: %s   Redirects done: %u\r\n"),
				np2scene_is_armed() ? TEXT("yes (waiting for the game to open a scene)") : TEXT("no"),
				np2scene_redirect_count());
	np2skip_status(&sk_act, &sk_press, &sk_target, &sk_fin);
	if ((sk_act) || (sk_fin) || (sk_press)) {
		wsprintf(part, TEXT("Fast-forward: %s  skipped %u of %u dialogues\r\n"),
				(sk_act) ? TEXT("RUNNING") : ((sk_fin) ? TEXT("DONE") : TEXT("stopped")),
				sk_press, sk_target);
		lstrcat(work, part);
	}
	if (!np2scene_log_count()) {
		lstrcat(work, TEXT("\r\nNo file has been opened yet through INT 21h (AH=3Dh).\r\n"));
	}
	else {
		lstrcat(work, TEXT("\r\nFiles opened by the game (newest first):\r\n"));
	}
	for (i = 0; np2scene_log_get(i, name, &lin, &redir); i++) {
		wsprintf(part, TEXT("  %-14hs  name at linear %05X (%04X:%04X)%s\r\n"), name, (UINT)lin,
				(UINT)(lin >> 4), (UINT)(lin & 15),
				redir ? TEXT("  <- REDIRECTED") : TEXT(""));
		lstrcat(work, part);
	}
	SetDlgItemText(hWnd, IDC_SCENE_INFO, work);
}

static bool scene_go(HWND hWnd) {

	TCHAR	name[32];
	TCHAR	path[MAX_PATH];
	TCHAR	work[32];
	char	aname[32];
	UINT	skip;
	UINT32	seg;
	UINT	i;
	UINT	len;

	name[0] = 0;
	GetDlgItemText(hWnd, IDC_SCENE_NAME, name, 32);
	skip = GetDlgItemInt(hWnd, IDC_SCENE_SKIP, NULL, FALSE);
	work[0] = 0;
	GetDlgItemText(hWnd, IDC_SCENE_SEG, work, 32);
	seg = (UINT32)_tcstoul(work, NULL, 16);

	scene_inipath(path, MAX_PATH);
	WritePrivateProfileString(s_scene, TEXT("name"), name, path);
	wsprintf(work, TEXT("%u"), skip);
	WritePrivateProfileString(s_scene, TEXT("skip"), work, path);
	wsprintf(work, TEXT("%X"), seg);
	WritePrivateProfileString(s_scene, TEXT("seg"), work, path);

	np2skip_cancel();
	np2scene_cancel();
	if ((!name[0]) && (!skip)) {
		return true;							// cancel everything
	}
	if (seg < 0x100) {
		MessageBox(hWnd, TEXT("Enter the script segment (4458 for this game)."),
							TEXT("Go to scene"), MB_OK | MB_ICONWARNING);
		return false;
	}
	len = 0;
	for (i = 0; (name[i]) && (i < 31); i++) {
		aname[i] = (name[i] < 0x80) ? (char)name[i] : '?';
		len++;
	}
	aname[len] = '\0';
	if (name[0]) {
		bool dot = false;
		for (i = 0; i < len; i++) {
			if (aname[i] == '.') {
				dot = true;
			}
		}
		if ((!dot) && (len <= 8)) {
			lstrcpyA(aname + len, ".BIN");
			len += 4;
		}
		if (len > 12) {
			MessageBox(hWnd, TEXT("The file name must be a DOS 8.3 name (e.g. H14_R07.BIN)."),
							TEXT("Go to scene"), MB_OK | MB_ICONWARNING);
			return false;
		}
		np2scene_arm(aname);
		np2skip_arm((UINT16)seg, skip, 2);
	}
	else {
		np2skip_arm((UINT16)seg, skip, 0);		// no scene: fast-forward from here
	}
	if (np2stopemulate) {
		np2active_set(1);
	}
	return true;
}

LRESULT CALLBACK SceneDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	TCHAR	path[MAX_PATH];
	TCHAR	work[64];
	UINT	seg;

	switch (msg) {
		case WM_INITDIALOG:
			scene_inipath(path, MAX_PATH);
			GetPrivateProfileString(s_scene, TEXT("name"), TEXT(""), work, 64, path);
			SetDlgItemText(hWnd, IDC_SCENE_NAME, work);
			SetDlgItemInt(hWnd, IDC_SCENE_SKIP,
							(UINT)GetPrivateProfileInt(s_scene, TEXT("skip"), 0, path), FALSE);
			GetPrivateProfileString(s_scene, TEXT("seg"), TEXT(""), work, 64, path);
			if (!work[0]) {
				GetPrivateProfileString(TEXT("jump"), TEXT("seg"), TEXT("4458"), work, 64, path);
			}
			seg = (UINT)_tcstoul(work, NULL, 16);
			wsprintf(work, TEXT("%X"), seg ? seg : 0x4458);
			SetDlgItemText(hWnd, IDC_SCENE_SEG, work);
			scene_info(hWnd);
			SetFocus(GetDlgItem(hWnd, IDC_SCENE_NAME));
			return FALSE;

		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case IDC_SCENE_REFRESH:
					scene_info(hWnd);
					return TRUE;

				case IDOK:
					if (scene_go(hWnd)) {
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

void dialog_scene(HWND hWnd) {

	DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_SCENEDLG), hWnd,
													(DLGPROC)SceneDialogProc);
}

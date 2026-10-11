// D_SCENE.CPP - "Go to scene" tool (Debug menu)
// ------
// Intercepts the DOS "open file" call of the game (see np2scene_* in break.c).
//   1. Arms the target scene (e.g. H14_R07.BIN).
//   2. The emulator presses Enter by itself, without drawing the frames in between, until the game
//      opens its next scene script; that open is redirected to the target scene.
//   3. In the new scene it keeps pressing Enter until N dialogues have been skipped (or the script
//      position reaches the given offset) and stops at the next dialogue.
// "Scene file" also accepts FILE:ID (e.g. SNK_001.BIN:3), the format of the "Ir_a" column of the
// helper spreadsheet.  With an empty scene file it fast-forwards from the current dialogue.
// The information box shows the current position (scene, dialogue ID, script offset) and the log of
// the files opened by the game.  Values are remembered in np2jump.ini (section [scene]).
// Empty scene file + N = 0 + empty offset + "Go" cancels everything.

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

	TCHAR	work[2000];
	TCHAR	part[160];
	char	name[16];
	char	text[56];
	UINT32	lin;
	UINT32	esi;
	UINT32	bstart;
	int		id;
	BOOL	redir;
	UINT	i;
	BOOL	sk_act, sk_fin;
	UINT	sk_press, sk_target;

	work[0] = 0;
	if (np2track_current(&esi, &id, &bstart, text, sizeof(text))) {
		if (np2scene_last_script(name)) {
			wsprintf(part, TEXT("CURRENT: scene %hs  dialogue ID %d  (block at 0x%X, waiting at 0x%X)\r\n"),
														name, id, (UINT)bstart, (UINT)esi);
		}
		else {
			wsprintf(part, TEXT("CURRENT: dialogue ID %d  (block at 0x%X, waiting at 0x%X)\r\n"),
														id, (UINT)bstart, (UINT)esi);
		}
		lstrcat(work, part);
		wsprintf(part, TEXT("  text: %hs\r\n"), text);
		lstrcat(work, part);
	}
	else {
		lstrcat(work, TEXT("CURRENT: unknown yet (the game has not waited at a dialogue, or the segment is wrong)\r\n"));
	}
	wsprintf(part, TEXT("Scene hook armed: %s   Redirects done: %u\r\n"),
				np2scene_is_armed() ? TEXT("yes (waiting for the game to open a scene)") : TEXT("no"),
				np2scene_redirect_count());
	lstrcat(work, part);
	np2skip_status(&sk_act, &sk_press, &sk_target, &sk_fin);
	if ((sk_act) || (sk_fin) || (sk_press)) {
		wsprintf(part, TEXT("Fast-forward: %s  skipped %u dialogues\r\n"),
				(sk_act) ? TEXT("RUNNING") : ((sk_fin) ? TEXT("DONE") : TEXT("stopped")), sk_press);
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

	TCHAR	name[64];
	TCHAR	offtxt[32];
	TCHAR	path[MAX_PATH];
	TCHAR	work[32];
	char	aname[64];
	UINT	skip;
	UINT32	seg;
	UINT32	stopoff;
	UINT	i;
	UINT	len;
	bool	haveoff;

	name[0] = 0;
	GetDlgItemText(hWnd, IDC_SCENE_NAME, name, 64);
	skip = GetDlgItemInt(hWnd, IDC_SCENE_SKIP, NULL, FALSE);
	offtxt[0] = 0;
	GetDlgItemText(hWnd, IDC_SCENE_OFF, offtxt, 32);
	work[0] = 0;
	GetDlgItemText(hWnd, IDC_SCENE_SEG, work, 32);
	seg = (UINT32)_tcstoul(work, NULL, 16);

	scene_inipath(path, MAX_PATH);
	WritePrivateProfileString(s_scene, TEXT("name"), name, path);
	wsprintf(work, TEXT("%u"), skip);
	WritePrivateProfileString(s_scene, TEXT("skip"), work, path);
	WritePrivateProfileString(s_scene, TEXT("offset"), offtxt, path);
	wsprintf(work, TEXT("%X"), seg);
	WritePrivateProfileString(s_scene, TEXT("seg"), work, path);

	np2skip_cancel();
	np2scene_cancel();

	// "FILE:ID" shorthand: the number after the colon is N
	for (i = 0; name[i]; i++) {
		if (name[i] == _T(':')) {
			skip = (UINT)_tcstoul(name + i + 1, NULL, 10);
			name[i] = 0;
			break;
		}
	}
	haveoff = (offtxt[0] != 0);
	stopoff = haveoff ? (UINT32)_tcstoul(offtxt, NULL, 16) : 0;
	if ((!name[0]) && (!skip) && (!haveoff)) {
		return true;							// cancel everything
	}
	if (seg < 0x100) {
		MessageBox(hWnd, TEXT("Enter the script segment (4458 for this game)."),
							TEXT("Go to scene"), MB_OK | MB_ICONWARNING);
		return false;
	}
	np2track_setseg((UINT16)seg);
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
	}
	if (haveoff) {
		np2skip_arm_offset((UINT16)seg, stopoff, (name[0]) ? 2 : 0);
	}
	else {
		np2skip_arm((UINT16)seg, skip, (name[0]) ? 2 : 0);
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
			GetPrivateProfileString(s_scene, TEXT("offset"), TEXT(""), work, 64, path);
			SetDlgItemText(hWnd, IDC_SCENE_OFF, work);
			GetPrivateProfileString(s_scene, TEXT("seg"), TEXT(""), work, 64, path);
			if (!work[0]) {
				GetPrivateProfileString(TEXT("jump"), TEXT("seg"), TEXT("4458"), work, 64, path);
			}
			seg = (UINT)_tcstoul(work, NULL, 16);
			wsprintf(work, TEXT("%X"), seg ? seg : 0x4458);
			SetDlgItemText(hWnd, IDC_SCENE_SEG, work);
			np2track_setseg((UINT16)(seg ? seg : 0x4458));
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

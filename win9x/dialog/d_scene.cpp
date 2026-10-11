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
// "Dialogue #" uses the embedded dialogue map (dialogue_map.dlgmap) to convert a dialogue index
// to a script offset.  Empty scene file + N = 0 + empty offset + "Go" cancels everything.
// The information box shows the current position (scene, dialogue ID, script offset) and the log of
// the files opened by the game.  Values are remembered in np2jump.ini (section [scene]).

#include	"compiler.h"
#include	"resource.h"
#include	"np2.h"
#include	"dialog.h"
#include	"pccore.h"
#include	"cpucore.h"
#include	"break.h"

static const TCHAR s_scene[] = TEXT("scene");

// ---------- Dialogue map loader (binary .dlgmap format) ----------
// Format:
//   4 bytes: magic "DLGM"
//   2 bytes: version (1)
//   2 bytes: num_files (uint16_t)
//   Per file:
//     1 byte:  name_len (uint8_t)
//     name_len bytes: name (ASCII)
//     2 bytes: num_dialogues (uint16_t)
//     Per dialogue:
//       4 bytes: start_offset (uint32_t)
//       4 bytes: ha_offset (uint32_t, 0xFFFFFFFF if none)
//       1 byte:  prefix (uint8_t)

typedef struct {
    uint32_t start_offset;
    uint32_t ha_offset;
    uint8_t  prefix;
} DlgMapEntry;

typedef struct {
    const char *name;
    const DlgMapEntry *entries;
    uint16_t count;
} DlgMapScene;

static const DlgMapScene *g_dlgmap_scenes = NULL;
static uint16_t g_dlgmap_scene_count = 0;
static void *g_dlgmap_buffer = NULL; // raw buffer for cleanup

static BOOL dlgmap_load(const TCHAR *exe_dir) {
    TCHAR path[MAX_PATH];
    HANDLE fh;
    DWORD size, read;
    BYTE *buf;
    BYTE *p;
    uint16_t i, j, nfiles, ndlg;
    uint8_t namelen;

    lstrcpy(path, exe_dir);
    lstrcat(path, TEXT("\\dialogue_map.dlgmap"));

    fh = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    size = GetFileSize(fh, NULL);
    if (size == INVALID_FILE_SIZE || size < 8) {
        CloseHandle(fh);
        return FALSE;
    }
    buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size);
    if (!buf) {
        CloseHandle(fh);
        return FALSE;
    }
    if (!ReadFile(fh, buf, size, &read, NULL) || read != size) {
        HeapFree(GetProcessHeap(), 0, buf);
        CloseHandle(fh);
        return FALSE;
    }
    CloseHandle(fh);

    p = buf;
    // Magic
    if (p[0] != 'D' || p[1] != 'L' || p[2] != 'G' || p[3] != 'M') {
        HeapFree(GetProcessHeap(), 0, buf);
        return FALSE;
    }
    p += 4;
    // Version
    if (p[0] != 1 || p[1] != 0) {
        HeapFree(GetProcessHeap(), 0, buf);
        return FALSE;
    }
    p += 2;
    nfiles = *(uint16_t *)p; p += 2;

    // Allocate scene array
    g_dlgmap_scenes = (const DlgMapScene *)HeapAlloc(GetProcessHeap(), 0, nfiles * sizeof(DlgMapScene));
    if (!g_dlgmap_scenes) {
        HeapFree(GetProcessHeap(), 0, buf);
        return FALSE;
    }
    g_dlgmap_scene_count = nfiles;
    g_dlgmap_buffer = buf;

    for (i = 0; i < nfiles; i++) {
        namelen = *p++; 
        g_dlgmap_scenes[i].name = (const char *)p;
        p += namelen;
        ndlg = *(uint16_t *)p; p += 2;
        g_dlgmap_scenes[i].count = ndlg;
        g_dlgmap_scenes[i].entries = (const DlgMapEntry *)p;
        p += ndlg * sizeof(DlgMapEntry);
    }
    return TRUE;
}

static void dlgmap_free(void) {
    if (g_dlgmap_buffer) {
        HeapFree(GetProcessHeap(), 0, g_dlgmap_buffer);
        g_dlgmap_buffer = NULL;
    }
    if (g_dlgmap_scenes) {
        HeapFree(GetProcessHeap(), 0, (void *)g_dlgmap_scenes);
        g_dlgmap_scenes = NULL;
    }
    g_dlgmap_scene_count = 0;
}

// Case-insensitive string compare for ASCII
static int dlgmap_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? *a + 0x20 : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? *b + 0x20 : *b;
        if (ca != cb) return (ca - cb);
        a++; b++;
    }
    return (*a - *b);
}

static uint32_t dlgmap_lookup_offset(const char *scene_name, int dialogue_idx) {
    if (!g_dlgmap_scenes) return 0;
    for (uint16_t i = 0; i < g_dlgmap_scene_count; i++) {
        if (dlgmap_strcasecmp(g_dlgmap_scenes[i].name, scene_name) == 0) {
            if (dialogue_idx >= 0 && dialogue_idx < g_dlgmap_scenes[i].count) {
                return g_dlgmap_scenes[i].entries[dialogue_idx].start_offset;
            }
            break;
        }
    }
    return 0;
}

// ---------- INI helpers ----------
static void scene_inipath(TCHAR *path, int len) {
    TCHAR *p;
    GetModuleFileName(NULL, path, len);
    p = _tcsrchr(path, _T('\\'));
    if (p) p++; else p = path;
    lstrcpy(p, TEXT("np2jump.ini"));
}

// ---------- Info box ----------
static void scene_info(HWND hWnd) {
    TCHAR work[4200];
    TCHAR part[260];
    char name[16];
    char text[56];
    UINT32 lin, esi, bstart;
    int id;
    BOOL redir;
    UINT i;
    BOOL sk_act, sk_fin;
    UINT sk_press, sk_target;

    work[0] = 0;
    if (np2track_current(&esi, &id, &bstart, text, sizeof(text))) {
        if (np2scene_last_script(name)) {
            wsprintf(part, TEXT("CURRENT: scene %hs  dialogue ID %d  (block at 0x%X, waiting at 0x%X)\r\n"),
                                                    name, id, (UINT)bstart, (UINT)esi);
        } else {
            wsprintf(part, TEXT("CURRENT: dialogue ID %d  (block at 0x%X, waiting at 0x%X)\r\n"),
                                                    id, (UINT)bstart, (UINT)esi);
        }
        lstrcat(work, part);
        wsprintf(part, TEXT("  text: %hs\r\n"), text);
        lstrcat(work, part);
    } else {
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
    if (np2skip_is_waiting()) {
        lstrcat(work, TEXT("Fast-forward: WAITING - the game is not at a dialogue (map or menu). Play it normally; it resumes at the next dialogue.\r\n"));
    }
    if (!np2scene_log_count()) {
        lstrcat(work, TEXT("\r\nNo file has been opened yet through INT 21h (AH=3Dh).\r\n"));
    } else {
        lstrcat(work, TEXT("\r\nFiles opened by the game (newest first):\r\n"));
    }
    for (i = 0; np2scene_log_get(i, name, &lin, &redir); i++) {
        wsprintf(part, TEXT("  %-14hs  name at linear %05X (%04X:%04X)%s\r\n"), name, (UINT)lin,
                (UINT)(lin >> 4), (UINT)(lin & 15),
                redir ? TEXT("  <- REDIRECTED") : TEXT(""));
        lstrcat(work, part);
        { // for scene scripts, show who opened them
            UINT16 ccs, cip;
            UINT8 ctx[16];
            UINT n = (UINT)lstrlenA(name);
            if ((n > 4) && (name[n - 4] == '.') && ((name[n - 3] | 0x20) == 'b') &&
                (np2scene_log_caller(i, &ccs, &cip, ctx))) {
                wsprintf(part, TEXT("      called from %04X:%04X   code before it: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n"),
                        (UINT)ccs, (UINT)cip, ctx[0], ctx[1], ctx[2], ctx[3], ctx[4], ctx[5], ctx[6], ctx[7],
                        ctx[8], ctx[9], ctx[10], ctx[11], ctx[12], ctx[13], ctx[14], ctx[15]);
                lstrcat(work, part);
            }
        }
    }
    SetDlgItemText(hWnd, IDC_SCENE_INFO, work);
}

// ---------- Go action ----------
static bool scene_go(HWND hWnd) {
    TCHAR name[64];
    TCHAR offtxt[32];
    TCHAR path[MAX_PATH];
    TCHAR work[32];
    char aname[64];
    UINT skip, dlg_idx;
    UINT32 seg, stopoff;
    UINT i, len;
    bool haveoff;

    name[0] = 0;
    GetDlgItemText(hWnd, IDC_SCENE_NAME, name, 64);
    skip = GetDlgItemInt(hWnd, IDC_SCENE_SKIP, NULL, FALSE);
    dlg_idx = GetDlgItemInt(hWnd, IDC_SCENE_DLG, NULL, FALSE);
    offtxt[0] = 0;
    GetDlgItemText(hWnd, IDC_SCENE_OFF, offtxt, 32);
    work[0] = 0;
    GetDlgItemText(hWnd, IDC_SCENE_SEG, work, 32);
    seg = (UINT32)_tcstoul(work, NULL, 16);

    scene_inipath(path, MAX_PATH);
    WritePrivateProfileString(s_scene, TEXT("name"), name, path);
    wsprintf(work, TEXT("%u"), skip);
    WritePrivateProfileString(s_scene, TEXT("skip"), work, path);
    wsprintf(work, TEXT("%u"), dlg_idx);
    WritePrivateProfileString(s_scene, TEXT("dialogue"), work, path);
    WritePrivateProfileString(s_scene, TEXT("offset"), offtxt, path);
    WritePrivateProfileString(s_scene, TEXT("mash"), (IsDlgButtonChecked(hWnd, IDC_SCENE_MASH) == BST_CHECKED) ? TEXT("1") : TEXT("0"), path);
    wsprintf(work, TEXT("%X"), seg);
    WritePrivateProfileString(s_scene, TEXT("seg"), work, path);

    np2skip_cancel();
    np2scene_cancel();
    np2skip_set_mash(IsDlgButtonChecked(hWnd, IDC_SCENE_MASH) == BST_CHECKED);

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

    // If Dialogue # is given and no manual offset, look up the offset from the map
    if (!haveoff && dlg_idx > 0 && name[0]) {
        len = 0;
        for (i = 0; (name[i]) && (i < 31); i++) {
            aname[i] = (name[i] < 0x80) ? (char)name[i] : '?';
            len++;
        }
        aname[len] = '\0';
        // Ensure .BIN extension for lookup
        if (!strchr(aname, '.')) {
            lstrcatA(aname, ".BIN");
        }
        stopoff = dlgmap_lookup_offset(aname, (int)dlg_idx - 1); // 1-based in UI
        if (stopoff) {
            haveoff = true;
        }
    }

    if ((!name[0]) && (!skip) && (!haveoff)) {
        return true; // cancel everything
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
            if (aname[i] == '.') dot = true;
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
    } else {
        np2skip_arm((UINT16)seg, skip, (name[0]) ? 2 : 0);
    }
    if (np2stopemulate) {
        np2active_set(1);
    }
    return true;
}

// ---------- Dialog proc ----------
LRESULT CALLBACK SceneDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TCHAR path[MAX_PATH];
    TCHAR work[64];
    UINT seg;

    switch (msg) {
        case WM_INITDIALOG: {
            scene_inipath(path, MAX_PATH);
            GetPrivateProfileString(s_scene, TEXT("name"), TEXT(""), work, 64, path);
            SetDlgItemText(hWnd, IDC_SCENE_NAME, work);
            SetDlgItemInt(hWnd, IDC_SCENE_SKIP, (UINT)GetPrivateProfileInt(s_scene, TEXT("skip"), 0, path), FALSE);
            SetDlgItemInt(hWnd, IDC_SCENE_DLG, (UINT)GetPrivateProfileInt(s_scene, TEXT("dialogue"), 0, path), FALSE);
            GetPrivateProfileString(s_scene, TEXT("offset"), TEXT(""), work, 64, path);
            SetDlgItemText(hWnd, IDC_SCENE_OFF, work);
            CheckDlgButton(hWnd, IDC_SCENE_MASH, GetPrivateProfileInt(s_scene, TEXT("mash"), 0, path) ? BST_CHECKED : BST_UNCHECKED);
            GetPrivateProfileString(s_scene, TEXT("seg"), TEXT(""), work, 64, path);
            if (!work[0]) {
                GetPrivateProfileString(TEXT("jump"), TEXT("seg"), TEXT("4458"), work, 64, path);
            }
            seg = (UINT)_tcstoul(work, NULL, 16);
            wsprintf(work, TEXT("%X"), seg ? seg : 0x4458);
            SetDlgItemText(hWnd, IDC_SCENE_SEG, work);
            np2track_setseg((UINT16)(seg ? seg : 0x4458));

            // Load dialogue map
            TCHAR exe_dir[MAX_PATH];
            GetModuleFileName(NULL, exe_dir, MAX_PATH);
            TCHAR *p = _tcsrchr(exe_dir, _T('\\'));
            if (p) *p = 0; else exe_dir[0] = 0;
            if (!dlgmap_load(exe_dir)) {
                // Not fatal, just no dialogue # lookup
            }
            scene_info(hWnd);
            SetFocus(GetDlgItem(hWnd, IDC_SCENE_NAME));
            return FALSE;
        }
        case WM_NCDESTROY:
            dlgmap_free();
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
    DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_SCENEDLG), hWnd, (DLGPROC)SceneDialogProc);
}
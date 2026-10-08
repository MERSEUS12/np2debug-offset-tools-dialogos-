#include "compiler.h"
#include "resource.h"
#include "dialog.h"
#include "np2.h"
#include "cpucore.h"
#include "pico.h"

static INT_PTR CALLBACK JumpDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(hDlg, IDC_OFFSET_INPUT, "279"); // Offset de ejemplo por defecto
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: // Al presionar el botón Jump
            {
                char szOffset[32];
                GetDlgItemText(hDlg, IDC_OFFSET_INPUT, szOffset, sizeof(szOffset));

                // 1. Convertir el offset hexadecimal ingresado (ej: "279" -> 0x000279)
                UINT32 binOffset = (UINT32)strtoul(szOffset, NULL, 16);

                // 2. Pausar un instante la emulación para realizar la modificación de forma limpia
                np2_active = 0;

                // 3. Modificar el registro de índice ESI (apunta al offset dentro del segmento DS)
                CPU_ESI = binOffset;

                // 4. Reanudar la emulación
                np2_active = 1;

                EndDialog(hDlg, IDOK);
            }
            return TRUE;

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void DialogJumpToOffset(HWND hwndParent) {
    DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_JUMP_DIALOG), hwndParent, JumpDlgProc);
}

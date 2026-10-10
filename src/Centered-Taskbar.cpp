// Centered-Taskbar.cpp - Centers the Windows 10 taskbar icons (animated, low CPU)
// Native C++ (pure Win32). See README.md for build instructions (needs windres for the icon/manifest).
//
// How it works: the taskbar's task list (MSTaskListWClass) is a child window of Explorer; its buttons are
// measured with UI Automation and the whole list is shifted so the buttons sit in the middle of the screen,
// with a short eased animation whenever buttons are added or removed.
//
// Idle cost goals: no timer faster than 1.5 s outside short bursts; one UIA measurement per poll.

#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uiautomationclient.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <climits>

#define WM_TRAY    (WM_APP + 1)
#define T_POLL     1
#define T_REINIT   3
#define T_ANIM     4
#define T_PROBE    6
#define ICON_ID    1

#define APP_NAME    L"Centered-Taskbar"
#define APP_VERSION L"1.0.0"

// ============================================================ config
struct Config { bool center = true; int anim = 2; };
static Config g_cfg;
static HINSTANCE g_hInst;
static HWND g_main;
static wchar_t g_dir[MAX_PATH];
static UINT g_msgTaskbarCreated, g_msgShell;
static HICON g_trayIcon;

static std::wstring CfgPath() { return std::wstring(g_dir) + L"\\config.ini"; }
static void InitDir() {
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, g_dir);
    wcscat_s(g_dir, L"\\" APP_NAME);
    CreateDirectoryW(g_dir, NULL);
}
static void LoadCfg() {
    auto p = CfgPath();
    g_cfg.center = GetPrivateProfileIntW(L"CenteredTaskbar", L"CenterTaskbar", 1, p.c_str()) != 0;
    g_cfg.anim   = std::max(0, std::min(3, (int)GetPrivateProfileIntW(L"CenteredTaskbar", L"AnimSpeed", 2, p.c_str())));
}
static void SaveCfg() {
    auto p = CfgPath();
    WritePrivateProfileStringW(L"CenteredTaskbar", L"CenterTaskbar", g_cfg.center ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"CenteredTaskbar", L"AnimSpeed", std::to_wstring(g_cfg.anim).c_str(), p.c_str());
}
static void TrimMem() { SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1); }

// ---- inicio automatico (clave "Run" del usuario: no necesita permisos de administrador)
static const wchar_t* RUNKEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static bool Autostart() {
    HKEY k; bool r = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUNKEY, 0, KEY_READ, &k) == 0) {
        r = RegQueryValueExW(k, APP_NAME, NULL, NULL, NULL, NULL) == 0;
        RegCloseKey(k);
    }
    return r;
}
static void RunHidden(const std::wstring& cmd) {
    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi = {};
    std::wstring c = cmd;
    if (!CreateProcessW(NULL, &c[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return;
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
}
static void SetAutostart(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUNKEY, 0, KEY_WRITE, &k) != 0) return;
    RegDeleteValueW(k, L"StartMod");                              // limpieza: nombre usado por una version anterior
    if (on) {
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
        std::wstring v = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(k, APP_NAME, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else RegDeleteValueW(k, APP_NAME);
    RegCloseKey(k);
    // limpieza: una version anterior (StartMod, con menu Inicio) creaba una tarea programada con administrador
    RunHidden(L"schtasks.exe /Delete /TN StartMod /F");
}

// ============================================================ menu emergente propio (oscuro, plano)
// Los menus nativos de Windows son siempre blancos; este los reemplaza para el icono de la bandeja.
static int g_dpi = 96;
static int S(int v) { return MulDiv(v, g_dpi, 96); }
static RECT MkRect(int l, int t, int r, int b) { RECT x = { l, t, r, b }; return x; }
struct Pal { COLORREF bg, hot, text, sub, border; };
static const Pal kPal = { RGB(32, 32, 32), RGB(60, 60, 60), RGB(255, 255, 255), RGB(165, 165, 165), RGB(56, 56, 56) };

static void Fill(HDC dc, RECT r, COLORREF c) { HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b); }
static void Outline(HDC dc, RECT r, COLORREF c) { HBRUSH b = CreateSolidBrush(c); FrameRect(dc, &r, b); DeleteObject(b); }
static void Txt(HDC dc, const std::wstring& s, RECT r, HFONT f, COLORREF c, UINT fmt) {
    SelectObject(dc, f); SetTextColor(dc, c); DrawTextW(dc, s.c_str(), -1, &r, fmt | DT_NOPREFIX);
}
static HFONT MkFont(int px, int weight, const wchar_t* face) {
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

struct MItem { int id = 0; std::wstring text; bool checked = false, disabled = false, sep = false, header = false; };
static void MAdd(std::vector<MItem>& v, int id, const std::wstring& text, bool checked = false) {
    MItem m; m.id = id; m.text = text; m.checked = checked; v.push_back(m);
}
static void MSep(std::vector<MItem>& v) { MItem m; m.sep = true; m.disabled = true; v.push_back(m); }
static void MHeader(std::vector<MItem>& v, const std::wstring& text) {
    MItem m; m.header = true; m.disabled = true; m.text = text; v.push_back(m);
}

static bool g_popupActive;
static HWND s_popWnd;
static bool s_popDone;
static int s_popRes, s_popHot;
static std::vector<MItem>* s_popItems;
static std::vector<int> s_popTop;
static HFONT s_popFont, s_popIcon;

static int PopHit(POINT p) {
    if (!s_popWnd || !s_popItems) return -1;
    RECT rc; GetClientRect(s_popWnd, &rc);
    if (p.x < 0 || p.x >= rc.right) return -1;
    for (size_t i = 0; i < s_popItems->size(); i++)
        if (p.y >= s_popTop[i] && p.y < s_popTop[i + 1]) {
            const MItem& m = (*s_popItems)[i];
            return (m.sep || m.header || m.disabled) ? -1 : (int)i;
        }
    return -1;
}
static void PopMove(int dir) {
    int n = (int)s_popItems->size(); if (!n) return;
    int i = s_popHot;
    for (int k = 0; k < n; k++) {
        i = (i < 0) ? (dir > 0 ? 0 : n - 1) : (i + dir + n) % n;
        const MItem& m = (*s_popItems)[i];
        if (!m.sep && !m.header && !m.disabled) { s_popHot = i; return; }
    }
}
static LRESULT CALLBACK PopupProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC dc = CreateCompatibleDC(wdc);
        HBITMAP bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(dc, bmp);
        SetBkMode(dc, TRANSPARENT);
        Fill(dc, rc, kPal.bg);
        if (s_popItems) for (size_t i = 0; i < s_popItems->size(); i++) {
            const MItem& it = (*s_popItems)[i];
            int top = s_popTop[i], bot = s_popTop[i + 1];
            if (it.sep) { Fill(dc, MkRect(S(12), top + S(4), rc.right - S(12), top + S(5)), kPal.border); continue; }
            if (it.header) {
                Txt(dc, it.text, MkRect(S(14), top, rc.right - S(10), bot), s_popFont, kPal.sub, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                continue;
            }
            if ((int)i == s_popHot && !it.disabled) Fill(dc, MkRect(S(4), top, rc.right - S(4), bot), kPal.hot);
            COLORREF c = it.disabled ? kPal.sub : kPal.text;
            if (it.checked) Txt(dc, L"\uE73E", MkRect(S(10), top, S(30), bot), s_popIcon, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            Txt(dc, it.text, MkRect(S(34), top, rc.right - S(10), bot), s_popFont, c, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        Outline(dc, rc, kPal.border);
        BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob); DeleteObject(bmp); DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        int i = PopHit(p);
        if (i != s_popHot) { s_popHot = i; InvalidateRect(h, NULL, FALSE); }
        return 0;
    }
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: {                     // clic fuera = cerrar
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        RECT rc; GetClientRect(h, &rc);
        if (!PtInRect(&rc, p)) { s_popRes = 0; s_popDone = true; }
        return 0;
    }
    case WM_LBUTTONUP: case WM_RBUTTONUP: {
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        int i = PopHit(p);
        if (i >= 0) { s_popRes = (*s_popItems)[i].id; s_popDone = true; }
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}
// Muestra el menu en (x,y) de pantalla; devuelve el id elegido o 0. Usa su propio bucle de mensajes.
static int PopupMenu(std::vector<MItem>& items, int x, int y) {
    if (items.empty() || g_popupActive) return 0;
    s_popFont = MkFont(S(14), FW_NORMAL, L"Segoe UI");
    s_popIcon = MkFont(S(12), FW_NORMAL, L"Segoe MDL2 Assets");
    HDC sdc = GetDC(NULL); HGDIOBJ of = SelectObject(sdc, s_popFont);
    int maxw = 0;
    s_popTop.assign(1, S(4));
    for (auto& mi : items) {
        int hgt = mi.sep ? S(9) : mi.header ? S(28) : S(32);
        if (!mi.sep) {
            SIZE sz = { 0, 0 };
            GetTextExtentPoint32W(sdc, mi.text.c_str(), (int)mi.text.size(), &sz);
            maxw = std::max<int>(maxw, sz.cx);
        }
        s_popTop.push_back(s_popTop.back() + hgt);
    }
    SelectObject(sdc, of); ReleaseDC(NULL, sdc);
    int W = std::max(S(200), maxw + S(34) + S(24)), H = s_popTop.back() + S(4);
    POINT pt = { x, y };
    MONITORINFO mon = { sizeof mon };
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mon);
    int px = x, py = y;
    if (px + W > mon.rcMonitor.right) px = mon.rcMonitor.right - W;       // no salirse de la pantalla
    if (px < mon.rcMonitor.left) px = mon.rcMonitor.left;
    if (py + H > mon.rcMonitor.bottom) py = std::max<int>(mon.rcMonitor.top, y - H);   // cerca de la barra: abrir hacia arriba
    if (py < mon.rcMonitor.top) py = mon.rcMonitor.top;

    s_popItems = &items; s_popHot = -1; s_popDone = false; s_popRes = 0;
    s_popWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"CenteredTaskbarPopup", L"", WS_POPUP,
                               px, py, W, H, NULL, NULL, g_hInst, NULL);
    if (!s_popWnd) { DeleteObject(s_popFont); DeleteObject(s_popIcon); s_popItems = NULL; return 0; }
    g_popupActive = true;
    ShowWindow(s_popWnd, SW_SHOWNOACTIVATE);
    SetCapture(s_popWnd);
    MSG msg;
    while (!s_popDone) {
        int r = GetMessageW(&msg, NULL, 0, 0);
        if (r <= 0) { if (r == 0) PostQuitMessage((int)msg.wParam); break; }
        if (msg.message == WM_KEYDOWN) {                            // teclado
            if (msg.wParam == VK_ESCAPE) { s_popRes = 0; s_popDone = true; continue; }
            if (msg.wParam == VK_UP)   { PopMove(-1); InvalidateRect(s_popWnd, NULL, FALSE); continue; }
            if (msg.wParam == VK_DOWN) { PopMove(1);  InvalidateRect(s_popWnd, NULL, FALSE); continue; }
            if (msg.wParam == VK_RETURN) {
                if (s_popHot >= 0) { s_popRes = items[s_popHot].id; s_popDone = true; }
                continue;
            }
        }
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    ReleaseCapture();
    DestroyWindow(s_popWnd); s_popWnd = NULL; s_popItems = NULL;
    DeleteObject(s_popFont); DeleteObject(s_popIcon); s_popFont = s_popIcon = NULL;
    g_popupActive = false;
    return s_popRes;
}

// ============================================================ taskbar centering
// Estrategia de bajo consumo:
//  * Reposo: un solo sondeo UIA cada 1.5 s (una medicion, sin timer rapido).
//  * Eventos del shell (ventana creada/cerrada/activada) -> sondeo o rafaga corta.
//  * Hook WinEvent sobre la lista de tareas: si Explorer la mueve (la devuelve a x=0),
//    se restaura al instante para que no se vea el salto, y luego se anima al nuevo centro.
static void StartBurst(DWORD ms);

static IUIAutomation* g_uia;
static IUIAutomationCondition* g_true;
static IUIAutomationCacheRequest* g_cr;
static IUIAutomationElement* g_tlEl;
static HWND g_tray, g_rebar, g_tlSw, g_tl;
static HWINEVENTHOOK g_winEv;
static bool g_animOn, g_haveTarget, g_haveSet;
static ULONGLONG g_burstEnd, g_lastTick, g_lastMeasure, g_fixWindow;
static int g_cachedTarget, g_lastSetX, g_fixCount;

// Velocidad: 0 = sin animacion, 1 = rapida, 2 = normal, 3 = suave (constante de tiempo en ms)
static double Tau() { static const double t[4] = { 0.0, 30.0, 55.0, 95.0 }; return t[std::max(0, std::min(3, g_cfg.anim))]; }

static bool FindTaskbar() {
    g_tray  = FindWindowW(L"Shell_TrayWnd", NULL);
    g_rebar = g_tray ? FindWindowExW(g_tray, NULL, L"ReBarWindow32", NULL) : NULL;
    g_tlSw  = g_rebar ? FindWindowExW(g_rebar, NULL, L"MSTaskSwWClass", NULL) : NULL;
    g_tl    = g_tlSw ? FindWindowExW(g_tlSw, NULL, L"MSTaskListWClass", NULL) : NULL;
    return g_tl != NULL;
}
static bool CurrentPos(int& x, int& y) {
    RECT wr; if (!g_tl || !GetWindowRect(g_tl, &wr)) return false;
    POINT c = { wr.left, wr.top }; ScreenToClient(g_tlSw, &c);
    x = c.x; y = c.y; return true;
}
static void SetTLX(int x, int y) {
    SetWindowPos(g_tl, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    g_lastSetX = x; g_haveSet = true;
}
static void ResetTaskbar() {                                   // devolver la lista a su sitio original (x = 0)
    int x, y;
    if (g_tl && IsWindow(g_tl) && CurrentPos(x, y))
        SetWindowPos(g_tl, NULL, 0, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    g_haveSet = false;
}
// Una sola llamada UIA (propiedades en cache) para medir todos los botones
static bool Measure(LONG& minL, LONG& maxR) {
    if (!g_uia || !g_tlEl) return false;
    IUIAutomationElementArray* arr = NULL;
    HRESULT hr = g_cr ? g_tlEl->FindAllBuildCache(TreeScope_Children, g_true, g_cr, &arr)
                      : g_tlEl->FindAll(TreeScope_Children, g_true, &arr);
    if (FAILED(hr) || !arr) return false;
    int n = 0; arr->get_Length(&n);
    minL = LONG_MAX; maxR = LONG_MIN;
    for (int i = 0; i < n; i++) {
        IUIAutomationElement* c = NULL;
        if (SUCCEEDED(arr->GetElement(i, &c)) && c) {
            RECT r;
            HRESULT h2 = g_cr ? c->get_CachedBoundingRectangle(&r) : c->get_CurrentBoundingRectangle(&r);
            if (SUCCEEDED(h2) && r.right > r.left) {
                minL = std::min<LONG>(minL, r.left); maxR = std::max<LONG>(maxR, r.right);
            }
            c->Release();
        }
    }
    arr->Release();
    return n > 0 && minL <= maxR;
}
// Donde deberia estar la ventana de la lista (coords del padre)
static bool ComputeTarget(int& targetX) {
    if (!g_cfg.center || !g_tl || !IsWindow(g_tl) || !IsWindow(g_tray)) return false;
    RECT tr; GetWindowRect(g_tray, &tr);
    if ((tr.right - tr.left) < (tr.bottom - tr.top)) return false;      // barra vertical: no tocar
    LONG minL, maxR; if (!Measure(minL, maxR)) return false;
    RECT rb, tl; GetWindowRect(g_rebar, &rb); GetWindowRect(g_tl, &tl);
    MONITORINFO mi = { sizeof mi };
    GetMonitorInfoW(MonitorFromWindow(g_tray, MONITOR_DEFAULTTOPRIMARY), &mi);
    LONG content = maxR - minL, internal = minL - tl.left;
    LONG want = (mi.rcMonitor.left + mi.rcMonitor.right) / 2 - content / 2;
    if (want + content > rb.right) want = rb.right - content;
    if (want < rb.left) want = rb.left;
    POINT t = { want - internal, tl.top }; ScreenToClient(g_tlSw, &t);
    targetX = t.x;
    return true;
}
static void StopAnim() { KillTimer(g_main, T_ANIM); g_animOn = false; }
static void AnimTick() {
    ULONGLONG now = GetTickCount64();
    double dt = (double)(now - g_lastTick); g_lastTick = now;
    if (!g_cfg.center) { StopAnim(); return; }
    if (!g_tl || !IsWindow(g_tl)) { StopAnim(); SetTimer(g_main, T_REINIT, 1000, NULL); return; }
    if (now > g_burstEnd + 1000) { StopAnim(); return; }               // tope duro: nunca quedarse girando
    int cur, y; if (!CurrentPos(cur, y)) { StopAnim(); return; }
    if (!g_haveTarget || now - g_lastMeasure >= 30) {                  // medir cada ~30 ms (lo caro)
        g_lastMeasure = now;
        int t;
        if (ComputeTarget(t)) {
            if (!g_haveTarget || abs(t - g_cachedTarget) > 1) g_burstEnd = std::max<ULONGLONG>(g_burstEnd, now + 250);
            g_cachedTarget = t; g_haveTarget = true;
        } else if (!g_haveTarget) { if (now > g_burstEnd) StopAnim(); return; }
    }
    int diff = g_cachedTarget - cur;
    if (diff != 0) {
        double tau = Tau();
        int nx;
        if (tau <= 0.0) nx = g_cachedTarget;                           // sin animacion
        else {
            double step = diff * (1.0 - exp(-dt / tau));
            if (fabs(step) < 1.0) step = diff > 0 ? 1.0 : -1.0;
            nx = cur + (int)(step > 0 ? floor(step + 0.5) : ceil(step - 0.5));
            if (abs(g_cachedTarget - nx) <= 1) nx = g_cachedTarget;
        }
        SetTLX(nx, y);
    } else if (now > g_burstEnd) StopAnim();
}
static void StartBurst(DWORD ms) {
    if (!g_cfg.center || !g_uia) return;
    ULONGLONG now = GetTickCount64();
    g_burstEnd = std::max<ULONGLONG>(g_burstEnd, now + ms);
    if (!g_animOn) { g_animOn = true; g_haveTarget = false; g_lastTick = now - 16; SetTimer(g_main, T_ANIM, 16, NULL); }
}
// Sondeo barato: una medicion. Solo arranca la animacion si de verdad hay que mover algo.
static void Probe() {
    if (!g_cfg.center || !g_uia || g_animOn) return;
    if (!g_tl || !IsWindow(g_tl)) { SetTimer(g_main, T_REINIT, 500, NULL); return; }
    int t, cur, y;
    if (!ComputeTarget(t) || !CurrentPos(cur, y)) return;
    if (abs(t - cur) > 1) StartBurst(300);
}
// Explorer movio la lista de tareas (normalmente a x=0): deshacerlo ya, sin esperar al timer
static void CALLBACK WinEvProc(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObj, LONG, DWORD, DWORD) {
    if (hwnd != g_tl || idObj != OBJID_WINDOW || !g_cfg.center || !g_haveSet) return;
    int x, y; if (!CurrentPos(x, y) || x == g_lastSetX) return;           // x == ultimo: fue nuestro movimiento
    ULONGLONG now = GetTickCount64();
    if (now - g_fixWindow > 1000) { g_fixWindow = now; g_fixCount = 0; }
    if (++g_fixCount <= 30)                                                // freno por si Explorer insiste
        SetWindowPos(g_tl, NULL, g_lastSetX, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    StartBurst(300);
}
static void InitCenter() {
    if (!g_uia) {
        if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER,
                                    __uuidof(IUIAutomation), (void**)&g_uia))) { g_uia = NULL; return; }
        g_uia->CreateTrueCondition(&g_true);
        if (SUCCEEDED(g_uia->CreateCacheRequest(&g_cr)) && g_cr) {
            g_cr->AddProperty(UIA_BoundingRectanglePropertyId);
            g_cr->put_AutomationElementMode(AutomationElementMode_None);   // sin referencias vivas: mas barato
        }
    }
    if (!FindTaskbar()) { SetTimer(g_main, T_REINIT, 1500, NULL); return; }
    if (g_tlEl) { g_tlEl->Release(); g_tlEl = NULL; }
    if (FAILED(g_uia->ElementFromHandle(g_tl, &g_tlEl)) || !g_tlEl) {      // Explorer aun no esta listo: reintentar
        g_tlEl = NULL; SetTimer(g_main, T_REINIT, 1500, NULL); return;
    }
    if (g_winEv) { UnhookWinEvent(g_winEv); g_winEv = NULL; }
    DWORD pid = 0, tid = GetWindowThreadProcessId(g_tl, &pid);
    g_winEv = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, NULL, WinEvProc, pid, tid, WINEVENT_OUTOFCONTEXT);
    g_haveSet = false;
    SetTimer(g_main, T_POLL, 1500, NULL);
    StartBurst(600);
}
static void StopCenter() {
    KillTimer(g_main, T_POLL); KillTimer(g_main, T_PROBE); KillTimer(g_main, T_REINIT); StopAnim();
    if (g_winEv) { UnhookWinEvent(g_winEv); g_winEv = NULL; }
    if (g_tlEl) { g_tlEl->Release(); g_tlEl = NULL; }
    ResetTaskbar();
}
// Ventana que tendria boton en la barra de tareas
static bool IsTaskbarStyle(HWND h) {
    if (!IsWindow(h) || GetWindow(h, GW_OWNER)) return false;
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    return !((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW));
}

// ============================================================ bandeja + ventana principal
static void AddTray() {
    if (!g_trayIcon) {
        g_trayIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(ICON_ID), IMAGE_ICON,
                                       GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
        if (!g_trayIcon) g_trayIcon = LoadIconW(NULL, IDI_APPLICATION);
    }
    NOTIFYICONDATAW n = { sizeof n };
    n.hWnd = g_main; n.uID = 1; n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    n.uCallbackMessage = WM_TRAY; n.hIcon = g_trayIcon;
    wcscpy_s(n.szTip, APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &n);
}
static void DelTray() { NOTIFYICONDATAW n = { sizeof n }; n.hWnd = g_main; n.uID = 1; Shell_NotifyIconW(NIM_DELETE, &n); }

static void TrayMenu() {
    std::vector<MItem> v;
    MHeader(v, std::wstring(APP_NAME) + L"  v" + APP_VERSION);
    MSep(v);
    MAdd(v, 101, L"Center taskbar", g_cfg.center);
    MHeader(v, L"Animation speed");
    const wchar_t* speeds[4] = { L"Off", L"Fast", L"Normal", L"Smooth" };
    for (int i = 0; i < 4; i++) MAdd(v, 201 + i, speeds[i], g_cfg.anim == i);
    MSep(v);
    MAdd(v, 102, L"Run at startup", Autostart());
    MSep(v);
    MAdd(v, 103, L"Exit");
    POINT p; GetCursorPos(&p);
    SetForegroundWindow(g_main);                                  // para que el menu reciba el teclado
    int c = PopupMenu(v, p.x, p.y);
    switch (c) {
    case 101: g_cfg.center = !g_cfg.center; SaveCfg(); if (g_cfg.center) InitCenter(); else StopCenter(); break;
    case 102: SetAutostart(!Autostart()); break;
    case 103: DestroyWindow(g_main); break;
    case 201: case 202: case 203: case 204:
        g_cfg.anim = c - 201; SaveCfg(); StartBurst(300); break;     // se aplica al instante
    }
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == g_msgTaskbarCreated) {                 // explorer.exe se reinicio: volver a crear todo
        AddTray();
        RegisterShellHookWindow(h);
        if (g_cfg.center) { g_tl = NULL; SetTimer(h, T_REINIT, 1500, NULL); }
        return 0;
    }
    if (m == g_msgShell) {                          // eventos del shell, filtrados
        HWND hw = (HWND)l;
        switch (w) {
        case HSHELL_WINDOWDESTROYED:                // se va un boton: rafaga corta si era una ventana "de barra"
            if (IsTaskbarStyle(hw)) { if (IsWindowVisible(hw)) StartBurst(400); else SetTimer(h, T_PROBE, 120, NULL); }
            break;
        case HSHELL_WINDOWCREATED:
            if (IsTaskbarStyle(hw)) SetTimer(h, T_PROBE, 120, NULL);
            break;
        case HSHELL_WINDOWREPLACED: case HSHELL_WINDOWACTIVATED: case 0x8004 /*RUDEAPPACTIVATED*/:
            SetTimer(h, T_PROBE, 120, NULL);        // el timer se reinicia: varios eventos seguidos = un solo sondeo
            break;
        }
        return 0;
    }
    switch (m) {
    case WM_TRAY:
        if (l == WM_RBUTTONUP || l == WM_LBUTTONUP) TrayMenu();
        return 0;
    case WM_DISPLAYCHANGE: StartBurst(500); return 0;
    case WM_SETTINGCHANGE: if (w == SPI_SETWORKAREA) StartBurst(500); return 0;
    case WM_TIMER:
        if (w == T_ANIM) AnimTick();
        else if (w == T_POLL) Probe();
        else if (w == T_PROBE) { KillTimer(h, T_PROBE); Probe(); }
        else if (w == T_REINIT) { KillTimer(h, T_REINIT); if (g_cfg.center) InitCenter(); }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    SetProcessDPIAware();
    HANDLE mtx = CreateMutexW(NULL, TRUE, L"Local\\CenteredTaskbarSingleton");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    g_hInst = hi;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    InitDir(); LoadCfg();
    { HDC sdc = GetDC(NULL); g_dpi = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(NULL, sdc); }

    WNDCLASSW wc = {}; wc.lpfnWndProc = MainProc; wc.hInstance = hi; wc.lpszClassName = L"CenteredTaskbarMain";
    RegisterClassW(&wc);
    WNDCLASSW qc = {}; qc.lpfnWndProc = PopupProc; qc.hInstance = hi; qc.lpszClassName = L"CenteredTaskbarPopup";
    qc.hCursor = LoadCursorW(NULL, IDC_ARROW); qc.style = CS_DROPSHADOW;
    RegisterClassW(&qc);

    g_main = CreateWindowExW(WS_EX_TOOLWINDOW, L"CenteredTaskbarMain", APP_NAME, WS_POPUP, 0, 0, 0, 0, NULL, NULL, hi, NULL);
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g_msgShell = RegisterWindowMessageW(L"SHELLHOOK");
    RegisterShellHookWindow(g_main);
    AddTray();
    if (g_cfg.center) InitCenter();
    TrimMem();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    DeregisterShellHookWindow(g_main);
    StopCenter();
    if (g_cr) g_cr->Release();
    if (g_true) g_true->Release();
    if (g_uia) g_uia->Release();
    DelTray();
    CoUninitialize();
    CloseHandle(mtx);
    return 0;
}

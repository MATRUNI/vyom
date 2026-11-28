#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include "webview2_lite.h"
#include "bridge.h"

typedef HRESULT (STDMETHODCALLTYPE *CreateWebViewEnvironmentWithOptionsInternalFn)(
    BOOLEAN unknown1,
    int runtime_type,
    PCWSTR user_data_dir,
    IUnknown *options,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *handler
);

static HMODULE find_and_load_webview2_runtime(void)
{
    const wchar_t *subkeys[] = {
        L"Software\\Microsoft\\EdgeUpdate\\ClientState\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        L"Software\\WOW6432Node\\Microsoft\\EdgeUpdate\\ClientState\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        L"Software\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        L"Software\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
    };
    HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };

    wchar_t runtime_dir[MAX_PATH];

    for (size_t r = 0; r < 2; r++) {
        for (size_t s = 0; s < sizeof(subkeys)/sizeof(subkeys[0]); s++) {
            HKEY hKey;
            if (RegOpenKeyExW(roots[r], subkeys[s], 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS) {
                DWORD size = sizeof(runtime_dir);
                if (RegQueryValueExW(hKey, L"EBWebView", NULL, NULL, (LPBYTE)runtime_dir, &size) == ERROR_SUCCESS ||
                    RegQueryValueExW(hKey, L"location", NULL, NULL, (LPBYTE)runtime_dir, &size) == ERROR_SUCCESS) {
                    RegCloseKey(hKey);

                    wchar_t dll_path[MAX_PATH];
                    wcsncpy(dll_path, runtime_dir, MAX_PATH - 1);
#if defined(_WIN64) || defined(__x86_64__)
                    wcsncat(dll_path, L"\\EBWebView\\x64\\EmbeddedBrowserWebView.dll", MAX_PATH - wcslen(dll_path) - 1);
#else
                    wcsncat(dll_path, L"\\EBWebView\\x86\\EmbeddedBrowserWebView.dll", MAX_PATH - wcslen(dll_path) - 1);
#endif
                    HMODULE hMod = LoadLibraryExW(dll_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
                    if (hMod) return hMod;
                } else {
                    RegCloseKey(hKey);
                }
            }
        }
    }
    return NULL;
}

static ICoreWebView2Controller *g_controller = NULL;
static ICoreWebView2 *g_webview = NULL;
HWND g_hwnd = NULL;
int g_min_width = 0;
int g_min_height = 0;
int g_max_width = 0;
int g_max_height = 0;
static const app_config_t *g_config = NULL;

static HRESULT STDMETHODCALLTYPE dummy_QueryInterface(void *This, REFIID riid, void **ppv) {
    (void)This; (void)riid;
    *ppv = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE dummy_AddRef(void *This) {
    (void)This; return 1;
}

static ULONG STDMETHODCALLTYPE dummy_Release(void *This) {
    (void)This; return 1;
}

static HRESULT STDMETHODCALLTYPE OnNavigationStarting(ICoreWebView2NavigationStartingEventHandler *This, ICoreWebView2 *sender, ICoreWebView2NavigationStartingEventArgs *args)
{
    (void)This;
    LPWSTR uri_w = NULL;
    args->lpVtbl->get_Uri(args, &uri_w);
    if (!uri_w) return S_OK;

    const wchar_t *prefix = L"https://app.localhost/";
    size_t prefix_len = wcslen(prefix);

    if (wcsncmp(uri_w, prefix, prefix_len) == 0)
    {
        const wchar_t *rel_path = uri_w + prefix_len;

        if (*rel_path != L'\0' && wcscmp(rel_path, L"index.html") != 0 && !wcsstr(rel_path, L"__route="))
        {
            wchar_t clean_rel[MAX_PATH] = {0};
            wcsncpy(clean_rel, rel_path, MAX_PATH - 1);
            wchar_t *q = wcschr(clean_rel, L'?');
            if (q) *q = L'\0';
            wchar_t *h = wcschr(clean_rel, L'#');
            if (h) *h = L'\0';

            const wchar_t *dot = wcsrchr(clean_rel, L'.');
            if (!dot)
            {
                wchar_t disk_path[MAX_PATH];
                GetFullPathNameW(L"dist", MAX_PATH, disk_path, NULL);
                wcscat(disk_path, L"\\");

                wchar_t disk_rel[MAX_PATH] = {0};
                wcsncpy(disk_rel, clean_rel, MAX_PATH - 1);
                for (wchar_t *p = disk_rel; *p; p++) {
                    if (*p == L'/') *p = L'\\';
                }
                wcscat(disk_path, disk_rel);

                DWORD attrs = GetFileAttributesW(disk_path);
                BOOL exists = (attrs != INVALID_FILE_ATTRIBUTES);

                if (!exists)
                {
                    args->lpVtbl->put_Cancel(args, TRUE);
                    wchar_t fallback[2048];
                    swprintf(fallback, 2048, L"https://app.localhost/index.html?__route=/%ls", clean_rel);
                    sender->lpVtbl->Navigate(sender, fallback);
                }
            }
        }
    }

    CoTaskMemFree(uri_w);
    return S_OK;
}

static ICoreWebView2NavigationStartingEventHandlerVtbl NavStartingHandlerVtbl = {
    .QueryInterface = (void*)dummy_QueryInterface,
    .AddRef         = (void*)dummy_AddRef,
    .Release        = (void*)dummy_Release,
    .Invoke         = OnNavigationStarting
};

static ICoreWebView2NavigationStartingEventHandler NavStartingHandler = {
    .lpVtbl = &NavStartingHandlerVtbl
};

static HRESULT STDMETHODCALLTYPE OnControllerCreated(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *This, HRESULT result, ICoreWebView2Controller *controller)
{
    (void)This;
    if (FAILED(result) || !controller) return S_OK;

    g_controller = controller;
    controller->lpVtbl->AddRef(controller);

    controller->lpVtbl->get_CoreWebView2(controller, &g_webview);
    windows_ipc_init(g_webview);
    wchar_t bridge_w[32768];
    MultiByteToWideChar(CP_UTF8, 0, bridge_js, -1, bridge_w, 32768);
    g_webview->lpVtbl->AddScriptToExecuteOnDocumentCreated(g_webview, bridge_w, NULL);

    ICoreWebView2Settings *settings = NULL;
    g_webview->lpVtbl->get_Settings(g_webview, (void**)&settings);
    if (settings && g_config && g_config->webview)
    {
        settings->lpVtbl->put_AreDevToolsEnabled(settings, g_config->webview->developer_extras);
        settings->lpVtbl->put_AreDefaultContextMenusEnabled(settings, g_config->webview->enable_context_menu);
        settings->lpVtbl->put_IsScriptEnabled(settings, g_config->webview->javascript_enabled);
        settings->lpVtbl->Release(settings);
    }

    RECT bounds;
    GetClientRect(g_hwnd, &bounds);
    controller->lpVtbl->put_Bounds(controller, bounds);
    controller->lpVtbl->put_IsVisible(controller, TRUE);

    wchar_t dist_path[MAX_PATH];
    GetFullPathNameW(L"dist", MAX_PATH, dist_path, NULL);

    ICoreWebView2_3 *wv3 = NULL;
    g_webview->lpVtbl->QueryInterface(g_webview, &IID_ICoreWebView2_3, (void**)&wv3);
    if (wv3)
    {
        wv3->lpVtbl->SetVirtualHostNameToFolderMapping(wv3, L"app.localhost", dist_path, COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        wv3->lpVtbl->Release(wv3);
    }

    EventRegistrationToken nav_token;
    g_webview->lpVtbl->add_NavigationStarting(g_webview, &NavStartingHandler, &nav_token);
    g_webview->lpVtbl->Navigate(g_webview, L"https://app.localhost/index.html");

    return S_OK;
}

static ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl ControllerHandlerVtbl = {
    .QueryInterface = (void*)dummy_QueryInterface,
    .AddRef = (void *)dummy_AddRef,
    .Release = (void *)dummy_Release,
    .Invoke = OnControllerCreated
};

static ICoreWebView2CreateCoreWebView2ControllerCompletedHandler ControllerHandler = {
    .lpVtbl = &ControllerHandlerVtbl
};

static HRESULT STDMETHODCALLTYPE OnEnvironmentCreated(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *This, HRESULT result, ICoreWebView2Environment *env)
{
    (void)This;
    if (FAILED(result) || !env) return S_OK;

    env->lpVtbl->CreateCoreWebView2Controller(env, g_hwnd, &ControllerHandler);
    return S_OK;
}

static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl EnvHandlerVtbl = {
    .QueryInterface = (void*)dummy_QueryInterface,
    .AddRef         = (void*)dummy_AddRef,
    .Release        = (void*)dummy_Release,
    .Invoke         = OnEnvironmentCreated
};

static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler EnvHandler = {
    .lpVtbl = &EnvHandlerVtbl
};

static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (win32_tray_handle_message(hwnd, uMsg, wParam, lParam))
        return 0;
    if (win32_process_handle_message(uMsg, wParam, lParam))
        return 0;
    switch (uMsg)
    {
        case WM_DESTROY:
            win32_tray_cleanup();
            if (g_controller != NULL)
                g_controller->lpVtbl->Close(g_controller);
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            if (g_controller != NULL)
            {
                RECT bounds;
                GetClientRect(hwnd, &bounds);
                g_controller->lpVtbl->put_Bounds(g_controller, bounds);
            }
            return 0;
        case WM_GETMINMAXINFO:
        {
            MINMAXINFO *mmi = (MINMAXINFO *)lParam;
            int min_w = g_min_width  ? g_min_width  : (g_config && g_config->window ? g_config->window->min_width : 0);
            int min_h = g_min_height ? g_min_height : (g_config && g_config->window ? g_config->window->min_height : 0);
            int max_w = g_max_width  ? g_max_width  : (g_config && g_config->window ? g_config->window->max_width : 0);
            int max_h = g_max_height ? g_max_height : (g_config && g_config->window ? g_config->window->max_height : 0);

            if (min_w) mmi->ptMinTrackSize.x = min_w;
            if (min_h) mmi->ptMinTrackSize.y = min_h;
            if (max_w) mmi->ptMaxTrackSize.x = max_w;
            if (max_h) mmi->ptMaxTrackSize.y = max_h;
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int platform_app_run(const app_config_t *config, int argc, char *argv[])
{
    HINSTANCE hInstance = GetModuleHandle(NULL);
    (void)argc;
    (void)argv;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc = {0};
    g_config = config;

    wc.hInstance = hInstance;
    wc.cbSize = sizeof(WNDCLASSEXW);
    wchar_t title_w[256] = L"DesktopApp";
    if (config->window->title)
        MultiByteToWideChar(CP_UTF8, 0, config->window->title, -1, title_w, 256);

    wc.lpszClassName = L"VyomWindow";
    wc.lpfnWndProc = WndProc;
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!config->window->decorations)
        style = WS_POPUP;
    if (!config->window->resizable)
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

    HWND hwnd = CreateWindowExW(
        0,
        L"VyomWindow",
        title_w,
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        config->window->width,
        config->window->height,
        NULL,
        NULL,
        hInstance,
        NULL
    );
    g_hwnd = hwnd;

    HMODULE hRuntime = find_and_load_webview2_runtime();
    if (hRuntime)
    {
        CreateWebViewEnvironmentWithOptionsInternalFn create_env =
            (CreateWebViewEnvironmentWithOptionsInternalFn)(void *)GetProcAddress(hRuntime, "CreateWebViewEnvironmentWithOptionsInternal");
        if (create_env)
            create_env(TRUE, 0, NULL, NULL, &EnvHandler);
    }

    if (config->window->maximized)
        ShowWindow(hwnd, SW_MAXIMIZE);
    else
        ShowWindow(hwnd, SW_SHOW);

    UpdateWindow(hwnd);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

void platform_app_emit(const char *event, const char *json_data)
{
    if (!g_webview || !event) return;

    const char *payload = (json_data && *json_data) ? json_data : "null";

    char script[4096];
    snprintf(script, sizeof(script), "window.vyom._emit(\"%s\", %s);", event, payload);

    wchar_t script_w[4096];
    MultiByteToWideChar(CP_UTF8, 0, script, -1, script_w, 4096);
    g_webview->lpVtbl->ExecuteScript(g_webview, script_w, NULL);
}

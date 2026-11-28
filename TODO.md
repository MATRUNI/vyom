# Vyom (व्योम) — Master Engineering Roadmap & TODO

> **Vyom (व्योम)** is an ultra-featherlight (< 30 KB), zero-dependency, cross-platform native desktop application runtime written in pure **C11**, designed as an ultra-minimalist alternative to Electron and Tauri.

This document tracks all completed milestones and outlines the comprehensive roadmap of every native function, subsystem, and architecture milestone needed for a complete, production-grade framework.

---

## 🌟 Milestone Progress Matrix

```text
[ Core IPC & O(1) Wyhash Switchboard  ]   100% [====================] (Complete)
[ Window Geometry & Controls         ]   100% [====================] (Size, Pos, Center, Topmost, Resizable, Title, Min/Max)
[ 64-Bit Chunked Filesystem Engine   ]   100% [====================] (Info, Chunk, Write, ReadDir, Touch, Rm, Rename, Move)
[ Native Dialogs                     ]   100% [====================] (OpenFile, SaveFile, OpenDirectory, Message, Confirm)
[ Native Clipboard Engine            ]   100% [====================] (writeText, readText, clear, hasText)
[ macOS (Cocoa + WKWebView)          ]   100% [====================] (Complete & CI Verified)
[ Native Notifications (Tray/Banners)]   100% [====================] (Complete & CI Verified)
[ Process & Shell Execution          ]   100% [====================] (Complete & CI Verified)
[ System Tray & Context Menus        ]   100% [====================] (Complete & CI Verified)
```

---

## Phase 1: Core Foundation & Shipped Capabilities (Complete ✅)

- [x] **Zero-Dependency C11 Runtime Core**: Standalone binary compiles to `< 30 KB` without bundled Node.js or Chromium.
- [x] **O(1) Compile-Time Wyhash32 Hash Router**: 32-bit register command routing in 1 CPU cycle with zero hot-path `strcmp` and robust avalanche properties.
- [x] **Zero-Allocation JSON Parser**: Lightweight `json_get_string`, `json_get_uint64`, `json_get_bool` (`json_util.c`).
- [x] **Linux Native Backend**: GTK4 + WebKitGTK 6.0 with `jsc_value_new_string` script replies and `app://localhost/` custom URI scheme.
- [x] **Windows Native Backend**: Win32 + Microsoft WebView2 COM integration with custom virtual host mapping (`https://app.localhost`).
- [x] **Frameless Draggable Windowing**: Native OS compositor dragging (`platform_window_start_drag` using Win32 `WM_SYSCOMMAND` + `SC_MOVE` and GTK4 `gtk_window_interactive_drag_begin`).
- [x] **Native File Dialogs**:
  - `vyom:dialog:open_file` (Win32 `GetOpenFileNameW` / GTK4 `GtkFileDialog`).
  - `vyom:dialog:save_file` (Win32 `GetSaveFileNameW` / GTK4 `GtkFileDialog`).
- [x] **Native 64-Bit Chunked Filesystem Engine**:
  - `vyom:fs:file_info`: Metadata extraction (size, existence, directory check).
  - `vyom:fs:read_chunk`: Hybrid Header + Raw Data 64-bit seek streaming (`offset`, `length`, `total`, `eof`).
  - `vyom:fs:write_text`: Text file atomic creation and appending.
  - `vyom:fs:read_dir`: 64 KB zero-allocation paginated directory enumerator (`opendir` / `dirent.h`).
  - `vyom:fs:create_file`: Native touch / empty file creator.
  - `vyom:fs:create_dir`: Cross-platform directory creation (`_mkdir` on Windows, `mkdir 0755` on Linux).
  - `vyom:fs:remove`: File and directory deletion (`remove`).
  - `vyom:fs:rename` / `vyom:fs:move`: Cross-platform atomic file and directory rename & move (Win32 `MoveFileExA` / POSIX `rename`).
- [x] **Production React 19 Frontend**:
  - Modular SPA architecture bundled via Vite (`Titlebar`, `TelemetryHero`, `FsExplorer`, `DocsViewer`).
  - Dual routing support: SPA HTML5 history rewrite fallback (`__route`) + hash router.
  - CommonMark compliant Markdown renderer with CRLF (`\r\n`) normalization.

---

## Phase 2: Essential Desktop Capabilities (High Priority 🚀)

### 2.1 Native Clipboard Engine (`window.vyom.clipboard`)
- [x] **`clipboard.writeText(text)`**: Copy UTF-8 string to clipboard without browser permission prompts.
  - **Win32**: `OpenClipboard`, `EmptyClipboard`, `SetClipboardData(CF_UNICODETEXT, ...)`.
  - **Linux**: `gdk_clipboard_set_text` via GDK4.
- [x] **`clipboard.readText()`**: Read plain text from system clipboard.
  - **Win32**: `GetClipboardData(CF_UNICODETEXT)` with `WideCharToMultiByte`.
  - **Linux**: `gdk_clipboard_read_text_async`.
- [x] **`clipboard.clear()`**: Wipe system clipboard contents.
- [x] **`clipboard.hasText()`**: Fast boolean check without retrieving full payload.

### 2.2 Native Message & Confirmation Dialogs (`window.vyom.dialog`)
- [x] **`dialog.openDirectory({ title })`**: Pick a directory folder.
  - **Win32**: `IFileDialog` with `FOS_PICKFOLDERS` or `SHBrowseForFolder`.
  - **Linux**: `gtk_file_dialog_select_folder`.
  - **macOS**: `NSOpenPanel` with `canChooseDirectories = YES`.
- [x] **`dialog.message({ title, message, type })`**: Native alert popups (`info`, `warning`, `error`).
  - **Win32**: `MessageBoxW`.
  - **Linux**: `GtkAlertDialog`.
  - **macOS**: `NSAlert`.
- [x] **`dialog.confirm({ title, message, buttons })`**: Native prompt returning selected button (OK/Cancel, Yes/No).
  - **Win32**: `MessageBoxW` with `MB_OKCANCEL`.
  - **Linux**: `GtkAlertDialog` choose.
  - **macOS**: `NSAlert` with OK/Cancel.

### 2.3 Window Customization & Geometry (`window.vyom.window`)
- [x] **`window.setSize(width, height)` / `window.getSize()`**: Dynamic runtime resize across Win32, GTK4 & Cocoa.
- [x] **`window.setPosition(x, y)` / `window.getPosition()`**: Window positioning.
- [x] **`window.center()`**: Center window on active monitor.
- [x] **`window.setAlwaysOnTop(bool)`**: Pin window on top of all other windows (Win32 HWND_TOPMOST).
- [x] **`window.setResizable(bool)`**: Toggle dynamic window resizing borders and controls.
- [x] **`window.setMinSize(w, h)` / `window.setMaxSize(w, h)`**: Dynamic min/max bounds tracking.
- [x] **`window.setTitle(title)`**: Dynamic runtime titlebar updates.
- [x] **`window.isMaximized()` / `window.isMinimized()`**: Query live window state.
- [ ] **`window.setFullscreen(bool)` / `window.isFullscreen()`**: Fullscreen / kiosk mode.
- [ ] **`window.setFocus()` / `window.requestUserAttention()`**: Flash taskbar/dock icon on background event.
- [ ] **`window.getMonitors()`**: Retrieve monitor bounds, resolution, refresh rate, and DPI scaling.

---

## Phase 3: System & Shell Integration (Medium Priority ⚡)

### 3.1 Subprocess & Shell Execution (`window.vyom.process`)
- [x] **`process.exec(command, args)`**: Execute command asynchronously with timeout, returning `{ stdout, stderr, exitCode, timedOut }`.
  - **Win32**: `CreateProcessW` with anonymous pipes, Job Object, and timeout.
  - **Linux**: `GSubprocess` async pump with `PR_SET_PDEATHSIG` + process groups.
  - **macOS**: `NSTask` + `NSPipe` + `dispatch_semaphore`.
- [x] **`process.spawn(command, args)`**: Long-running background process streaming `stdout`, `stderr`, and `exit` events via `vyom.on()`.
- [x] **`process.kill(pid)`**: O(1) `hash_u32` owned-PID verification + process group termination.

### 3.2 Standard Paths & Environment (`window.vyom.path` / `os`)
- [x] **`path.getSpecial(name)`**: Resolve OS-standard directories across Windows, Linux, and macOS:
  - `home` (`%USERPROFILE%` / `$HOME`)
  - `appData` (`%APPDATA%` / `~/.config`)
  - `desktop`
  - `documents`
  - `downloads`
  - `temp` (`GetTempPathW` / `/tmp`)
- [ ] **`os.getMemory()`**: Total system RAM vs resident set size (RSS) of Vyom process.
- [ ] **`os.uptime()`**: System uptime in seconds.

### 3.3 Enhanced Filesystem Operations (`window.vyom.fs`)
- [ ] **`fs.copy(src, dst)`**: Fast OS-level file copy (`CopyFileW` / `sendfile`).
- [ ] **`fs.rename(src, dst)`**: Atomic rename / move across filesystem boundaries.
- [ ] **`fs.watch(path, callback)`**: Live directory / file change watcher (`ReadDirectoryChangesW` / `inotify`).

---

## Phase 4: Native Shell Presence (System Tray & Menus 🔔)

### 4.1 System Tray & Notifications Area (`window.vyom.tray`)
- [x] **`tray.create({ icon, tooltip })`**: Spawn system tray icon.
  - **Win32**: `Shell_NotifyIconW` with `NIM_ADD`.
  - **Linux**: `StatusNotifierItem` over `GDBusConnection`.
  - **macOS**: `NSStatusBar` + `NSStatusItem`.
- [x] **`tray.setMenu(menuItems)`**: Right-click tray context menu (*Open*, *Hide*, *Separator*, *Quit*).
- [x] **`tray.destroy()`**: Clean removal on shutdown.

### 4.2 Native Desktop Notifications (`window.vyom.notification`)
- [x] **`notification.send({ title, body, icon })`**: Native banner / toast alert.
  - **Win32**: `Shell_NotifyIconW` with `NIF_INFO`.
  - **Linux**: `org.freedesktop.Notifications.Notify` over `GDBus`.
  - **macOS**: `osascript` native notification.

### 4.3 Native Window Menu Bar (`window.vyom.menu`)
- [ ] **`menu.setWindowMenu(menuTree)`**: Top application menu bar (*File*, *Edit*, *View*, *Window*, *Help*).

---

## Phase 5: App Lifecycle & Enterprise Hardening 🛡️

### 5.1 Single-Instance Mutex Lock
- [ ] **`app.singleInstanceLock(appId)`**: Prevent opening duplicate instances.
  - **Win32**: Named Mutex (`CreateMutexW`).
  - **Linux**: File lock (`flock`) on a lockfile in runtime directory.
  - **Focus Forwarding**: Focus the existing window when a second instance tries to launch.

### 5.2 Deep Linking & Custom URL Schemes
- [ ] **`protocol.register(scheme)`**: Register custom schemes (e.g., `vyom://open?file=xyz` or `myapp://auth`).
  - **Win32**: Registry key under `HKEY_CURRENT_USER\Software\Classes\<scheme>`.
  - **Linux**: `.desktop` file MIME association.

### 5.3 Global Keyboard Shortcuts
- [ ] **`shortcuts.register(accelerator, callback)`**: System-wide global hotkeys (e.g. `Ctrl+Alt+Space`) active even when window is minimized or unfocused.
  - **Win32**: `RegisterHotKey`.
  - **Linux**: X11 `XGrabKey` or Wayland Global Shortcuts Portal.

### 5.4 Window State Persistence
- [ ] Auto-remember and restore window width, height, position, and maximized state across app reboots.

### 5.5 Windows Executable Branding (`.rc` + `windres`)
- [ ] Embed custom `.ico` app icon into `build/app.exe`.
- [ ] Embed Windows Version Manifest (`ProductName`, `CompanyName`, `LegalCopyright`, `FileVersion`).

---

## Phase 6: Cross-Platform Expansion (macOS 🍏 & Android 🤖)

- [x] Implement `src/platform/macos/app.m` using Cocoa (`NSApplication`, `NSWindow`) + `WKWebView`.
- [x] Implement `src/platform/macos/ipc.m` via `WKScriptMessageHandlerWithReply`.
- [x] Wire macOS target flags and framework links in `Makefile` (`-framework Cocoa -framework WebKit`).
- [x] Apple Silicon (ARM64) CI verification on GitHub Actions (`macos-latest`).
- [ ] Implement `src/platform/android/` NDK JNI bridge (`libvyom.so`) & AssetLoader.

---

## ⚡ Performance & Optimization Backlog

- [ ] **`json_util.c` (`json_extract` 1-Pass Scanner)**: Single-pass `json_field_t[]` descriptor extractor (eliminates repeated `strstr` scans, handles spaces before `:`, ignores keys inside string values), plus `memcpy` fast-path.
- [ ] **Raw Binary IPC (`ArrayBuffer` / `Uint8Array`)**: Bypass UTF-8 string conversion (`jsc_value_new_typed_array` / binary response) to push file/data transfers toward 2,000+ MB/s.
- [ ] **Shared `JSON_ESC_LUT` Helper**: Reuse the 256-byte LUT + span `memcpy` across `clipboard.c` and `linux/process.c` (`emit_pipe_chunk`).

---

## 📐 Architecture Rules & Coding Standards

1. **Zero Unnecessary Mallocs on Hot Paths**: Hot switchboards and IPC replies must use stack buffers or `.bss` static buffers wherever sizes are bounded.
2. **Compile-Time Wyhash32 Hashing**: Every new command must define an integer Wyhash32 hash constant in `registry.c` for 1-cycle CPU dispatch.
3. **No External Shared Libraries**: Keep Windows builds linking strictly against core system DLLs (`user32`, `gdi32`, `ole32`, `comdlg32`, `shlwapi`).
4. **UTF-8 Everywhere Internal**: Convert to/from UTF-16 (`wchar_t`) strictly at Win32 API boundaries; keep all C engine logic pure UTF-8.
5. **No Node.js / Python Dependency in Production**: Pre-compile all web bundles into `dist/` so end-users and C build tools only require `make` and `gcc`.

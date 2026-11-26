// SPA HTML5 History Route Restoration (restores /route if rewritten by native fallback)
(function () {
  try {
    const url = new URL(window.location.href);
    const route = url.searchParams.get("__route");
    if (route) {
      window.history.replaceState(null, "", route);
    }
  } catch (e) {}
})();

window.vyom = {
  _reqId: 0,
  _listeners: new Map(),

  window:{
    minimize: () => window.vyom.invoke("vyom:window:minimize"),
    toggleMaximize: () => window.vyom.invoke("vyom:window:toggle_maximize"),
    close: () => window.vyom.invoke("vyom:window:close"),
    startDrag: () => window.vyom.invoke("vyom:window:start_drag"),
    setSize: async (width, height) => {
      const res = await window.vyom.invoke("vyom:window:set_size", { width: Number(width), height: Number(height) });
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    getSize: async () => {
      const res = await window.vyom.invoke("vyom:window:get_size");
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    setPosition: async (x, y) => {
      const res = await window.vyom.invoke("vyom:window:set_position", { x: Number(x), y: Number(y) });
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    getPosition: async () => {
      const res = await window.vyom.invoke("vyom:window:get_position");
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    center: async () => {
      const res = await window.vyom.invoke("vyom:window:center");
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    setAlwaysOnTop: async (alwaysOnTop = true) => {
      const res = await window.vyom.invoke("vyom:window:set_always_on_top", { alwaysOnTop: Boolean(alwaysOnTop) });
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    setResizable: async (resizable = true) => {
      const res = await window.vyom.invoke("vyom:window:set_resizable", { resizable: Boolean(resizable) });
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    setMinSize: async (width, height) => {
      const res = await window.vyom.invoke("vyom:window:set_min_size", { width: Number(width), height: Number(height) });
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    setMaxSize: async (width, height) => {
      const res = await window.vyom.invoke("vyom:window:set_max_size", { width: Number(width), height: Number(height) });
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    setTitle: async (title) => {
      document.title = String(title);
      window.dispatchEvent(new CustomEvent("vyom:titlechange", { detail: String(title) }));
      const res = await window.vyom.invoke("vyom:window:set_title", { title: String(title) });
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    isMaximized: async () => {
      const res = await window.vyom.invoke("vyom:window:is_maximized");
      const parsed = typeof res === "string" ? JSON.parse(res) : res;
      return Boolean(parsed?.maximized);
    },
    isMinimized: async () => {
      const res = await window.vyom.invoke("vyom:window:is_minimized");
      const parsed = typeof res === "string" ? JSON.parse(res) : res;
      return Boolean(parsed?.minimized);
    },
  },
  dialog:{
    openFile: async (options = {}) => {
      const res = await window.vyom.invoke("vyom:dialog:open_file", options);
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    saveFile: async (options = {}) => {
      const res = await window.vyom.invoke("vyom:dialog:save_file", options);
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    openDirectory: async (options = {}) => {
      const res = await window.vyom.invoke("vyom:dialog:open_directory", options);
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    message: async (options = {}) => {
      const res = await window.vyom.invoke("vyom:dialog:message", options);
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    confirm: async (options = {}) => {
      const res = await window.vyom.invoke("vyom:dialog:confirm", options);
      const parsed = typeof res === "string" ? JSON.parse(res) : res;
      return Boolean(parsed?.confirmed);
    },
  },

  clipboard: {
    writeText: async (text) => {
      const res = await window.vyom.invoke("vyom:clipboard:write_text", { text: String(text) });
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    readText: async () => {
      const res = await window.vyom.invoke("vyom:clipboard:read_text");
      const parsed = typeof res === "string" ? JSON.parse(res) : res;
      return parsed?.text ?? "";
    },
    clear: async () => {
      const res = await window.vyom.invoke("vyom:clipboard:clear");
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    hasText: async () => {
      const res = await window.vyom.invoke("vyom:clipboard:has_text");
      const parsed = typeof res === "string" ? JSON.parse(res) : res;
      return Boolean(parsed?.hasText);
    },
  },

  path: {
    getSpecial: async (name) => {
      const res = await window.vyom.invoke("vyom:path:get_special", { name: String(name) });
      const parsed = typeof res === "string" ? JSON.parse(res) : res;
      return parsed?.path ?? "";
    },
    getAll: async () => {
      const res = await window.vyom.invoke("vyom:path:get_all");
      return typeof res === "string" ? JSON.parse(res) : res;
    },
  },

  notification: {
    send: async (options = {}) => {
      const res = await window.vyom.invoke("vyom:notification:send", {
        title: options.title ? String(options.title) : "Vyom",
        body: options.body ? String(options.body) : "",
        icon: options.icon ? String(options.icon) : "",
      });
      return typeof res === "string" ? JSON.parse(res) : res;
    },
  },

  tray: {
    create: async (options = {}) => {
      const res = await window.vyom.invoke("vyom:tray:create", {
        tooltip: options.tooltip ? String(options.tooltip) : "Vyom",
        icon: options.icon ? String(options.icon) : "",
      });
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    setTooltip: async (tooltip) => {
      const res = await window.vyom.invoke("vyom:tray:set_tooltip", { tooltip: String(tooltip) });
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    setMenu: async (items = []) => {
      const res = await window.vyom.invoke("vyom:tray:set_menu", { items });
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    destroy: async () => {
      const res = await window.vyom.invoke("vyom:tray:destroy");
      return typeof res === "string" ? JSON.parse(res) : res;
    },
  },

  process: {
    exec: async (cmd, args = [], options = {}) => {
      const res = await window.vyom.invoke("vyom:process:exec", {
        cmd: String(cmd),
        args: Array.isArray(args) ? args.map(String) : [],
        timeout: options.timeout ? Number(options.timeout) : 5000,
      });
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    spawn: async (cmd, args = []) => {
      const res = await window.vyom.invoke("vyom:process:spawn", {
        cmd: String(cmd),
        args: Array.isArray(args) ? args.map(String) : [],
      });
      return typeof res === "string" ? JSON.parse(res) : res;
    },
    kill: async (pid) => {
      const res = await window.vyom.invoke("vyom:process:kill", { pid: Number(pid) });
      return typeof res === "string" ? JSON.parse(res) : res;
    },
  },
  
  // JavaScript -> C (Commands)
  invoke: function (cmd, args = {}) 
  {
    if(window.webkit?.messageHandlers?.desktop)
    {
      return window.webkit.messageHandlers.desktop.postMessage({ cmd, args });
    }
    if(window?.chrome?.webview)
    {
      return new Promise((resolve, reject) => {
        const id = ++this._reqId;
        const replyEvent = `__reply_${id}`;
        
        const handler = (res) => {
          this.off(replyEvent, handler);
          if (res.error) reject(new Error(res.error));
          else resolve(res.result);
        };
        this.on(replyEvent, handler);
        window.chrome.webview.postMessage({ id, cmd, args });
      });
    }

    return Promise.reject(new Error("No native platform detected."));
  },

  // C -> JavaScript (Events)
  on: function (event, callback) 
  {
    if (!this._listeners.has(event)) 
    {
      this._listeners.set(event, new Set());
    }
    this._listeners.get(event).add(callback);
    return ()=>{
      this.off(event,callback);
    }
  },

  off: function (event, callback) 
  {
    this._listeners.get(event)?.delete(callback);
  },

  // Internal trigger called by C
  _emit: function (event, data) 
  {
    const callbacks = this._listeners.get(event);
    if(!callbacks) return;

    for(const callback of callbacks)
    {
      try 
      {
        callback(data);
      } 
      catch (err) 
      {
        console.error("[bridge] Error in event listener:", err);
      }
    }
  },

  // File system commands
  fs: {
    // 1. Get file metadata
    fileInfo: async (path) => {
      const res = await window.vyom.invoke("vyom:fs:file_info", { path });
      return typeof res === "string" ? JSON.parse(res) : res;
    },

    // 2. Read chunk: splits header on line 1, hands you raw data! (4MB optimal L3 chunk size)
    readChunk: async (path, offset = 0, length = 4 * 1024 * 1024) => {
      const raw = await window.vyom.invoke("vyom:fs:read_chunk", { path, offset, length });
      const idx = raw.indexOf("\n");
      if (idx === -1) {
        return { offset, length: 0, total: 0, eof: true, data: "" };
      }

      const header = raw.slice(0, idx);
      const data = raw.slice(idx + 1);

      // Parse "offset=0,len=65536,total=1048576,eof=0"
      const meta = {};
      for (const pair of header.split(",")) {
        const [k, v] = pair.split("=");
        meta[k] = Number(v);
      }

      return {
        offset: meta.offset,
        length: meta.len,
        total: meta.total,
        eof: meta.eof === 1,
        data: data, // 100% pure raw unescaped text!
      };
    },

    // 3. Write text
    writeTextFile: async (path, content, append = false) => {
      const res = await window.vyom.invoke("vyom:fs:write_text", { path, content, append });
      return typeof res === "string" ? JSON.parse(res) : res;
    },

    // 4. List directory contents (with optional pagination)
    readDir: async (path, options = {}) => {
      const offset = options.offset || 0;
      const limit = options.limit || 500;
      const res = await window.vyom.invoke("vyom:fs:read_dir", { path, offset, limit });
      return typeof res === "string" ? JSON.parse(res) : res;
    },

    // 5. Create empty file (with any extension)
    createFile: async (path) => {
      const res = await window.vyom.invoke("vyom:fs:create_file", { path });
      return typeof res === "string" ? JSON.parse(res) : res;
    },

    // 6. Create directory
    createDir: async (path) => {
      const res = await window.vyom.invoke("vyom:fs:create_dir", { path });
      return typeof res === "string" ? JSON.parse(res) : res;
    },

    // 7. Remove file or empty directory
    remove: async (path) => {
      const res = await window.vyom.invoke("vyom:fs:remove", { path });
      return typeof res === "string" ? JSON.parse(res) : res;
    },

    // 8. Rename / Move file or directory
    rename: async (from, to) => {
      const res = await window.vyom.invoke("vyom:fs:rename", { from, to });
      return typeof res === "string" ? JSON.parse(res) : res;
    },

    // 9. Move alias (identical to rename)
    move: async (from, to) => {
      const res = await window.vyom.invoke("vyom:fs:move", { from, to });
      return typeof res === "string" ? JSON.parse(res) : res;
    },

    // 10. Get all mounted storage drives / disks
    getDrives: async () => {
      const res = await window.vyom.invoke("vyom:fs:get_drives");
      return typeof res === "string" ? JSON.parse(res) : res;
    },
  },
};
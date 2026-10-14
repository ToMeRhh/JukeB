/* JukeB panel: vanilla web component, no build step, no external imports. */
const UPLOAD_URL = "/api/jukeb/upload";
const POLL_MS = 2000;
const TABS = ["tags", "media", "settings"];
const TAB_KEY = "jukeb-panel-tab";

const esc = (s) =>
  String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

const ago = (iso) => {
  if (!iso) return "never";
  const s = Math.max(0, Math.round((Date.now() - new Date(iso).getTime()) / 1000));
  if (s < 60) return `${s} s ago`;
  if (s < 3600) return `${Math.floor(s / 60)} min ago`;
  return `since ${new Date(iso).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })}`;
};

const fmtSize = (n) => (n >= 1048576 ? `${(n / 1048576).toFixed(1)} MB` : `${Math.round(n / 1024)} KB`);

const STYLE = `
  :host { display: block; padding: 16px; max-width: 900px; margin: 0 auto; color: var(--primary-text-color); box-sizing: border-box; }
  h1 { font-size: 24px; font-weight: 400; margin: 0 0 16px; }
  .card { background: var(--card-background-color, #fff); border-radius: var(--ha-card-border-radius, 12px); box-shadow: var(--ha-card-box-shadow, 0 1px 3px rgba(0,0,0,.2)); padding: 16px; margin-bottom: 16px; }
  .card h2 { font-size: 18px; font-weight: 500; margin: 0 0 12px; }
  .muted { color: var(--secondary-text-color); }
  .ok { color: var(--success-color, #43a047); font-weight: 500; }
  .bad { color: var(--error-color, #db4437); font-weight: 500; }
  .row { display: flex; flex-wrap: wrap; gap: 8px 16px; align-items: center; }
  .uid { font-family: monospace; font-size: 15px; padding: 2px 6px; border-radius: 4px; background: var(--secondary-background-color, #f3f3f3); }
  .uid.fresh { outline: 2px solid var(--primary-color); }
  .badge { font-size: 12px; padding: 2px 8px; border-radius: 10px; background: var(--secondary-background-color, #eee); }
  .badge.new { background: var(--primary-color); color: var(--text-primary-color, #fff); }
  table { width: 100%; border-collapse: collapse; }
  th, td { text-align: left; padding: 8px 6px; border-bottom: 1px solid var(--divider-color, #e0e0e0); vertical-align: middle; }
  th { font-weight: 500; color: var(--secondary-text-color); font-size: 13px; }
  td.actions { white-space: nowrap; text-align: right; }
  button { font: inherit; padding: 6px 12px; border-radius: 6px; border: 1px solid var(--divider-color, #ccc); background: var(--card-background-color, #fff); color: var(--primary-text-color); cursor: pointer; }
  button.primary { background: var(--primary-color); color: var(--text-primary-color, #fff); border-color: var(--primary-color); }
  button.danger { color: var(--error-color, #db4437); }
  button:disabled { opacity: .5; cursor: default; }
  label { display: block; margin: 10px 0 4px; font-size: 13px; color: var(--secondary-text-color); }
  input, select { font: inherit; width: 100%; box-sizing: border-box; padding: 8px; border-radius: 6px; border: 1px solid var(--divider-color, #ccc); background: var(--card-background-color, #fff); color: var(--primary-text-color); }
  input[type=range] { padding: 0; }
  .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 0 16px; }
  @media (max-width: 600px) { .grid { grid-template-columns: 1fr; } td.actions button { padding: 6px 8px; } }
  .msg { margin-top: 8px; font-size: 14px; min-height: 18px; }
  .drop { border: 2px dashed var(--divider-color, #ccc); border-radius: 8px; padding: 20px; text-align: center; margin-bottom: 12px; }
  .drop.over { border-color: var(--primary-color); }
  progress { width: 100%; height: 8px; }
  .radios label { display: inline-flex; align-items: center; gap: 4px; margin: 4px 16px 4px 0; color: var(--primary-text-color); font-size: 14px; }
  .radios input { width: auto; }
  .tabs { display: flex; gap: 4px; border-bottom: 1px solid var(--divider-color, #e0e0e0); margin-bottom: 16px; }
  .tabs button { border: none; border-bottom: 3px solid transparent; border-radius: 0; background: none; padding: 10px 16px; font-size: 15px; color: var(--secondary-text-color); margin-bottom: -1px; }
  .tabs button[aria-selected=true] { color: var(--primary-color); border-bottom-color: var(--primary-color); font-weight: 500; }
  section[hidden] { display: none; }
  .check { display: inline-flex; align-items: center; gap: 6px; color: var(--primary-text-color); font-size: 14px; margin: 10px 0 4px; }
  .check input { width: auto; }
  .status { display: grid; grid-template-columns: auto repeat(5, minmax(0, 1fr)); gap: 8px 16px; align-items: center; }
  .status .cell { min-width: 0; }
  .status .k { display: block; font-size: 12px; color: var(--secondary-text-color); white-space: nowrap; }
  .status .v { display: block; font-size: 15px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .status .sub { display: block; font-size: 12px; color: var(--secondary-text-color); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; min-height: 14px; }
  .status .err { grid-column: 1 / -1; font-size: 13px; min-height: 18px; }
  .status .foot { grid-column: 1 / -1; font-size: 12px; color: var(--secondary-text-color); }
  .toggle { position: relative; display: inline-block; width: 44px; height: 24px; }
  .toggle input { opacity: 0; width: 0; height: 0; margin: 0; }
  .toggle .knob { position: absolute; inset: 0; border-radius: 12px; background: var(--disabled-color, #bdbdbd); transition: .15s; cursor: pointer; }
  .toggle .knob::before { content: ""; position: absolute; width: 18px; height: 18px; left: 3px; top: 3px; border-radius: 50%; background: #fff; transition: .15s; }
  .toggle input:checked + .knob { background: var(--primary-color); }
  .toggle input:checked + .knob::before { transform: translateX(20px); }
  .toggle input:disabled + .knob { opacity: .5; cursor: default; }
  @media (max-width: 700px) { .status { grid-template-columns: auto 1fr 1fr; } }
  audio { width: 100%; margin: 4px 0 12px; }
  .hours { display: flex; gap: 8px; align-items: center; }
  .hours input { width: auto; }
`;

class JukeBPanel extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    this._hass = null;
    this._timer = null;
    this._status = null;
    this._tags = {};
    this._files = [];
    this._settings = null;
    this._editing = null;
    this._lastScanUid = null;
    this._lastScanAt = null;
    this._playerCount = -1;
    this._previewPath = null;
    this._previewPlaying = false;
    this._rendered = false;
  }

  set hass(hass) {
    const first = !this._hass;
    this._hass = hass;
    if (first && this.isConnected) this._start();
    else if (this._rendered) this._renderPlayers();
  }

  get hass() {
    return this._hass;
  }

  connectedCallback() {
    if (this._hass) this._start();
  }

  disconnectedCallback() {
    clearInterval(this._timer);
    this._timer = null;
  }

  async _start() {
    if (!this._rendered) this._renderShell();
    await this._refreshAll();
    if (!this.isConnected) return; // navigated away during the first load: no timer to leak
    clearInterval(this._timer);
    this._timer = setInterval(() => this._refreshStatus(), POLL_MS);
  }

  $(id) {
    return this.shadowRoot.getElementById(id);
  }

  _say(id, text, ok) {
    const el = this.$(id);
    el.textContent = text;
    el.className = `msg ${ok ? "ok" : "bad"}`;
  }

  _ws(msg) {
    return this._hass.callWS(msg);
  }

  // ---------- data ----------

  async _refreshAll() {
    // Each section reports its own load failure; one failing call must not hide the others.
    await Promise.all([
      this._refreshStatus(),
      this._guard("tagMsg", () => this._refreshTags()),
      this._guard("fileMsg", () => this._refreshFiles()),
      this._guard("settingsMsg", () => this._refreshSettings()),
    ]);
  }

  async _guard(msgId, fn) {
    try {
      await fn();
    } catch (e) {
      this._say(msgId, `Failed to load: ${e.message || e}`, false);
    }
  }

  async _refreshStatus() {
    try {
      this._status = await this._ws({ type: "jukeb/status" });
      this._renderStatus();
      this._renderScan();
    } catch (e) {
      this.$("stError").textContent = `Cannot reach the JukeB integration: ${e.message || e}`;
    }
  }

  async _refreshTags() {
    this._tags = (await this._ws({ type: "jukeb/tags/list" })).tags;
    this._renderTags();
  }

  async _refreshFiles() {
    this._files = (await this._ws({ type: "jukeb/media/list" })).files;
    this._renderFiles();
    this._renderFileOptions();
  }

  async _refreshSettings() {
    this._settings = (await this._ws({ type: "jukeb/settings/get" })).settings;
    this._renderSettings();
  }

  // ---------- shell ----------

  _renderShell() {
    this._rendered = true;
    this.shadowRoot.innerHTML = `
      <style>${STYLE}</style>
      <h1>JukeB</h1>
      <div class="card">
        <div class="status">
          <div class="cell"><span class="k">Enabled</span>
            <label class="toggle" title="Turn JukeB on or off"><input id="enabledToggle" type="checkbox" disabled><span class="knob"></span></label></div>
          <div class="cell"><span class="k">Box</span><span class="v" id="stBox">—</span><span class="sub" id="stSeen"></span></div>
          <div class="cell"><span class="k">Signal</span><span class="v" id="stRssi">—</span><span class="sub" id="stBoxId"></span></div>
          <div class="cell"><span class="k">Firmware</span><span class="v" id="stFw">—</span><span class="sub"></span></div>
          <div class="cell"><span class="k">Playback</span><span class="v" id="stActive">—</span><span class="sub" id="stActiveWhy"></span></div>
          <div class="cell"><span class="k">Playing</span><span class="v" id="stPlaying">—</span><span class="sub" id="stPlayingUid"></span></div>
          <div class="err bad" id="stError"></div>
          <div class="foot" id="stFoot"></div>
        </div>
      </div>

      <div class="tabs" role="tablist">
        <button role="tab" data-tab="tags">Tags</button>
        <button role="tab" data-tab="media">Media</button>
        <button role="tab" data-tab="settings">Settings</button>
      </div>

      <section data-panel="tags">
      <div class="card">
        <h2>Register an object</h2>
        <div class="row">
          <span class="muted">Last scanned:</span>
          <span id="scanUid" class="uid">none yet</span>
          <span id="scanBadge" class="badge" hidden></span>
          <button id="btnUse" disabled>Use this UID</button>
        </div>
        <div class="muted" style="margin-top:8px">Tap an object on the box, then press "Use this UID" to fill the form below.</div>
      </div>

      <div class="card">
        <h2>Tags</h2>
        <table><thead><tr><th>Name</th><th>UID</th><th>File</th><th></th></tr></thead><tbody id="tagRows"></tbody></table>
        <div id="tagForm" style="margin-top:12px">
          <div class="grid">
            <div><label>UID</label><input id="fUid" placeholder="04:A3:7F:12"></div>
            <div><label>Name</label><input id="fName" placeholder="Blue dinosaur"></div>
            <div><label>File</label><select id="fFile"></select></div>
          </div>
          <div class="row" style="margin-top:12px">
            <button id="btnSaveTag" class="primary">Add tag</button>
            <button id="btnCancelTag" hidden>Cancel</button>
          </div>
          <div id="tagMsg" class="msg"></div>
        </div>
      </div>
      </section>

      <section data-panel="media" hidden>
      <div class="card">
        <h2>Media library</h2>
        <div id="drop" class="drop">Drop MP3 files here or <button id="btnPick">choose files</button>
          <input id="filePick" type="file" accept=".mp3,audio/mpeg" multiple hidden></div>
        <div id="uploads"></div>
        <div class="muted">Play previews a file here in the browser; the speaker is not involved.</div>
        <audio id="preview" controls preload="none"></audio>
        <table><thead><tr><th>File</th><th>Size</th><th></th></tr></thead><tbody id="fileRows"></tbody></table>
        <div id="fileMsg" class="msg"></div>
      </div>
      </section>

      <section data-panel="settings" hidden>
      <div class="card">
        <h2>Settings</h2>
        <div class="grid">
          <div><label>Speaker</label><select id="sPlayer"></select></div>
          <div>
            <label class="check"><input id="sVolOn" type="checkbox"> Set volume before playing</label>
            <label>Volume: <span id="sVolLabel"></span>%</label><input id="sVolume" type="range" min="0" max="100">
          </div>
          <div><label>When the object is removed</label>
            <div class="radios">
              <label><input type="radio" name="onRemove" value="none"> Keep playing</label>
              <label><input type="radio" name="onRemove" value="pause"> Pause</label>
              <label><input type="radio" name="onRemove" value="stop"> Stop</label>
            </div></div>
          <div><label>Removal grace (seconds)</label><input id="sGrace" type="number" min="0" max="60" step="1"></div>
          <div><label>Active hours (objects are ignored outside this window)</label>
            <div class="hours">
              <input id="sFrom" type="time"> <span class="muted">to</span> <input id="sUntil" type="time">
              <button id="btnClearHours" type="button">Always</button>
            </div></div>
        </div>
        <div class="row" style="margin-top:12px"><button id="btnSaveSettings" class="primary">Save settings</button></div>
        <div id="settingsMsg" class="msg"></div>
      </div>
      </section>
    `;
    this._bind();
    this._showTab(this._savedTab());
  }

  _bind() {
    this.shadowRoot.querySelectorAll(".tabs button").forEach((b) => { b.onclick = () => this._showTab(b.dataset.tab); });
    this.$("btnUse").onclick = () => {
      const uid = this._status?.last_scanned?.uid;
      if (!uid) return;
      this._editing = null;
      this.$("fUid").value = uid;
      const tag = this._tags[uid];
      if (tag) this._startEdit(uid);
      else {
        this.$("fName").value = "";
        this.$("fName").focus();
      }
    };
    this.$("btnSaveTag").onclick = () => this._saveTag();
    this.$("btnCancelTag").onclick = () => this._resetTagForm();
    this.$("tagRows").onclick = (ev) => {
      const btn = ev.target.closest("button[data-act]");
      if (!btn) return;
      const uid = btn.dataset.uid;
      if (btn.dataset.act === "play") this._playTag(uid);
      if (btn.dataset.act === "edit") this._startEdit(uid);
      if (btn.dataset.act === "del") this._deleteTag(uid);
    };
    const drop = this.$("drop");
    drop.ondragover = (ev) => { ev.preventDefault(); drop.classList.add("over"); };
    drop.ondragleave = () => drop.classList.remove("over");
    drop.ondrop = (ev) => { ev.preventDefault(); drop.classList.remove("over"); this._uploadFiles(ev.dataTransfer.files); };
    this.$("btnPick").onclick = () => this.$("filePick").click();
    this.$("filePick").onchange = (ev) => { this._uploadFiles(ev.target.files); ev.target.value = ""; };
    this.$("fileRows").onclick = (ev) => {
      const btn = ev.target.closest("button[data-act]");
      if (!btn) return;
      if (btn.dataset.act === "play") this._preview(btn.dataset.path);
      if (btn.dataset.act === "pause") this.$("preview").pause();
      if (btn.dataset.act === "del") this._deleteFile(btn.dataset.path);
    };
    const audio = this.$("preview");
    for (const evName of ["play", "pause", "ended", "emptied"]) {
      audio.addEventListener(evName, () => { this._previewPlaying = !audio.paused && !audio.ended; this._renderFiles(); });
    }
    this.$("enabledToggle").onchange = (ev) => this._setEnabled(ev.target.checked);
    this.$("btnClearHours").onclick = () => { this.$("sFrom").value = ""; this.$("sUntil").value = ""; };
    this.$("sVolume").oninput = (ev) => { this.$("sVolLabel").textContent = ev.target.value; };
    this.$("sVolOn").onchange = (ev) => { this.$("sVolume").disabled = !ev.target.checked; };
    this.$("btnSaveSettings").onclick = () => this._saveSettings();
  }

  // ---------- tabs ----------

  _savedTab() {
    try {
      const t = localStorage.getItem(TAB_KEY);
      if (TABS.includes(t)) return t;
    } catch (_) { /* storage blocked: default tab */ }
    return TABS[0];
  }

  _showTab(name) {
    this.shadowRoot.querySelectorAll(".tabs button").forEach((b) => b.setAttribute("aria-selected", b.dataset.tab === name));
    this.shadowRoot.querySelectorAll("section[data-panel]").forEach((sec) => { sec.hidden = sec.dataset.panel !== name; });
    try { localStorage.setItem(TAB_KEY, name); } catch (_) { /* ignore */ }
  }

  // ---------- status + scan ----------

  _renderStatus() {
    const s = this._status;
    const set = (id, text, cls) => { const el = this.$(id); el.textContent = text; if (cls !== undefined) el.className = cls; };
    const toggle = this.$("enabledToggle");
    toggle.disabled = false;
    if (document.activeElement !== toggle) toggle.checked = !!s.enabled;
    set("stBox", s.online ? "Online" : "Offline", `v ${s.online ? "ok" : "bad"}`);
    set("stSeen", s.last_seen ? `seen ${ago(s.last_seen)}` : "never seen");
    set("stRssi", s.rssi != null ? `${s.rssi} dBm` : "—");
    set("stBoxId", s.box_id || "");
    set("stFw", s.version || "—");
    set("stActive", s.active ? "Active" : "Inactive", `v ${s.active ? "ok" : "muted"}`);
    set("stActiveWhy", s.active ? "" : s.active_reason);
    set("stPlaying", s.now_playing ? s.now_playing.name : "nothing", `v ${s.now_playing ? "" : "muted"}`);
    set("stPlayingUid", s.now_playing?.uid || "");
    set("stError", s.last_error ? `Last error: ${s.last_error}` : "");
    set("stFoot", `webhook ${s.webhook_id} · integration ${s.integration_version}`);
  }

  async _setEnabled(on) {
    const toggle = this.$("enabledToggle");
    toggle.disabled = true;
    try {
      this._settings = (await this._ws({ type: "jukeb/settings/set", settings: { enabled: on } })).settings;
      await this._refreshStatus();
    } catch (e) {
      this.$("stError").textContent = `Could not change Enabled: ${e.message || e}`;
      toggle.checked = !on;
    }
    toggle.disabled = false;
  }

  _renderScan() {
    const ls = this._status?.last_scanned;
    const uidEl = this.$("scanUid");
    const badge = this.$("scanBadge");
    if (!ls) { uidEl.textContent = "none yet"; badge.hidden = true; this.$("btnUse").disabled = true; return; }
    const known = !!this._tags[ls.uid];
    uidEl.textContent = ls.uid;
    badge.hidden = false;
    badge.textContent = known ? `known: ${this._tags[ls.uid].name}` : "new";
    badge.className = `badge ${known ? "" : "new"}`;
    this.$("btnUse").disabled = false;
    if (ls.uid !== this._lastScanUid || ls.at !== this._lastScanAt) {
      uidEl.classList.add("fresh");
      setTimeout(() => uidEl.classList.remove("fresh"), 1500);
      this._lastScanUid = ls.uid;
      this._lastScanAt = ls.at;
    }
  }

  // ---------- tags ----------

  _renderTags() {
    const uids = Object.keys(this._tags).sort((a, b) => this._tags[a].name.localeCompare(this._tags[b].name));
    this.$("tagRows").innerHTML = uids.length
      ? uids.map((uid) => {
          const t = this._tags[uid];
          return `<tr><td>${esc(t.name)}</td><td><span class="uid">${esc(uid)}</span></td><td>${esc(t.media.path)}</td>
            <td class="actions"><button data-act="play" data-uid="${esc(uid)}">Play</button>
            <button data-act="edit" data-uid="${esc(uid)}">Edit</button>
            <button data-act="del" data-uid="${esc(uid)}" class="danger">Delete</button></td></tr>`;
        }).join("")
      : `<tr><td colspan="4" class="muted">No tags yet. Scan an object and add it below.</td></tr>`;
    this._renderScan();
  }

  _renderFileOptions() {
    const sel = this.$("fFile");
    const current = sel.value;
    sel.innerHTML = this._files.length
      ? this._files.map((f) => `<option value="${esc(f.path)}">${esc(f.path)}</option>`).join("")
      : `<option value="">(upload an MP3 first)</option>`;
    if (current && this._files.some((f) => f.path === current)) sel.value = current;
  }

  _startEdit(uid) {
    const t = this._tags[uid];
    this._editing = uid;
    this.$("fUid").value = uid;
    this.$("fName").value = t.name;
    this.$("fFile").value = t.media.path;
    this.$("btnSaveTag").textContent = "Save changes";
    this.$("btnCancelTag").hidden = false;
    this._say("tagMsg", "", true);
    this.$("fName").focus();
  }

  _resetTagForm() {
    this._editing = null;
    this.$("fUid").value = "";
    this.$("fName").value = "";
    this.$("btnSaveTag").textContent = "Add tag";
    this.$("btnCancelTag").hidden = true;
    this._say("tagMsg", "", true);
  }

  async _saveTag() {
    const uid = this.$("fUid").value.trim();
    const name = this.$("fName").value.trim();
    const path = this.$("fFile").value;
    if (!uid) return this._say("tagMsg", "UID is required", false);
    if (!name) return this._say("tagMsg", "Name is required", false);
    if (!path) return this._say("tagMsg", "Upload an MP3 first, then pick it here", false);
    try {
      const r = await this._ws({ type: "jukeb/tags/set", uid, name, media: { type: "file", path } });
      await this._refreshTags();
      this._resetTagForm();
      this._say("tagMsg", `Saved ${r.tag.name} (${r.uid})`, true);
    } catch (e) {
      this._say("tagMsg", e.message || String(e), false);
    }
  }

  async _playTag(uid) {
    try {
      await this._ws({ type: "jukeb/play", uid });
      this._say("tagMsg", `Playing ${this._tags[uid].name}`, true);
      setTimeout(() => this._refreshStatus(), 500);
    } catch (e) {
      this._say("tagMsg", e.message || String(e), false);
    }
  }

  async _deleteTag(uid) {
    if (!confirm(`Delete tag "${this._tags[uid].name}" (${uid})?`)) return;
    try {
      await this._ws({ type: "jukeb/tags/delete", uid });
      if (this._editing === uid) this._resetTagForm();
      await this._refreshTags();
      this._say("tagMsg", "Tag deleted", true);
    } catch (e) {
      this._say("tagMsg", e.message || String(e), false);
    }
  }

  // ---------- media ----------

  _renderFiles() {
    this.$("fileRows").innerHTML = this._files.length
      ? this._files.map((f) => {
          const playing = this._previewPlaying && f.path === this._previewPath;
          const ctl = playing
            ? `<button data-act="pause" data-path="${esc(f.path)}" class="primary">Pause</button>`
            : `<button data-act="play" data-path="${esc(f.path)}">Play</button>`;
          return `<tr><td>${esc(f.path)}</td><td>${fmtSize(f.size)}</td>
            <td class="actions">${ctl} <button data-act="del" data-path="${esc(f.path)}" class="danger">Delete</button></td></tr>`;
        }).join("")
      : `<tr><td colspan="3" class="muted">No files yet.</td></tr>`;
  }

  async _preview(path) {
    const audio = this.$("preview");
    if (path === this._previewPath && audio.getAttribute("src")) { audio.play(); return; }
    const file = this._files.find((f) => f.path === path);
    if (!file) return;
    try {
      // HA signs a short-lived URL for the file; the browser streams it directly.
      const r = await this._ws({ type: "media_source/resolve_media", media_content_id: file.uri });
      this._previewPath = path;
      audio.src = r.url;
      await audio.play();
      this._say("fileMsg", "", true);
    } catch (e) {
      this._say("fileMsg", `Cannot preview ${path}: ${e.message || e}`, false);
    }
  }

  async _uploadFiles(fileList) {
    const files = Array.from(fileList || []);
    if (!files.length) return;
    const existing = new Set(this._files.map((f) => f.path));
    const dupes = files.filter((f) => existing.has(f.name)).map((f) => f.name);
    if (dupes.length && !confirm(`These files already exist and will be replaced:\n${dupes.join("\n")}\n\nContinue?`)) return;
    for (const file of files) await this._uploadOne(file);
    await this._refreshFiles();
  }

  async _uploadOne(file) {
    // HA access tokens expire after 30 min and are refreshed lazily; XHR bypasses fetchWithAuth.
    if (this._hass.auth.expired) {
      try { await this._hass.auth.refreshAccessToken(); } catch (_) { /* the request below will report 401 */ }
    }
    const box = document.createElement("div");
    box.innerHTML = `<div class="row"><span>${esc(file.name)}</span><span class="muted up-state">0%</span></div><progress max="100" value="0"></progress>`;
    this.$("uploads").appendChild(box);
    const bar = box.querySelector("progress");
    const state = box.querySelector(".up-state");
    const form = new FormData();
    form.append("file", file, file.name);
    return new Promise((resolve) => {
      const xhr = new XMLHttpRequest();
      xhr.open("POST", UPLOAD_URL);
      xhr.setRequestHeader("Authorization", `Bearer ${this._hass.auth.data.access_token}`);
      xhr.upload.onprogress = (ev) => {
        if (ev.lengthComputable) { bar.value = Math.round((ev.loaded / ev.total) * 100); state.textContent = `${bar.value}%`; }
      };
      xhr.onload = () => {
        let body = {};
        try { body = JSON.parse(xhr.responseText); } catch (_) { /* ignore */ }
        if (xhr.status === 200) { state.textContent = "done"; state.className = "ok up-state"; setTimeout(() => box.remove(), 2000); }
        else { state.textContent = body.error || `HTTP ${xhr.status}`; state.className = "bad up-state"; }
        resolve();
      };
      xhr.onerror = () => { state.textContent = "upload failed"; state.className = "bad up-state"; resolve(); };
      xhr.send(form);
    });
  }

  async _deleteFile(path) {
    if (!confirm(`Delete "${path}"?`)) return;
    try {
      await this._ws({ type: "jukeb/media/delete", path });
      if (path === this._previewPath) { const a = this.$("preview"); a.pause(); a.removeAttribute("src"); a.load(); this._previewPath = null; }
      await this._refreshFiles();
      this._say("fileMsg", `Deleted ${path}`, true);
    } catch (e) {
      this._say("fileMsg", e.message || String(e), false);
    }
  }

  // ---------- settings ----------

  _renderPlayers() {
    const states = this._hass?.states || {};
    const ids = Object.keys(states).filter((id) => id.startsWith("media_player.")).sort();
    if (ids.length === this._playerCount) return;
    this._playerCount = ids.length;
    const sel = this.$("sPlayer");
    const current = sel.value || this._settings?.player || "";
    const opts = ids.map((id) => `<option value="${esc(id)}">${esc(states[id].attributes.friendly_name || id)} (${esc(id)})</option>`);
    if (current && !ids.includes(current)) opts.unshift(`<option value="${esc(current)}">${esc(current)} (not found)</option>`);
    sel.innerHTML = opts.join("") || `<option value="">(no media players found)</option>`;
    sel.value = current;
  }

  _renderSettings() {
    const s = this._settings;
    this._playerCount = -1;
    this._renderPlayers();
    this.$("sPlayer").value = s.player;
    const volOn = s.volume != null;
    const vol = Math.round((volOn ? s.volume : 0.3) * 100);
    this.$("sVolOn").checked = volOn;
    this.$("sVolume").disabled = !volOn;
    this.$("sVolume").value = vol;
    this.$("sVolLabel").textContent = vol;
    const radio = this.shadowRoot.querySelector(`input[name=onRemove][value=${s.on_remove}]`);
    if (radio) radio.checked = true;
    this.$("sGrace").value = s.remove_grace_s;
    this.$("sFrom").value = s.active_from || "";
    this.$("sUntil").value = s.active_until || "";
    this.$("enabledToggle").checked = !!s.enabled;
  }

  async _saveSettings() {
    const onRemove = this.shadowRoot.querySelector("input[name=onRemove]:checked")?.value || "pause";
    const grace = this.$("sGrace").value === "" ? this._settings.remove_grace_s : Number(this.$("sGrace").value);
    const settings = {
      player: this.$("sPlayer").value,
      volume: this.$("sVolOn").checked ? Number(this.$("sVolume").value) / 100 : null,
      on_remove: onRemove,
      remove_grace_s: grace,
      enabled: this._settings.enabled,
      active_from: this.$("sFrom").value || null,
      active_until: this.$("sUntil").value || null,
    };
    try {
      this._settings = (await this._ws({ type: "jukeb/settings/set", settings })).settings;
      this._renderSettings();
      this._say("settingsMsg", "Saved", true);
      this._refreshStatus();
    } catch (e) {
      this._say("settingsMsg", e.message || String(e), false);
    }
  }
}

customElements.define("jukeb-panel", JukeBPanel);

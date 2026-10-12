/* Jukebox panel: vanilla web component, no build step, no external imports. */
const UPLOAD_URL = "/api/jukebox/upload";
const POLL_MS = 2000;

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
`;

class JukeboxPanel extends HTMLElement {
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
    this._playerCount = -1;
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
    await Promise.all([this._refreshStatus(), this._refreshTags(), this._refreshFiles(), this._refreshSettings()]);
  }

  async _refreshStatus() {
    try {
      this._status = await this._ws({ type: "jukebox/status" });
      this._renderStatus();
      this._renderScan();
    } catch (e) {
      this.$("status").innerHTML = `<span class="bad">Cannot reach the Jukebox integration: ${esc(e.message || e)}</span>`;
    }
  }

  async _refreshTags() {
    this._tags = (await this._ws({ type: "jukebox/tags/list" })).tags;
    this._renderTags();
  }

  async _refreshFiles() {
    this._files = (await this._ws({ type: "jukebox/media/list" })).files;
    this._renderFiles();
    this._renderFileOptions();
  }

  async _refreshSettings() {
    this._settings = (await this._ws({ type: "jukebox/settings/get" })).settings;
    this._renderSettings();
  }

  // ---------- shell ----------

  _renderShell() {
    this._rendered = true;
    this.shadowRoot.innerHTML = `
      <style>${STYLE}</style>
      <h1>Jukebox</h1>
      <div class="card"><div id="status" class="row">Loading…</div></div>

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

      <div class="card">
        <h2>Media library</h2>
        <div id="drop" class="drop">Drop MP3 files here or <button id="btnPick">choose files</button>
          <input id="filePick" type="file" accept=".mp3,audio/mpeg" multiple hidden></div>
        <div id="uploads"></div>
        <table><thead><tr><th>File</th><th>Size</th><th></th></tr></thead><tbody id="fileRows"></tbody></table>
        <div id="fileMsg" class="msg"></div>
      </div>

      <div class="card">
        <h2>Settings</h2>
        <div class="grid">
          <div><label>Speaker</label><select id="sPlayer"></select></div>
          <div><label>Default volume: <span id="sVolLabel"></span>%</label><input id="sVolume" type="range" min="0" max="100"></div>
          <div><label>When the object is removed</label>
            <div class="radios">
              <label><input type="radio" name="onRemove" value="none"> Keep playing</label>
              <label><input type="radio" name="onRemove" value="pause"> Pause</label>
              <label><input type="radio" name="onRemove" value="stop"> Stop</label>
            </div></div>
          <div><label>Removal grace (seconds)</label><input id="sGrace" type="number" min="0" max="60" step="1"></div>
        </div>
        <div class="row" style="margin-top:12px"><button id="btnSaveSettings" class="primary">Save settings</button></div>
        <div id="settingsMsg" class="msg"></div>
      </div>
    `;
    this._bind();
  }

  _bind() {
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
      const btn = ev.target.closest("button[data-act=del]");
      if (btn) this._deleteFile(btn.dataset.path);
    };
    this.$("sVolume").oninput = (ev) => { this.$("sVolLabel").textContent = ev.target.value; };
    this.$("btnSaveSettings").onclick = () => this._saveSettings();
  }

  // ---------- status + scan ----------

  _renderStatus() {
    const s = this._status;
    const parts = [];
    parts.push(s.online
      ? `<span class="ok">Online</span><span class="muted">last seen ${esc(ago(s.last_seen))}</span>`
      : `<span class="bad">Offline</span><span class="muted">${s.last_seen ? "last seen " + esc(ago(s.last_seen)) : "never seen"}</span>`);
    if (s.rssi != null) parts.push(`<span class="muted">RSSI ${esc(s.rssi)} dBm</span>`);
    if (s.version) parts.push(`<span class="muted">firmware ${esc(s.version)}</span>`);
    if (s.box_id) parts.push(`<span class="muted">box ${esc(s.box_id)}</span>`);
    parts.push(s.now_playing
      ? `<span>Now playing: <b>${esc(s.now_playing.name)}</b></span>`
      : `<span class="muted">Nothing playing</span>`);
    parts.push(`<span class="muted">webhook ${esc(s.webhook_id)} · integration ${esc(s.integration_version)}</span>`);
    if (s.last_error) parts.push(`<span class="bad" style="flex-basis:100%">Last error: ${esc(s.last_error)}</span>`);
    this.$("status").innerHTML = parts.join("");
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
      const r = await this._ws({ type: "jukebox/tags/set", uid, name, media: { type: "file", path } });
      await this._refreshTags();
      this._resetTagForm();
      this._say("tagMsg", `Saved ${r.tag.name} (${r.uid})`, true);
    } catch (e) {
      this._say("tagMsg", e.message || String(e), false);
    }
  }

  async _playTag(uid) {
    try {
      await this._ws({ type: "jukebox/play", uid });
      this._say("tagMsg", `Playing ${this._tags[uid].name}`, true);
      setTimeout(() => this._refreshStatus(), 500);
    } catch (e) {
      this._say("tagMsg", e.message || String(e), false);
    }
  }

  async _deleteTag(uid) {
    if (!confirm(`Delete tag "${this._tags[uid].name}" (${uid})?`)) return;
    try {
      await this._ws({ type: "jukebox/tags/delete", uid });
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
      ? this._files.map((f) => `<tr><td>${esc(f.path)}</td><td>${fmtSize(f.size)}</td>
          <td class="actions"><button data-act="del" data-path="${esc(f.path)}" class="danger">Delete</button></td></tr>`).join("")
      : `<tr><td colspan="3" class="muted">No files yet.</td></tr>`;
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

  _uploadOne(file) {
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
      await this._ws({ type: "jukebox/media/delete", path });
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
    this.$("sVolume").value = Math.round(s.volume * 100);
    this.$("sVolLabel").textContent = Math.round(s.volume * 100);
    const radio = this.shadowRoot.querySelector(`input[name=onRemove][value=${s.on_remove}]`);
    if (radio) radio.checked = true;
    this.$("sGrace").value = s.remove_grace_s;
  }

  async _saveSettings() {
    const onRemove = this.shadowRoot.querySelector("input[name=onRemove]:checked")?.value || "pause";
    const grace = this.$("sGrace").value === "" ? this._settings.remove_grace_s : Number(this.$("sGrace").value);
    const settings = {
      player: this.$("sPlayer").value,
      volume: Number(this.$("sVolume").value) / 100,
      on_remove: onRemove,
      remove_grace_s: grace,
    };
    try {
      this._settings = (await this._ws({ type: "jukebox/settings/set", settings })).settings;
      this._renderSettings();
      this._say("settingsMsg", "Saved", true);
    } catch (e) {
      this._say("settingsMsg", e.message || String(e), false);
    }
  }
}

customElements.define("jukebox-panel", JukeboxPanel);

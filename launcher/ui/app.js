"use strict";

const invoke = window.__TAURI__.core.invoke;
const $ = (s, root = document) => root.querySelector(s);
const $$ = (s, root = document) => [...root.querySelectorAll(s)];

const GROUPS = [
  ["walk", "Walk & run", "Ground speed, top running speed and speed out of turns."],
  ["squat", "Crouch walk", "Speed while crouch-walking."],
  ["dive", "Dive", "Dive (head slide) speed."],
  ["roll", "Roll", "Roll start, boost and top speed."],
  ["long_jump", "Long jump", "Long jump launch and travel speed."],
  ["air", "Air speed", "Sideways speed during jumps. Jump height has its own page."],
  ["swim", "Swim", "Surface, underwater and seafloor-walk speeds."],
];

// English names for the game's internal capture ids. Only ones we're sure of; others show the id.
const CAPTURE_NAMES = {
  Kuribo: "Goomba", Frog: "Frog", Killer: "Bullet Bill", TRex: "T-Rex", Senobi: "Uproot",
  Megane: "Moe-Eye", Imomu: "Tropical Wiggler", Bubble: "Lava Bubble", Pukupuku: "Cheep Cheep",
  Jugem: "Lakitu", Wanwan: "Chain Chomp", Tank: "Sherm", Motorcycle: "Motor Scooter", Yoshi: "Yoshi",
  Bull: "Chargin' Chuck", Fastener: "Zipper", ElectricWire: "Spark Pylon", Tsukkun: "Pokio",
};

const FP_RULES = [
  ["peek", "Hold the button to peek", "Holding it shows the normal camera until you let go. A quick tap still switches views."],
  ["off_cutscenes", "Cutscenes & scripted cameras", "Moon gets, story scenes, and any camera the right stick can't turn."],
  ["off_2d", "8-bit 2D sections", "The flat side-on wall sections."],
  ["off_captures", "Captures", "Off here means you look out from the captured object instead."],
];

let status = null;
let settings = null;

// ---------------------------------------------------------------- helpers

function esc(s) {
  return String(s ?? "").replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}

function toast(msg, bad = false) {
  const t = $("#toast");
  t.textContent = msg;
  t.className = "toast" + (bad ? " bad" : "");
  t.hidden = false;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => (t.hidden = true), bad ? 8000 : 4500);
}

function pill(card, text, tone = "") {
  const p = $(`${card} [data-pill]`);
  p.textContent = text;
  p.className = "pill" + (tone ? " " + tone : "");
}

function dl(card, rows) {
  $(`${card} dl`).innerHTML = rows
    .filter(Boolean)
    .map(([k, v]) => `<dt>${esc(k)}</dt><dd>${v}</dd>`)
    .join("");
}

function ago(secs) {
  if (!secs) return "never";
  const d = Date.now() / 1000 - secs;
  if (d < 90) return "just now";
  if (d < 3600) return `${Math.round(d / 60)} min ago`;
  if (d < 86400) return `${Math.round(d / 3600)} h ago`;
  return new Date(secs * 1000).toLocaleDateString();
}

const baseName = p => (p || "").split(/[\\/]/).pop();
const mult = v => `${Number(v).toFixed(2)}×`;

async function busy(btn, fn) {
  const was = btn.disabled;
  btn.disabled = true;
  try {
    return await fn();
  } catch (e) {
    toast(String(e), true);
  } finally {
    btn.disabled = was;
  }
}

// ---------------------------------------------------------------- status

function evidence(s) {
  const log = s.log || {};
  const buildOk = (log.build_ids || []).includes(s.expected_build_id);
  return {
    buildOk,
    anyBuild: (log.build_ids || []).length > 0,
    found: log.moonrush_found_enabled,
    loaded: log.slot_replaced,
    reported: !!log.applied_in_game,
  };
}

function render(s) {
  status = s;
  const ev = evidence(s);
  const mr = s.moonrush;
  const game = s.game || {};

  // Game
  if (!s.exe) {
    pill("#card-game", "unknown");
    dl("#card-game", [["File", "—"]]);
  } else {
    const tone = game.update_selected ? "bad" : ev.buildOk ? "ok" : game.path ? "accent" : "bad";
    const label = game.update_selected ? "update selected" : ev.buildOk ? "1.0.0 verified" : game.path ? "found" : "not found";
    pill("#card-game", label, tone);
    dl("#card-game", [
      ["File", game.path ? `<code>${esc(baseName(game.path))}</code>` : "Not found in Ryujinx's game folders"],
      ["Version", ev.buildOk
        ? "1.0.0 (build ID confirmed in the last Ryujinx log)"
        : ev.anyBuild ? "<span style='color:var(--bad)'>Not 1.0.0: last log shows a different build</span>"
        : "1.0.0 expected. Confirmed after the first launch."],
      ["Update", game.update_selected ? `<code>${esc(baseName(game.update_selected))}</code>` : "None (good)"],
    ]);
  }

  // Ryujinx
  if (!s.exe) {
    pill("#card-ryujinx", "not found", "bad");
    dl("#card-ryujinx", [["Exe", "No Ryujinx.exe found. Set it in Setup."]]);
  } else {
    pill("#card-ryujinx", s.ryujinx_running ? "running" : "ready", s.ryujinx_running ? "warn" : "ok");
    const inst = s.installs.find(i => i.exe === s.exe);
    dl("#card-ryujinx", [
      ["Exe", `<code>${esc(s.exe)}</code>`],
      ["Data", `<code>${esc(s.data_dir)}</code>`],
      ["Last used", esc(ago(inst?.last_used))],
    ]);
  }

  // Moonrush mod
  const install = $("#install");
  const enabled = $("#enabled");
  const uninstall = $("#uninstall");
  const installed = !!mr?.installed;
  let modLabel, modTone;
  if (installed && mr.outdated) [modLabel, modTone] = ["update ready", "warn"];
  else if (installed && mr.enabled) [modLabel, modTone] = ["enabled", "ok"];
  else if (installed) [modLabel, modTone] = ["disabled", ""];
  else [modLabel, modTone] = ["not installed", ""];
  pill("#card-mod", modLabel, modTone);
  dl("#card-mod", [
    ["Files", installed ? `<code>${esc(mr.files.join(", "))}</code>` : "—"],
    ["Location", installed ? `<code>${esc(mr.path)}</code>` : "—"],
    ["Build", s.module_source ? "Built and ready to install" : "<span style='color:var(--warn)'>Game module not built yet</span>"],
  ]);
  install.textContent = installed ? (mr.outdated ? "Update" : "Reinstall") : "Install";
  install.disabled = !s.exe || !s.module_built;
  enabled.checked = installed && mr.enabled;
  enabled.disabled = !installed;
  uninstall.disabled = !installed;

  // Other mods
  const others = s.other_mods || [];
  const issues = [
    ...s.conflicts.map(c => `<li class="issue">${esc(c)}</li>`),
    ...s.notes.map(n => `<li class="issue note">${esc(n)}</li>`),
  ];
  $("#card-others .mods").innerHTML =
    others.map(m => `
      <li>
        <span class="name">${esc(m.name)}<span class="pill ${m.enabled ? "ok" : ""}">${m.enabled ? "enabled" : "disabled"}</span></span>
        <span class="files">${esc([...m.exefs, m.has_romfs ? "romfs" : ""].filter(Boolean).join(" · ") || "no files")}</span>
      </li>`).join("") +
    issues.join("") ||
    `<li class="muted">No other mods for SMO.</li>`;
  pill("#card-others", s.conflicts.length ? "conflict" : others.length ? "no conflicts" : "", s.conflicts.length ? "bad" : "ok");

  // Last session evidence (only real log data)
  const log = s.log || {};
  $("#evidence-source").textContent = log.file
    ? `From ${baseName(log.file)} · ${ago(log.modified)}`
    : "No Ryujinx log yet. Launch SMO once to collect evidence.";
  const stacked = log.stacked || [];
  const checks = [
    [ev.buildOk, ev.anyBuild ? "bad" : "", "SMO 1.0.0 build ID", ev.buildOk ? "matches" : ev.anyBuild ? "different build" : "not seen"],
    [ev.found, "", "Ryujinx found Moonrush enabled", ev.found ? "yes" : log.moonrush_found_disabled ? "found, but disabled" : "not seen"],
    [ev.loaded, "", "Moonrush code loaded (subsdk8)", ev.loaded ? "yes" : "not seen"],
    [log.hooks_installed && !log.inactive, log.inactive ? "bad" : "", "Version check passed, hooks installed",
      log.inactive ? "failed: see the log below" : log.hooks_installed ? (stacked.length ? `yes, stacking on ${stacked.length} of another mod's hooks` : "yes") : "not seen"],
    [log.applied_in_game, "", "Moon Speed applied while playing", log.applied_in_game ? "yes" : "not seen yet (reach a kingdom)"],
  ];
  $("#card-evidence .checks").innerHTML = checks.map(([ok, failTone, what, detail]) =>
    `<li class="${ok ? "ok" : failTone || "warn"}"><span class="dot"></span><span>${esc(what)}</span><span class="detail">${esc(detail)}</span></li>`).join("");
  const allOk = checks.every(c => c[0]);
  pill("#card-evidence", allOk ? "verified working" : log.file ? "not verified" : "no data", allOk ? "ok" : "");
  const g = $("#guest-lines");
  g.hidden = !(log.guest_lines || []).length;
  g.textContent = (log.guest_lines || []).slice(-12).join("\n");

  // Setup
  const sel = $("#exe-select");
  sel.innerHTML = s.installs.map(i =>
    `<option value="${esc(i.exe)}"${i.exe === s.exe ? " selected" : ""}>${esc(i.exe)} (used ${esc(ago(i.last_used))})</option>`).join("")
    || `<option value="">No Ryujinx found</option>`;

  // Hero
  const eb = $("#hero-eyebrow");
  let sub;
  if (!s.exe) { eb.textContent = "Needs setup"; eb.className = "eyebrow bad"; sub = "Point Moonrush at your Ryujinx.exe below."; }
  else if (!game.path) { eb.textContent = "Needs setup"; eb.className = "eyebrow bad"; sub = "Couldn't find Super Mario Odyssey. Set the game file below."; }
  else if (s.conflicts.length) { eb.textContent = "Needs attention"; eb.className = "eyebrow warn"; sub = s.conflicts[0]; }
  else if (!installed) { eb.textContent = "Almost ready"; eb.className = "eyebrow warn"; sub = s.module_built ? "Install the Moonrush mod, then press Play." : "The game module isn't built yet, so SMO will start without Moonrush."; }
  else if (!mr.enabled) { eb.textContent = "Moonrush disabled"; eb.className = "eyebrow warn"; sub = "SMO will start vanilla. Turn Moonrush on in the mod card to use it."; }
  else { eb.textContent = allOk ? "Ready · verified" : "Ready"; eb.className = "eyebrow"; sub = allOk ? "Moonrush loaded and reported in last session." : "Installed and enabled. Press Play to choose your settings."; }
  $("#hero-sub").textContent = sub;
  $("#play").disabled = !s.exe || !game.path;
}

async function refresh() {
  try {
    const s = await invoke("get_status");
    s.module_built = !!s.module_source;
    render(s);
  } catch (e) {
    toast(String(e), true);
  }
}

// ---------------------------------------------------------------- settings window

function syncSettingsUI() {
  const m = settings.moon_speed;
  $("#ms-enabled").checked = m.enabled;
  $('.page[data-page="speed"]').classList.toggle("off", !m.enabled);
  $("#ms-start").value = m.start;
  $("#ms-per").value = m.per_moon;
  $("#ms-max").min = m.start;
  $("#ms-max").value = m.max;
  $("#o-start").textContent = mult(m.start);
  $("#o-per").textContent = `+${(m.per_moon * 100).toFixed(2)}%`;
  $("#o-max").textContent = mult(m.max);
  for (const seg of $$(".seg[data-key]")) {
    const v = m[seg.dataset.key];
    for (const b of $$("button", seg)) b.classList.toggle("on", b.dataset.v === v);
  }
  $("#count-hint").textContent = m.count === "total"
    ? "Every Moon on this save file. Only goes up."
    : "Moons you're holding. Drops when you power up the Odyssey, so Mario slows down after paying.";
  for (const [key] of GROUPS) $(`#g-${key}`).checked = m.groups[key];
  syncCaptures();
  syncFirstPerson();
  syncJump();

  const warns = [];
  if (m.start < 0.6) warns.push("Below 0.60× some early gaps and long jumps may be hard or impossible to clear.");
  if (m.max > 2.5) warns.push("Above 2.50× Mario can pass through thin walls, because collision is checked once per frame.");
  const w = $("#speed-warn");
  w.hidden = !warns.length;
  w.textContent = warns.join(" ");
  drawChart();
  footMessage();
}

async function drawChart() {
  const info = await invoke("curve", { settings });
  const m = settings.moon_speed;
  const xMax = Math.min(1000, Math.max(120, Math.ceil(((info.moons_to_max ?? 400) * 1.15) / 50) * 50));
  const yMax = Math.max(1.25, m.max * 1.12);
  const W = 600, H = 200;
  const x = n => (n / xMax) * W;
  const y = v => H - (v / yMax) * H;
  const pts = info.points.filter(p => p.moons <= xMax);
  const line = pts.map((p, i) => `${i ? "L" : "M"}${x(p.moons).toFixed(1)},${y(p.mult).toFixed(1)}`).join("");
  let grid = "";
  for (let v = 0.5; v < yMax; v += 0.5) grid += `<line class="grid-line" x1="0" x2="${W}" y1="${y(v)}" y2="${y(v)}"/>`;
  $("#chart").innerHTML =
    grid +
    `<line class="vanilla" x1="0" x2="${W}" y1="${y(1)}" y2="${y(1)}"/>` +
    `<path class="area" d="${line}L${W},${H}L0,${H}Z"/>` +
    `<path class="curve" d="${line}"/>`;
  const capAt = info.moons_to_max != null ? Math.ceil(info.moons_to_max) : null;
  $("#chart-meta").innerHTML = [
    `0 Moons: <b>${mult(m.start)}</b>`,
    info.vanilla_at != null ? (info.vanilla_at === 0 ? "At or above vanilla from the start" : `Vanilla speed at <b>${info.vanilla_at}</b> Moons`) : "Never reaches vanilla speed",
    capAt != null ? `Max <b>${mult(m.max)}</b> at <b>${capAt}</b> Moons` : "Speed never changes",
    `<span>Dashed line = vanilla (1.00×) · x-axis 0–${xMax} Moons</span>`,
  ].map(s => `<span>${s}</span>`).join("");
}

function syncCaptures() {
  const c = settings.captures;
  $("#cap-enabled").checked = c.enabled;
  $("#cappy-enabled").checked = c.cappy;
  $('.page[data-page="captures"]').classList.toggle("off", !c.enabled && !c.cappy);
  const seen = status?.captures_seen || [];
  // Names switched off but not seen in any log still get a row, so they can be switched back on.
  const rows = [...seen];
  for (const n of c.off) if (!rows.some(r => r.name === n)) rows.push({ name: n, path: null });
  $("#cap-hint").textContent = rows.length
    ? "On = sped up. A capture shows up here after you've captured it once with Moonrush installed."
    : "Nothing yet. Captures show up here after you've captured them once with Moonrush installed.";
  $("#cap-list").innerHTML = rows.map(r => {
    const label = CAPTURE_NAMES[r.name] ? `${CAPTURE_NAMES[r.name]} <small class="muted">${esc(r.name)}</small>` : esc(r.name);
    const how = r.path === "none"
      ? "Moves a way Moonrush can't speed up (e.g. along a rail). Stays vanilla either way."
      : r.path ? "Sped up with the multiplier." : "Not seen moving yet.";
    return `
    <label class="toggle">
      <span class="switch"><input type="checkbox" data-capture="${esc(r.name)}"${c.off.includes(r.name) ? "" : " checked"}${c.enabled ? "" : " disabled"}><span></span></span>
      <span><span class="t-title">${label}</span><p class="t-desc">${esc(how)}</p></span>
    </label>`;
  }).join("");
}

function syncJump() {
  const j = settings.jump;
  $("#jump-enabled").checked = j.enabled;
  $('.page[data-page="jump"]').classList.toggle("off", !j.enabled);
  $("#jump-height").value = j.height;
  $("#o-jump").textContent = mult(j.height);
  const launch = Math.sqrt(j.height);
  $("#jump-hint").textContent = j.height <= 1
    ? "1.00× = vanilla jumps."
    : `Mario jumps ${j.height.toFixed(2)}× as high (launch speed ×${launch.toFixed(2)}, gravity unchanged).`;
  const w = $("#jump-warn");
  w.hidden = !(j.enabled && j.height > 2.5);
  w.textContent = "Above 2.50× Mario can clear tall walls and skip parts of levels, and long falls take longer.";
}

function syncFirstPerson() {
  const f = settings.first_person;
  $("#fp-enabled").checked = f.enabled;
  $('.page[data-page="fp"]').classList.toggle("off", !f.enabled);
  for (const b of $$('[data-fp="button"] button')) b.classList.toggle("on", b.dataset.v === f.button);
  $("#fp-button-hint").textContent = f.peek
    ? "Tap to switch first/third person. Hold to peek. (Not the right stick: SMO uses its click for its own look-around view.)"
    : "Press to switch first/third person. (Not the right stick: SMO uses its click for its own look-around view.)";
  for (const [key] of FP_RULES) $(`#fp-${key}`).checked = f[key];
}

function footMessage() {
  const f = $("#foot-msg");
  const mr = status?.moonrush;
  if (status?.conflicts?.length) { f.textContent = status.conflicts[0]; f.className = "foot-msg bad"; }
  else if (!mr?.installed) { f.textContent = "Moonrush isn't installed. SMO will start without it."; f.className = "foot-msg warn"; }
  else if (!mr.enabled) { f.textContent = "Moonrush is disabled in Ryujinx. SMO will start vanilla."; f.className = "foot-msg warn"; }
  else if (!settings.moon_speed.enabled) { f.textContent = "Moon Speed is off. Mario moves at vanilla speed."; f.className = "foot-msg"; }
  else { f.textContent = "Settings are read when SMO starts."; f.className = "foot-msg"; }
  $("#launch").disabled = !!status?.ryujinx_running;
  if (status?.ryujinx_running) { f.textContent = "Close Ryujinx first; settings are read when SMO starts."; f.className = "foot-msg warn"; }
}

function showPage(name) {
  for (const n of $$(".nav[data-page]")) n.classList.toggle("on", n.dataset.page === name);
  for (const p of $$(".page")) p.hidden = p.dataset.page !== name;
}

async function openSettings() {
  settings = await invoke("get_settings");
  showPage("speed");
  $("#settings").hidden = false;
  syncSettingsUI();
  $(".nav.on").focus();
}

function closeSettings() {
  $("#settings").hidden = true;
  $("#play").focus();
}

function buildFpRules() {
  $("#fp-rules").innerHTML = FP_RULES.map(([key, title, desc]) => `
    <label class="toggle">
      <span class="switch"><input type="checkbox" id="fp-${key}" data-fp-rule="${key}"><span></span></span>
      <span><span class="t-title">${esc(title)}</span><p class="t-desc">${esc(desc)}</p></span>
    </label>`).join("");
}

function buildToggles() {
  $("#groups").innerHTML = GROUPS.map(([key, title, desc]) => `
    <label class="toggle">
      <span class="switch"><input type="checkbox" id="g-${key}" data-group="${key}"><span></span></span>
      <span><span class="t-title">${esc(title)}</span><p class="t-desc">${esc(desc)}</p></span>
    </label>`).join("");
}

// ---------------------------------------------------------------- wiring

function wire() {
  buildToggles();
  buildFpRules();
  $("#refresh").onclick = e => busy(e.currentTarget, refresh);
  $("#play").onclick = () => openSettings().catch(e => toast(String(e), true));
  $("#cancel").onclick = closeSettings;
  $("#settings").addEventListener("keydown", e => { if (e.key === "Escape") closeSettings(); });
  $("#settings").addEventListener("mousedown", e => { if (e.target.id === "settings") closeSettings(); });
  for (const n of $$(".nav[data-page]")) n.onclick = () => showPage(n.dataset.page);

  const m = () => settings.moon_speed;
  $("#ms-enabled").onchange = e => { m().enabled = e.target.checked; syncSettingsUI(); };
  $("#ms-start").oninput = e => { m().start = +e.target.value; if (m().max < m().start) m().max = m().start; syncSettingsUI(); };
  $("#ms-per").oninput = e => { m().per_moon = +e.target.value; syncSettingsUI(); };
  $("#ms-max").oninput = e => { m().max = +e.target.value; syncSettingsUI(); };
  for (const seg of $$(".seg[data-key]")) {
    seg.onclick = e => {
      const b = e.target.closest("button");
      if (!b) return;
      m()[seg.dataset.key] = b.dataset.v;
      syncSettingsUI();
    };
  }
  $("#jump-enabled").onchange = e => { settings.jump.enabled = e.target.checked; syncJump(); };
  $("#jump-height").oninput = e => { settings.jump.height = +e.target.value; syncJump(); };
  $("#fp-enabled").onchange = e => { settings.first_person.enabled = e.target.checked; syncFirstPerson(); };
  $("#fp-rules").onchange = e => {
    const k = e.target.dataset.fpRule;
    if (k) { settings.first_person[k] = e.target.checked; syncFirstPerson(); }
  };
  $('[data-fp="button"]').onclick = e => {
    const b = e.target.closest("button");
    if (b) { settings.first_person.button = b.dataset.v; syncFirstPerson(); }
  };
  $("#cap-enabled").onchange = e => { settings.captures.enabled = e.target.checked; syncCaptures(); };
  $("#cappy-enabled").onchange = e => { settings.captures.cappy = e.target.checked; syncCaptures(); };
  $("#cap-list").onchange = e => {
    const n = e.target.dataset.capture;
    if (!n) return;
    const off = new Set(settings.captures.off);
    if (e.target.checked) off.delete(n); else off.add(n);
    settings.captures.off = [...off].sort();
  };
  $("#groups").onchange = e => {
    const g = e.target.dataset.group;
    if (g) m().groups[g] = e.target.checked;
  };

  $("#save").onclick = e => busy(e.currentTarget, async () => {
    settings = await invoke("save_settings", { settings });
    syncSettingsUI();
    toast("Saved. SMO reads these the next time it starts.");
    refresh();
  });
  $("#launch").onclick = e => busy(e.currentTarget, async () => {
    const msg = await invoke("launch", { settings });
    closeSettings();
    toast(msg + ". Have fun!");
    setTimeout(refresh, 4000);
  });

  $("#install").onclick = e => busy(e.currentTarget, async () => {
    toast(await invoke("install_mod"));
    await refresh();
  });
  $("#uninstall").onclick = e => busy(e.currentTarget, async () => {
    toast(await invoke("uninstall_mod"));
    await refresh();
  });
  $("#enabled").onchange = async e => {
    const on = e.target.checked;
    try {
      await invoke("set_mod_enabled", { enabled: on });
      toast(on ? "Moonrush enabled in Ryujinx." : "Moonrush disabled. SMO will run vanilla.");
    } catch (err) {
      e.target.checked = !on;
      toast(String(err), true);
    }
    refresh();
  };

  $("#exe-save").onclick = e => busy(e.currentTarget, async () => {
    const exe = $("#exe-custom").value.trim() || $("#exe-select").value;
    if (!exe) return;
    await invoke("set_prefs", { ryujinxExe: exe, gamePath: null });
    $("#exe-custom").value = "";
    toast("Using " + exe);
    await refresh();
  });
  $("#exe-select").onchange = async e => {
    try {
      await invoke("set_prefs", { ryujinxExe: e.target.value, gamePath: null });
      await refresh();
    } catch (err) { toast(String(err), true); }
  };
  $("#game-save").onclick = e => busy(e.currentTarget, async () => {
    await invoke("set_prefs", { ryujinxExe: null, gamePath: $("#game-path").value.trim() });
    toast($("#game-path").value.trim() ? "Game file saved." : "Back to auto-detect.");
    await refresh();
  });

  window.addEventListener("focus", () => { if ($("#settings").hidden) refresh(); });
}

wire();
refresh();

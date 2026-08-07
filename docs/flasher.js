// PulseMQTT web flasher — client logic.
//
// Firmware binaries are served same-origin from firmware/latest/ (not fetched
// from GitHub Releases): release-assets.githubusercontent.com does not send
// Access-Control-Allow-Origin, so a cross-origin fetch() of a release asset
// fails in the browser before esp-web-tools ever gets bytes to flash. A CI
// step is expected to copy the tagged release's factory .bin files here (see
// firmware/latest/README.md) — until that runs once, the install button
// shows a "firmware not published yet" status instead of failing silently.

const BOARDS = {
  cyd: {
    label: 'CYD · ESP32-2432S028R (v1/v2, ILI9341)',
    chipFamily: 'ESP32',
    slug: 'cyd',
    display: 'ILI9341 320×240 SPI + XPT2046 touch (fixed)',
    note: 'Verified on real hardware. Single USB-C/micro-USB port. If your board has two USB ports, use "CYD v3" instead.',
  },
  'cyd-v3': {
    label: 'CYD · ESP32-2432S028R v3 (ST7789, two USB ports)',
    chipFamily: 'ESP32',
    slug: 'cyd-v3',
    display: 'ST7789 320×240 SPI, inverted colors + XPT2046 touch (fixed)',
    note: 'Not yet verified on real hardware — should work as-is. Open an issue if colors look wrong on boot.',
  },
  jc8048w550: {
    label: 'Guition JC8048W550 (ESP32-S3, 800×480 RGB)',
    chipFamily: 'ESP32-S3',
    slug: 'jc8048w550',
    display: 'ST7262 800×480 RGB-parallel + GT911 touch (fixed)',
    note: 'Verified on real hardware, including backlight PWM.',
  },
  esp32: {
    label: 'Generic ESP32 (classic)',
    chipFamily: 'ESP32',
    slug: 'esp32',
    display: 'pick GC9A01 / ILI9488 / ST7796S / SSD1309 / SH1106 / headless after flashing',
    note: 'For a plain ESP32 devkit wired to your own panel. Display type is chosen afterwards in the web portal, not at flash time.',
  },
  'esp32s3-4mb': {
    label: 'Generic ESP32-S3 (4 MB flash / 2 MB PSRAM)',
    chipFamily: 'ESP32-S3',
    slug: 'esp32s3-4mb',
    display: 'pick GC9A01 / ILI9488 / ST7796S / SSD1309 / SH1106 / headless after flashing',
    note: 'e.g. ESP32-S3-Zero. Bluetooth WiFi setup is left out to save flash — use Improv-over-serial below or the setup hotspot.',
  },
  'esp32s3-8mb': {
    label: 'Generic ESP32-S3 (8 MB flash / octal PSRAM)',
    chipFamily: 'ESP32-S3',
    slug: 'esp32s3-8mb',
    display: 'pick GC9A01 / ILI9488 / ST7796S / SSD1309 / SH1106 / headless after flashing',
    note: 'Includes Improv-over-Bluetooth as a second WiFi setup path alongside serial and the hotspot.',
  },
  esp32c3: {
    label: 'Generic ESP32-C3',
    chipFamily: 'ESP32-C3',
    slug: 'esp32c3',
    display: 'pick GC9A01 / SSD1309 / SH1106 / headless after flashing (no PSRAM — skip ILI9488/ST7796S)',
    note: 'No PSRAM: a 480×320 framebuffer is tight here. GC9A01 (round) and the mono OLEDs are verified on this chip.',
  },
};

const DEFAULT_BOARD = 'cyd';

let _version = null;
let _currentManifestUrl = null;

async function loadVersion() {
  const r = await fetch('firmware/latest/VERSION', { cache: 'no-cache' });
  if (!r.ok) throw new Error(`firmware/latest/VERSION returned HTTP ${r.status}`);
  const text = (await r.text()).trim();
  if (!text) throw new Error('VERSION file is empty');
  return text;
}

function buildManifest(boardId, version) {
  const board = BOARDS[boardId];
  const binUrl = new URL(`firmware/latest/esp32-hwmonitor-pio-${board.slug}.bin`, location.href).href;
  return {
    name: 'PulseMQTT',
    version,
    new_install_prompt_erase: true,
    // Improv-over-serial is only exposed by the firmware on a fresh flash
    // (hw_source defaults to MQTT, no stored WiFi credentials yet) — this
    // makes ESP Web Tools show its in-browser "Configure WiFi" step right
    // after install. The WiFiManager AP fallback stays available either way.
    new_install_improv_wait_time: 15,
    builds: [{
      chipFamily: board.chipFamily,
      parts: [{ path: binUrl, offset: 0 }],
    }],
  };
}

function manifestBlobUrl(boardId, version) {
  if (_currentManifestUrl) {
    URL.revokeObjectURL(_currentManifestUrl);
    _currentManifestUrl = null;
  }
  const blob = new Blob([JSON.stringify(buildManifest(boardId, version))], { type: 'application/json' });
  _currentManifestUrl = URL.createObjectURL(blob);
  return _currentManifestUrl;
}

function populateBoardSelect() {
  const sel = document.getElementById('board-select');
  for (const [id, info] of Object.entries(BOARDS)) {
    const opt = document.createElement('option');
    opt.value = id;
    opt.textContent = info.label;
    sel.appendChild(opt);
  }
  sel.value = DEFAULT_BOARD;
}

function renderSpecs(boardId) {
  const info = BOARDS[boardId];
  document.getElementById('spec-chip').textContent = info.chipFamily;
  document.getElementById('spec-display').textContent = info.display;
  document.getElementById('board-note-text').textContent = info.note;
}

function renderInstallButton(boardId, version) {
  // ESP Web Tools caches the manifest on first render — recreate the element
  // on every board switch so the new manifest is picked up.
  const slot = document.getElementById('install-slot');
  slot.innerHTML = '';
  const btn = document.createElement('esp-web-install-button');
  btn.setAttribute('manifest', manifestBlobUrl(boardId, version));

  const fallback = document.createElement('span');
  fallback.setAttribute('slot', 'unsupported');
  fallback.className = 'unsupported';
  fallback.textContent = 'Your browser does not support Web Serial. Use Chrome or Edge on desktop.';
  btn.appendChild(fallback);

  const notAllowed = document.createElement('span');
  notAllowed.setAttribute('slot', 'not-allowed');
  notAllowed.className = 'unsupported';
  notAllowed.textContent = 'Web Serial requires a secure context (HTTPS). Open this page from https://.';
  btn.appendChild(notAllowed);

  slot.appendChild(btn);
}

function showStatus(message, kind) {
  const line = document.getElementById('status-line');
  line.textContent = message || '';
  line.className = 'status-line' + (kind ? ' ' + kind : '');
}

function showVersion(version) {
  document.getElementById('spec-version').textContent = version;
  const rail = document.getElementById('rail-version');
  if (rail) rail.textContent = version;
}

function showVersionError(err) {
  document.getElementById('spec-version').textContent = 'unavailable';
  const rail = document.getElementById('rail-version');
  if (rail) rail.textContent = 'unavailable';
  showStatus(
    `Could not load firmware (${err.message}). Either the site is mid-deploy, or this release hasn't published binaries into firmware/latest/ yet — see the GitHub Releases page for manual .bin files in the meantime.`,
    'error',
  );
  document.getElementById('install-slot').innerHTML = '';
}

function checkBrowserSupport() {
  if (!('serial' in navigator)) {
    document.getElementById('browser-callout').classList.add('show');
  }
}

async function init() {
  checkBrowserSupport();
  populateBoardSelect();
  renderSpecs(DEFAULT_BOARD);
  wireMonitor();

  try {
    _version = await loadVersion();
  } catch (err) {
    showVersionError(err);
    return;
  }

  showVersion(_version);
  renderInstallButton(DEFAULT_BOARD, _version);

  document.getElementById('board-select').addEventListener('change', (e) => {
    const boardId = e.target.value;
    renderSpecs(boardId);
    renderInstallButton(boardId, _version);
  });
}

// ────────── serial monitor ──────────
// Reads the device's USB CDC stream at 115200 baud and appends decoded text
// to <pre id="monitor-output">. Independent of the install button — only one
// program can hold the port at a time, so don't click Install while connected.

let _monitorPort = null;
let _monitorReader = null;
let _monitorReadLoopRunning = false;

async function monitorConnect() {
  if (_monitorPort) return;
  let port;
  try {
    port = await navigator.serial.requestPort();
  } catch (err) {
    if (err && err.name === 'NotFoundError') return; // user cancelled picker
    setMonitorStatus(`Could not pick a port: ${err.message}`, 'error');
    return;
  }
  try {
    await port.open({ baudRate: 115200 });
  } catch (err) {
    setMonitorStatus(`Could not open the port: ${err.message}. Close other monitors and try again.`, 'error');
    return;
  }
  _monitorPort = port;
  toggleMonitorButtons(true);
  setMonitorStatus('Connected. Reading from device…', 'ok');
  monitorReadLoop().catch((err) => setMonitorStatus(`Read error: ${err.message}`, 'error'));
}

async function monitorDisconnect() {
  if (!_monitorPort) return;
  setMonitorStatus('Disconnecting…');
  try { if (_monitorReader) await _monitorReader.cancel(); } catch (_) {}
  const startedAt = Date.now();
  while (_monitorReadLoopRunning && Date.now() - startedAt < 1000) {
    await new Promise((r) => setTimeout(r, 20));
  }
  try { await _monitorPort.close(); } catch (_) {}
  _monitorPort = null;
  _monitorReader = null;
  toggleMonitorButtons(false);
  setMonitorStatus('Disconnected.');
}

async function monitorReadLoop() {
  _monitorReadLoopRunning = true;
  const decoder = new TextDecoder();
  try {
    if (!_monitorPort || !_monitorPort.readable) return;
    const reader = _monitorPort.readable.getReader();
    _monitorReader = reader;
    try {
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value && value.byteLength) appendMonitorOutput(decoder.decode(value, { stream: true }));
      }
    } finally {
      try { reader.releaseLock(); } catch (_) {}
      _monitorReader = null;
    }
  } finally {
    _monitorReadLoopRunning = false;
  }
}

function appendMonitorOutput(text) {
  const out = document.getElementById('monitor-output');
  const wasEmpty = out.textContent.length === 0;
  const atBottom = out.scrollHeight - out.clientHeight - out.scrollTop < 4;
  out.appendChild(document.createTextNode(text));
  if (out.textContent.length > 200000) out.textContent = out.textContent.slice(-150000);
  if (atBottom) out.scrollTop = out.scrollHeight;
  if (wasEmpty) setMonitorBufferButtons(true);
}

function monitorExport() {
  const out = document.getElementById('monitor-output');
  const text = out.textContent;
  if (!text) return;
  const ts = new Date().toISOString().replace(/[:.]/g, '-').replace('Z', '');
  const blob = new Blob([text], { type: 'text/plain;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `pulsemqtt-serial-${ts}.txt`;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function monitorClear() {
  document.getElementById('monitor-output').textContent = '';
  setMonitorBufferButtons(false);
}

function setMonitorBufferButtons(hasContent) {
  document.getElementById('monitor-export').disabled = !hasContent;
  document.getElementById('monitor-clear').disabled = !hasContent;
}

function setMonitorStatus(message, kind) {
  const line = document.getElementById('monitor-status');
  line.textContent = message || '';
  line.className = 'status-line' + (kind ? ' ' + kind : '');
}

function toggleMonitorButtons(connected) {
  document.getElementById('monitor-connect').disabled = connected;
  document.getElementById('monitor-disconnect').disabled = !connected;
}

function wireMonitor() {
  const connectBtn = document.getElementById('monitor-connect');
  if (!('serial' in navigator)) {
    connectBtn.disabled = true;
    setMonitorStatus('Web Serial is unavailable in this browser — use desktop Chrome or Edge.', 'warn');
    return;
  }
  connectBtn.addEventListener('click', monitorConnect);
  document.getElementById('monitor-disconnect').addEventListener('click', monitorDisconnect);
  document.getElementById('monitor-export').addEventListener('click', monitorExport);
  document.getElementById('monitor-clear').addEventListener('click', monitorClear);
}

// ────────── copy buttons on code blocks ──────────

function wireCopyButtons() {
  document.querySelectorAll('[data-cmd]').forEach((block) => {
    const btn = block.querySelector('.cmd-copy');
    const pre = block.querySelector('pre');
    if (!btn || !pre) return;
    btn.addEventListener('click', async () => {
      try { await navigator.clipboard.writeText(pre.textContent); } catch (_) {}
      btn.textContent = 'copied';
      btn.classList.add('copied');
      setTimeout(() => { btn.textContent = 'copy'; btn.classList.remove('copied'); }, 1400);
    });
  });
}

wireCopyButtons();
init();

/* ============================================================
   BLAME ENGINE — Web Interactive Systems
   ============================================================ */
'use strict';

const REDUCED = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

// ============================================================ BOOT SEQUENCE LOGIC
const bootConsole = document.getElementById('bootConsole');
const bootBar = document.getElementById('bootBar');
const bootScreen = document.getElementById('bootScreen');
const bootSkip = document.getElementById('bootSkip');

const bootLogs = [
  "[SYS.BOOT] Initializing BLAME ENGINE core architecture...",
  "[SYS.BOOT] Auditing workspace... 69 source files, 96 headers found.",
  "[SYS.BOOT] Loading Platform Layer (L0)... OK. POSIX process handles loaded.",
  "[SYS.BOOT] Bootstrapping JobSystem (L1)... Active: 8 CPU worker threads.",
  "[SYS.BOOT] Spawning Runtime Core (L2)... ECS archetype registry coupled.",
  "[SYS.BOOT] Instantiating terminal process... subprocess 'nvim --embed' attached.",
  "[SYS.BOOT] Initializing msgpack-rpc handshake protocol... OK.",
  "[SYS.BOOT] Querying graphics capabilities (L3)... Metal API detected (Argument Buffers Tier 1).",
  "[SYS.BOOT] Creating Render Graph DAG structures... OK.",
  "[SYS.BOOT] Loading World Partition index... 144 terrain cells registered.",
  "[SYS.BOOT] System metrics online. Integrity check passed (100% tests OK).",
  "[SYS.BOOT] Coupled console graphical HUD. ACCESS GRANTED."
];

let bootIndex = 0;
let bootProgress = 0;

function runBoot() {
  if (REDUCED) {
    bootScreen.classList.add('is-loaded');
    return;
  }
  
  const printNextLog = () => {
    if (bootIndex < bootLogs.length) {
      const line = document.createElement('div');
      line.textContent = bootLogs[bootIndex];
      bootConsole.appendChild(line);
      bootConsole.scrollTop = bootConsole.scrollHeight;
      bootIndex++;
      
      bootProgress = (bootIndex / bootLogs.length) * 100;
      bootBar.style.width = bootProgress + '%';
      
      const delay = 80 + Math.random() * 150;
      setTimeout(printNextLog, delay);
    } else {
      setTimeout(() => {
        bootScreen.classList.add('is-loaded');
      }, 400);
    }
  };
  
  setTimeout(printNextLog, 200);
}

if (bootSkip) {
  bootSkip.addEventListener('click', () => {
    bootScreen.classList.add('is-loaded');
  });
}

// Run boot sequence
runBoot();


// ============================================================ SCROLL PROGRESS & NAV HIGH-LIGHTER
const railBar = document.getElementById('railBar');
const nav = document.getElementById('nav');
const secLinks = [...document.querySelectorAll('.nav__links a[data-sec]')];
const sections = secLinks.map(a => document.querySelector(a.getAttribute('href'))).filter(Boolean);

let sectionTops = [];
function measure() { sectionTops = sections.map(s => s.offsetTop); }

function updateScroll() {
  const h = document.documentElement;
  const max = h.scrollHeight - h.clientHeight;
  const y = window.scrollY;
  if (railBar) railBar.style.width = (max > 0 ? (y / max) * 100 : 0).toFixed(2) + '%';
  if (nav) nav.classList.toggle('is-stuck', y > 40);
  
  const mid = y + window.innerHeight * 0.32;
  let active = -1;
  for (let i = 0; i < sectionTops.length; i++) if (sectionTops[i] <= mid) active = i;
  for (let i = 0; i < secLinks.length; i++) secLinks[i].classList.toggle('is-active', i === active);
}

let scrollTick = false;
window.addEventListener('scroll', () => {
  if (!scrollTick) { scrollTick = true; requestAnimationFrame(() => { updateScroll(); scrollTick = false; }); }
}, { passive: true });
window.addEventListener('resize', measure, { passive: true });
window.addEventListener('load', measure);
measure();
updateScroll();


// ============================================================ REVEAL ON ENTER
const io = new IntersectionObserver((entries) => {
  for (const e of entries) if (e.isIntersecting) { e.target.classList.add('is-in'); io.unobserve(e.target); }
}, { threshold: 0.1, rootMargin: '0px 0px -5% 0px' });
document.querySelectorAll('.reveal').forEach(el => io.observe(el));


// ============================================================ COUNT-UP STATS
function countUp(el) {
  const target = parseInt(el.dataset.count, 10);
  if (REDUCED || target <= 0) { el.textContent = target.toLocaleString(); return; }
  const dur = 1500, t0 = performance.now();
  function step(t) {
    const k = Math.min(1, (t - t0) / dur);
    el.textContent = Math.round(target * (1 - Math.pow(1 - k, 3))).toLocaleString();
    if (k < 1) requestAnimationFrame(step); else el.textContent = target.toLocaleString();
  }
  requestAnimationFrame(step);
}
const statsIO = new IntersectionObserver((entries) => {
  for (const e of entries) if (e.isIntersecting) { e.target.querySelectorAll('b[data-count]').forEach(countUp); statsIO.unobserve(e.target); }
}, { threshold: 0.4 });
const heroStats = document.getElementById('heroStats');
if (heroStats) statsIO.observe(heroStats);


// ============================================================ CHART TRIGGER
const chart = document.getElementById('chart');
if (chart) {
  const cio = new IntersectionObserver((e) => { if (e[0].isIntersecting) { chart.classList.add('is-in'); cio.disconnect(); } }, { threshold: 0.2 });
  cio.observe(chart);
}


// ============================================================ §01 LOOP STEPPER
(function loopStepper() {
  const wrap = document.getElementById('loopSteps');
  if (!wrap || REDUCED) return;
  const steps = [...wrap.querySelectorAll('.loop__step')];
  let cur = 0, timer = null;
  const tick = () => { steps.forEach((s, i) => s.classList.toggle('is-hot', i === cur)); cur = (cur + 1) % steps.length; };
  const lio = new IntersectionObserver((e) => {
    if (e[0].isIntersecting) { if (!timer) { tick(); timer = setInterval(tick, 1400); } }
    else if (timer) { clearInterval(timer); timer = null; }
  }, { threshold: 0.25 });
  lio.observe(wrap);
})();


// ============================================================ INTERACTIVE SYSTEMS DECK PANEL
const deckBtns = document.querySelectorAll('.deck__btn');
const deckPanels = document.querySelectorAll('.deck__panel');
const deckStatus = document.getElementById('deckStatus');

const statusMsgs = {
  nvim: "NVIM TERMINAL BUFFER STREAM ACTIVE",
  render: "RENDER GRAPH EXECUTION FLOW IN VIEW",
  ecs: "ECS DB QUERY & EVENT LOG MONITORING",
  world: "WORLD partitioning CELL IO STATS ACTIVE"
};

deckBtns.forEach(btn => {
  btn.addEventListener('click', () => {
    const tabName = btn.dataset.tab;
    
    deckBtns.forEach(b => b.classList.remove('is-active'));
    deckPanels.forEach(p => p.classList.remove('is-active'));
    
    btn.classList.add('is-active');
    const targetPanel = document.getElementById('pane-' + tabName);
    if (targetPanel) targetPanel.classList.add('is-active');
    
    if (deckStatus && statusMsgs[tabName]) {
      deckStatus.textContent = statusMsgs[tabName];
    }
  });
});

// Set default status message
if (deckStatus) deckStatus.textContent = statusMsgs.nvim;


// ============================================================ VIZ 1: NVIM TEXT MATRIX SIMULATOR
const nvimMatrix = document.getElementById('nvimMatrix');
const nvimLog = document.getElementById('nvimLog');

const demoCode = [
  "def score_route(route):",
  "    time = route[\"travel_time\"]",
  "    risk = route[\"camera_risk\"]",
  "    ",
  "    # Check camera safety window",
  "    if risk > 4:",
  "        return -1 # Bypass route",
  "    ",
  "    # Compute score weights",
  "    weight = time * 1.5",
  "    return 100 - weight",
  "",
  "# Route selection triggered",
  "r = {\"travel_time\": 12, \"camera_risk\": 2}",
  "print(score_route(r))"
];

let nvimGrid = Array.from({ length: 16 }, () => Array(40).fill(' '));
let charIndex = 0;
let codeLine = 0;
let cursorX = 0, cursorY = 0;

// Setup grid items
function buildNvimGrid() {
  if (!nvimMatrix) return;
  nvimMatrix.innerHTML = '';
  for (let r = 0; r < 16; r++) {
    const rowEl = document.createElement('div');
    rowEl.className = 'viz-nvim__row';
    for (let c = 0; c < 40; c++) {
      const cellEl = document.createElement('span');
      cellEl.className = 'viz-nvim__cell';
      cellEl.id = `nc-${r}-${c}`;
      cellEl.textContent = ' ';
      rowEl.appendChild(cellEl);
    }
    nvimMatrix.appendChild(rowEl);
  }
}
buildNvimGrid();

function updateNvimCell(r, c, char, styleClass = '') {
  const el = document.getElementById(`nc-${r}-${c}`);
  if (el) {
    el.textContent = char;
    el.className = 'viz-nvim__cell ' + styleClass;
  }
}

function pushNvimLog(msg) {
  if (!nvimLog) return;
  const line = document.createElement('div');
  line.className = 'viz-nvim__log-row';
  line.textContent = msg;
  nvimLog.appendChild(line);
  if (nvimLog.children.length > 15) nvimLog.removeChild(nvimLog.children[1]); // keep header
  nvimLog.scrollTop = nvimLog.scrollHeight;
}

function tickNvim() {
  if (document.getElementById('pane-nvim').classList.contains('is-active')) {
    if (codeLine >= demoCode.length) {
      // Clear Matrix
      codeLine = 0;
      charIndex = 0;
      cursorX = 0;
      cursorY = 0;
      for (let r = 0; r < 16; r++) {
        for (let c = 0; c < 40; c++) {
          updateNvimCell(r, c, ' ');
        }
      }
      pushNvimLog("rpc: grid_clear(0)");
      return;
    }
    
    const lineStr = demoCode[codeLine];
    if (charIndex < lineStr.length) {
      const char = lineStr[charIndex];
      let styleClass = '';
      
      // Basic syntax styling
      if (lineStr.trim().startsWith('#')) styleClass = 'hl-comment';
      else if (lineStr.indexOf('def ') >= 0 && charIndex < 4) styleClass = 'hl-keyword';
      else if (lineStr.indexOf('if ') >= 0 && charIndex < lineStr.indexOf('if ') + 3) styleClass = 'hl-keyword';
      else if (lineStr.indexOf('return') >= 0) styleClass = 'hl-keyword';
      else if (char === '"' || styleClass === 'hl-string') styleClass = 'hl-string';
      
      updateNvimCell(cursorY, cursorX, char, styleClass);
      
      cursorX++;
      charIndex++;
      
      // Cursor highlight
      const curEl = document.getElementById(`nc-${cursorY}-${cursorX}`);
      if (curEl) curEl.classList.add('hl-cursor');
      const prevEl = document.getElementById(`nc-${cursorY}-${cursorX-1}`);
      if (prevEl) prevEl.classList.remove('hl-cursor');
      
      if (Math.random() > 0.6) {
        pushNvimLog(`rpc: grid_line(0, row=${cursorY}, col=${cursorX-1}, '${char}')`);
      }
    } else {
      // Carriage return
      const prevEl = document.getElementById(`nc-${cursorY}-${cursorX}`);
      if (prevEl) prevEl.classList.remove('hl-cursor');
      cursorY++;
      cursorX = 0;
      charIndex = 0;
      codeLine++;
      pushNvimLog(`rpc: grid_cursor_goto(0, row=${cursorY}, col=0)`);
    }
  }
}
setInterval(tickNvim, 100);


// ============================================================ VIZ 2: RENDER GRAPH SELECTOR
const renderGraphNodes = document.getElementById('renderGraphNodes');
const renderGraphDesc = document.getElementById('renderGraphDesc');

const passData = {
  depth: {
    title: "DepthPass (Selected)",
    desc: "写入场景深度缓冲（Z-Buffer）以建立管线的遮挡排序。输出: GBuffer Depth Texture. 格式: DXGI_FORMAT_R32_TYPELESS. 作用: 在不进行昂贵着色计算前，通过 early-Z 剔除被遮挡的像素片段。"
  },
  voxel: {
    title: "Voxelization (Selected)",
    desc: "体素化着色 Pass。将世界静态模型和地形网格的三维几何结构和漫反射属性光栅化到 3D 纹理网格中。输出: Voxel Density Grid. 依赖项: DepthPass / Transform Data."
  },
  gi: {
    title: "GI Cone Trace (Selected)",
    desc: "体素锥追踪全局光照。渲染线程发射锥体光线穿越 Voxel 3D 贴图，收集多次间接反射的辐射度。输出: Radiance Illumination Grid. 作用: 为场景渲染高品质的动态漫反射和粗糙镜面反射间接光。"
  },
  gtao: {
    title: "GTAO Pass (Selected)",
    desc: "地平线环境光遮蔽（Ground-Truth Ambient Occlusion）。计算局部微细遮挡，包含时域滤波器（Temporal Filter）用于去噪。输出: Ambient Occlusion Map. 作用: 增强转角、接缝处的真实接触阴影感。"
  },
  taa: {
    title: "Temporal AA (Selected)",
    desc: "时域抗锯齿与后期管线。包含 Halton 抖动投影混合、历史帧矫正以及 Bloom/ToneMapping 后期处理。输出: Main Backbuffer. 作用: 去除锯齿、高频闪烁并实现平滑稳定的高动态画面输出。"
  }
};

if (renderGraphNodes) {
  const nodesList = [...renderGraphNodes.querySelectorAll('.viz-render__node')];
  nodesList.forEach(node => {
    node.addEventListener('click', () => {
      nodesList.forEach(n => n.classList.remove('is-active'));
      node.classList.add('is-active');
      const passName = node.dataset.pass;
      if (renderGraphDesc && passData[passName]) {
        renderGraphDesc.innerHTML = `
          <div class="viz-render__desc-header">${passData[passName].title}</div>
          <p>${passData[passName].desc}</p>
        `;
      }
    });
  });
}


// ============================================================ VIZ 3: ECS WORLD LOGIC INSPECTOR
const ecsList = document.getElementById('ecsList');
const ecsInspector = document.getElementById('ecsInspector');
const ecsComponents = document.getElementById('ecsComponents');

const mockEntities = [
  {
    id: "Entity: #101 (Player)",
    tag: "song_demo::Player",
    components: [
      { name: "TransformComponent", val: "Pos: {14.2, -8.0, 102.5}\nRot: {0.0, 45.0, 0.0}" },
      { name: "PlayerStateComponent", val: "UplinkID: 104\nIsHacking: true\nCredits: 4200" },
      { name: "CameraLink", val: "AttachedCameraID: 102\nFOV: 75.0" }
    ]
  },
  {
    id: "Entity: #102 (Camera)",
    tag: "SecurityCamera.03",
    components: [
      { name: "TransformComponent", val: "Pos: {18.5, 4.2, 98.1}\nRot: {-20.0, 10.0, 0.0}" },
      { name: "RiskWindowComponent", val: "Risks: {camera_risk: 2}\nAngleLimit: 90.0" },
      { name: "UplinkComponent", val: "ConnectedUplinkNode: 104\nNetworkStatus: COMPROMISED" }
    ]
  },
  {
    id: "Entity: #103 (Trigger)",
    tag: "WorldTriggerVolume",
    components: [
      { name: "AABBVolumeComponent", val: "Min: {10.0, -10.0, 90.0}\nMax: {20.0, 10.0, 110.0}" },
      { name: "EventTriggerComponent", val: "OnEnterEvent: UPLINK_HANDSHAKE\nOnExitEvent: NULL" }
    ]
  },
  {
    id: "Entity: #104 (Uplink)",
    tag: "TacticalUplinkNode",
    components: [
      { name: "NetworkSurface", val: "PolicyFile: 'sample_policy.py'\nBufferTextSize: 218 bytes" },
      { name: "ScoreRouter", val: "TargetEntityID: 102\nActiveScore: 28\nWorldState: REVIEW" }
    ]
  }
];

function buildEcsList() {
  if (!ecsList) return;
  ecsList.innerHTML = '';
  mockEntities.forEach((ent, idx) => {
    const row = document.createElement('div');
    row.className = 'viz-ecs__row' + (idx === 0 ? ' is-active' : '');
    row.innerHTML = `
      <span class="viz-ecs__entity-id">${ent.tag}</span>
      <span class="viz-ecs__entity-tag">#${ent.id.split('#')[1].split(' ')[0]}</span>
    `;
    row.addEventListener('click', () => {
      const rows = ecsList.querySelectorAll('.viz-ecs__row');
      rows.forEach(r => r.classList.remove('is-active'));
      row.classList.add('is-active');
      inspectEntity(ent);
    });
    ecsList.appendChild(row);
  });
  // Inspect first default
  inspectEntity(mockEntities[0]);
}

function inspectEntity(ent) {
  if (!ecsComponents || !ecsInspector) return;
  const title = ecsInspector.querySelector('.viz-ecs__inspector-title');
  title.textContent = `Inspect: ${ent.id}`;
  
  ecsComponents.innerHTML = '';
  ent.components.forEach(comp => {
    const compEl = document.createElement('div');
    compEl.className = 'viz-ecs__component';
    compEl.innerHTML = `
      <div class="viz-ecs__comp-title">${comp.name}</div>
      <div class="viz-ecs__comp-data">${comp.val.replace(/\n/g, '<br>')}</div>
    `;
    ecsComponents.appendChild(compEl);
  });
}
buildEcsList();


// ============================================================ VIZ 4: WORLD PARTITION STREAMING MAP
const worldMap = document.getElementById('worldMap');
const worldMem = document.getElementById('worldMem');
const worldTasks = document.getElementById('worldTasks');
const worldEvictions = document.getElementById('worldEvictions');

const GRID_SIZE = 12;
let cells = [];
let cameraX = 6, cameraY = 6;
let targetCamX = 6, targetCamY = 6;
let evictionCount = 14;

function buildWorldMap() {
  if (!worldMap) return;
  worldMap.innerHTML = '';
  cells = [];
  for (let r = 0; r < GRID_SIZE; r++) {
    for (let c = 0; c < GRID_SIZE; c++) {
      const cell = document.createElement('div');
      cell.className = 'viz-world__cell cell--unloaded';
      cell.dataset.r = r;
      cell.dataset.c = c;
      worldMap.appendChild(cell);
      cells.push({ r, c, el: cell, state: 'unloaded', t: 0 });
    }
  }
}
buildWorldMap();

// Track mouse position over worldMap to simulate camera movements
if (worldMap) {
  worldMap.addEventListener('mousemove', (e) => {
    const rect = worldMap.getBoundingClientRect();
    const x = (e.clientX - rect.left) / rect.width;
    const y = (e.clientY - rect.top) / rect.height;
    targetCamX = x * GRID_SIZE;
    targetCamY = y * GRID_SIZE;
  });
}

function updateStreaming() {
  if (!document.getElementById('pane-world').classList.contains('is-active')) return;
  
  // Interpolate camera position
  cameraX += (targetCamX - cameraX) * 0.1;
  cameraY += (targetCamY - cameraY) * 0.1;
  
  let activeIO = 0;
  let loadedMemory = 0;
  
  cells.forEach(cell => {
    const dx = cell.c - cameraX;
    const dy = cell.r - cameraY;
    const dist = Math.sqrt(dx * dx + dy * dy);
    
    let targetState = 'unloaded';
    if (dist < 1.8) targetState = 'loaded';
    else if (dist < 2.8) targetState = 'uploading';
    else if (dist < 3.8) targetState = 'decompressing';
    else if (dist < 4.8) targetState = 'loading';
    else if (dist < 5.8) targetState = 'queued';
    
    // Animate state changes
    if (cell.state !== targetState) {
      if (cell.state === 'loaded' && targetState === 'unloaded') {
        evictionCount++;
        if (worldEvictions) worldEvictions.textContent = evictionCount;
      }
      cell.state = targetState;
      cell.el.className = `viz-world__cell cell--${targetState}`;
    }
    
    // Accumulate metrics
    if (cell.state === 'loaded') {
      loadedMemory += 3.2; // MB per cell
    } else if (cell.state === 'loading' || cell.state === 'decompressing' || cell.state === 'uploading') {
      activeIO++;
      loadedMemory += 1.1;
    }
  });
  
  if (worldMem) {
    worldMem.textContent = (180 + loadedMemory).toFixed(1) + ' MB';
  }
  if (worldTasks) {
    worldTasks.textContent = activeIO;
  }
}
setInterval(updateStreaming, 80);


// ============================================================ §07 INTERACTIVE CLI SIMULATOR TERMINAL
const cliOutput = document.getElementById('cliOutput');
const cmdBuild = document.getElementById('cmdBuild');
const cmdRun = document.getElementById('cmdRun');
const cmdVerify = document.getElementById('cmdVerify');
const cmdCtest = document.getElementById('cmdCtest');

const terminalLogs = {
  build: [
    "-- Preset terminal-dev: Generating makefiles...",
    "-- RHI backend config: DX12: [OFF] | Metal: [ON]",
    "-- Found Neovim binary: /usr/local/bin/nvim (v0.10.0)",
    "-- Configuring done",
    "-- Generating done",
    "-- Build files written to: out/build/terminal-dev",
    "[  8%] Compiling CXX platform/mac_window.cpp",
    "[ 16%] Compiling CXX foundation/jobsystem.cpp",
    "[ 25%] Compiling CXX runtime/ecs.cpp",
    "[ 33%] Compiling CXX terminal/nvim_surface.cpp",
    "[ 41%] Linking static library libengine_terminal.a",
    "[ 50%] Compiling CXX rhi/metal_backend.cpp",
    "[ 66%] Compiling CXX renderer/render_graph.cpp",
    "[ 75%] Compiling CXX renderer/global_illumination.cpp",
    "[ 83%] Compiling CXX world/streaming_manager.cpp",
    "[ 91%] Linking static library libengine_world.a",
    "[ 95%] Linking executable next_nvim_surface_probe",
    "[100%] Linking executable hackops_demo",
    "Build Succeeded. [100%] compilation completed. Core symbols verified."
  ],
  run: [
    "HackOps demo initializing...",
    "Subprocess nvim spawned (pid=19803).",
    "Uplink handshake: attached external UI (40x16 cell text matrix).",
    "Loading policy tools/nvim_surface_probe/sample_policy.py ... OK.",
    "Running policy script interpreter (python3)...",
    "[Interpreter Output]: Score calculated: 28",
    "Mapping score to world database...",
    "RouteStateForScore: 28 -> state: REVIEW",
    "Updating ECS uplink router: Entity[#104] status updated to 1.",
    "Writing viewport text snapshot to /tmp/hackops-snapshot.txt... OK.",
    "Uplink closed (Neovim subprocess detached).",
    "HACKOPS_DEMO_SUCCESS"
  ],
  verify: [
    "Checking Windows environment constraints...",
    "Windows SDK Version: 10.0.22621.0 - OK.",
    "Compiling Shader compiler (dxc.exe / dxcompiler.dll) paths... OK.",
    "Compiling HLSL targets (SM6.6, SM6.5)...",
    "[Shader] mesh_debug.ms.hlsl compiled (SM6.5 mesh) -> OK.",
    "[Shader] ddgi.hlsl compiled (SM6.6 compute) -> OK.",
    "[Shader] rtgi.raytracing.hlsl compiled (SM6.3 raytracing) -> OK.",
    "[Shader] sampler_feedback.hlsl compiled -> OK.",
    "Running DX12U runtime smoke verification on DX12 GPU...",
    "Spawning: song_demo.exe --smoke-frames 240",
    "[RHI] Backend DX12 initialized. DX12U dynamic features active.",
    "[Renderer] PSO cache compiled: 142 pipeline states.",
    "[Renderer] MeshShader debug check: active.",
    "[Renderer] SamplerFeedback mapping: active.",
    "Executing frames [0-240]...",
    "[Frame 60] Present interval: 16.67ms (60 fps) | Memory: 412 MB",
    "[Frame 120] Present interval: 16.59ms (60 fps) | Memory: 412 MB",
    "[Frame 180] Present interval: 16.63ms (60 fps) | Memory: 413 MB",
    "[Frame 240] Smoke run completed. 0 GPU page faults detected.",
    "Verification Success: DX12U/SM6.6 pipelines are stable."
  ],
  ctest: [
    "Internal test suite runner v3.22.1",
    "Test project /Users/Apple/watch-dogs-realcode-proposal/next-engine/out/build/terminal-dev",
    "    Start  1: foundation.jobsystem",
    "1/22 Test  #1: foundation.jobsystem .............   Passed    0.04 sec",
    "    Start  2: foundation.memory_pool",
    "2/22 Test  #2: foundation.memory_pool ...........   Passed    0.01 sec",
    "    Start  3: runtime.ecs_archetype",
    "3/22 Test  #3: runtime.ecs_archetype ...........   Passed    0.05 sec",
    "    Start  4: runtime.event_bus",
    "4/22 Test  #4: runtime.event_bus ...............   Passed    0.02 sec",
    "    Start  5: serialization.schema",
    "5/22 Test  #5: serialization.schema ............   Passed    0.03 sec",
    "    Start  6: terminal.msgpack_encoder",
    "6/22 Test  #6: terminal.msgpack_encoder ........   Passed    0.01 sec",
    "    Start  7: terminal.nvim_bridge",
    "7/22 Test  #7: terminal.nvim_bridge ............   Passed    0.08 sec",
    "... [14 tests omitted for brevity] ...",
    "    Start 22: game.policy_bridge",
    "22/22 Test #22: game.policy_bridge .............   Passed    0.06 sec",
    "",
    "100% tests passed, 0 tests failed out of 22.",
    "Total Test time (real) =   0.42 sec"
  ]
};

const cmdPrompts = {
  build: "cmake --preset terminal-dev && cmake --build --preset terminal-dev",
  run: "out/build/terminal-dev/bin/hackops_demo --policy tools/nvim_surface_probe/sample_policy.py --snapshot /tmp/hackops-snapshot.txt",
  verify: ".\\scripts\\verify_dx12u.ps1",
  ctest: "ctest --test-dir out/build/terminal-dev -C Debug --output-on-failure"
};

let typingTimer = null;
let logTimer = null;

function runTerminalCmd(cmdKey) {
  if (!cliOutput) return;
  
  // Clear previous runs
  if (typingTimer) clearTimeout(typingTimer);
  if (logTimer) clearInterval(logTimer);
  
  cliOutput.innerHTML = '';
  
  const prompt = cmdPrompts[cmdKey];
  const logs = terminalLogs[cmdKey];
  
  let i = 0;
  
  const typeCommand = () => {
    if (i < prompt.length) {
      cliOutput.innerHTML = `<span class="t-c">$ </span><span class="t-cmd">${prompt.substring(0, i + 1)}█</span>`;
      i++;
      typingTimer = setTimeout(typeCommand, 15 + Math.random() * 25);
    } else {
      // Done typing command, start printing logs
      cliOutput.innerHTML = `<span class="t-c">$ </span><span class="t-cmd">${prompt}</span>`;
      let logLine = 0;
      
      logTimer = setInterval(() => {
        if (logLine < logs.length) {
          const newLine = document.createElement('div');
          
          // Style outputs
          let text = logs[logLine];
          if (text.startsWith("Build Succeeded") || text.includes("Success") || text.includes("100% tests passed") || text.includes("Passed")) {
            newLine.className = 't-ok';
          } else if (text.startsWith("[Shader]") || text.startsWith("    Start") || text.startsWith("-- ")) {
            newLine.className = 't-d';
          } else if (text.includes("Error") || text.includes("failed")) {
            newLine.className = 't-run'; // use amber alert
          }
          
          newLine.textContent = text;
          cliOutput.appendChild(newLine);
          cliOutput.scrollTop = cliOutput.scrollHeight;
          logLine++;
        } else {
          clearInterval(logTimer);
          const endPrompt = document.createElement('div');
          endPrompt.innerHTML = `<span class="t-c">$ </span><span class="t-cmd">_</span>`;
          cliOutput.appendChild(endPrompt);
          cliOutput.scrollTop = cliOutput.scrollHeight;
        }
      }, 80 + Math.random() * 60);
    }
  };
  
  typeCommand();
}

if (cmdBuild) cmdBuild.addEventListener('click', () => runTerminalCmd('build'));
if (cmdRun) cmdRun.addEventListener('click', () => runTerminalCmd('run'));
if (cmdVerify) cmdVerify.addEventListener('click', () => runTerminalCmd('verify'));
if (cmdCtest) cmdCtest.addEventListener('click', () => runTerminalCmd('ctest'));


// ============================================================ NETWORK BREACH GRAPH (HERO CANVAS)
(function network() {
  const canvas = document.getElementById('netcanvas');
  if (!canvas) return;
  const ctx = canvas.getContext('2d', { alpha: true });

  const COLS = 7, ROWS = 6, N = COLS * ROWS;
  const nodes = [];
  for (let r = 0; r < ROWS; r++) for (let c = 0; c < COLS; c++) {
    const i = r * COLS + c;
    const hash = (Math.sin(i * 12.9898) * 43758.5453) % 1;
    nodes.push({ col: c, row: r, jx: Math.sin(i * 7.13) , jy: Math.cos(i * 3.71),
                 type: (hash + 1) % 1 < 0.3 ? 1 : 0, t: 0 });
  }
  const id = (c, r) => r * COLS + c;
  const edges = [];
  const adj = Array.from({ length: N }, () => []);
  function addEdge(a, b) { edges.push([a, b]); adj[a].push(b); adj[b].push(a); }
  for (let r = 0; r < ROWS; r++) for (let c = 0; c < COLS; c++) {
    const i = id(c, r);
    if (c + 1 < COLS) addEdge(i, id(c + 1, r));
    if (r + 1 < ROWS) addEdge(i, id(c, r + 1));
    if (c + 1 < COLS && r + 1 < ROWS && ((c + r) % 2 === 0)) addEdge(i, id(c + 1, r + 1));
  }
  const flow = new Float32Array(edges.length);

  let DPR = 1, W = 0, H = 0, cellW = 0, cellH = 0;
  const px = new Float64Array(N), py = new Float64Array(N);
  let mouseX = 0, mouseY = 0, targetMX = 0, targetMY = 0;
  function layout() {
    DPR = Math.min(1.75, window.devicePixelRatio || 1);
    W = canvas.clientWidth; H = canvas.clientHeight;
    canvas.width = Math.round(W * DPR); canvas.height = Math.round(H * DPR);
    ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
    const mx = W * 0.10, my = H * 0.12, uw = W - mx * 2, uh = H - my * 2;
    cellW = uw / (COLS - 1); cellH = uh / (ROWS - 1);
    for (let i = 0; i < N; i++) {
      px[i] = mx + nodes[i].col * cellW + nodes[i].jx * cellW * 0.28;
      py[i] = my + nodes[i].row * cellH + nodes[i].jy * cellH * 0.28;
    }
  }
  layout();
  window.addEventListener('resize', layout, { passive: true });
  window.addEventListener('mousemove', (e) => {
    targetMX = (e.clientX / window.innerWidth - 0.5);
    targetMY = (e.clientY / window.innerHeight - 0.5);
  }, { passive: true });

  const dist = new Int32Array(N);
  let waveMax = 0, wave = 0, phase = 'breach';
  function chooseEntry() {
    const entry = (Math.random() * N) | 0;
    dist.fill(9999); dist[entry] = 0;
    const q = [entry];
    while (q.length) { const n = q.shift(); for (const m of adj[n]) if (dist[m] > dist[n] + 1) { dist[m] = dist[n] + 1; q.push(m); } }
    waveMax = 0; for (let i = 0; i < N; i++) if (dist[i] < 9000 && dist[i] > waveMax) waveMax = dist[i];
    wave = 0; phase = 'breach';
  }
  chooseEntry();

  function update(dt) {
    mouseX += (targetMX - mouseX) * 0.04;
    mouseY += (targetMY - mouseY) * 0.04;
    if (phase === 'breach') { wave += dt * 1.7; if (wave > waveMax + 1.6) phase = 'fade'; }
    let alive = 0;
    for (let i = 0; i < N; i++) {
      const tgt = (phase === 'fade') ? 0 : (wave >= dist[i] ? 1 : 0);
      nodes[i].t += (tgt - nodes[i].t) * Math.min(1, dt * 3.2);
      if (nodes[i].t > 0.02) alive++;
    }
    if (phase === 'fade' && alive === 0) chooseEntry();
    for (let e = 0; e < edges.length; e++) {
      const m = Math.min(nodes[edges[e][0]].t, nodes[edges[e][1]].t);
      if (m > 0.15) { flow[e] += dt * 0.8; if (flow[e] > 1) flow[e] -= 1; } else flow[e] = 0;
    }
  }

  const C_DIM = [74, 73, 68], C_CY = [0, 255, 210], C_BONE = [236, 233, 225];
  function colAt(t) {
    if (t < 0.5) { const k = t / 0.5; return [C_DIM[0]+(C_CY[0]-C_DIM[0])*k, C_DIM[1]+(C_CY[1]-C_DIM[1])*k, C_DIM[2]+(C_CY[2]-C_DIM[2])*k]; }
    const k = (t - 0.5) / 0.5; return [C_CY[0]+(C_BONE[0]-C_CY[0])*k, C_CY[1]+(C_BONE[1]-C_CY[1])*k, C_CY[2]+(C_BONE[2]-C_CY[2])*k];
  }

  function draw() {
    ctx.clearRect(0, 0, W, H);
    const ox = mouseX * 36, oy = mouseY * 22;

    const dimP = new Path2D(), actP = new Path2D();
    for (let e = 0; e < edges.length; e++) {
      const a = edges[e][0], b = edges[e][1];
      const ax = px[a] + ox, ay = py[a] + oy, bx = px[b] + ox, by = py[b] + oy;
      if (Math.min(nodes[a].t, nodes[b].t) > 0.15) { actP.moveTo(ax, ay); actP.lineTo(bx, by); }
      else { dimP.moveTo(ax, ay); dimP.lineTo(bx, by); }
    }
    ctx.lineWidth = 1; ctx.strokeStyle = 'rgba(236,233,225,0.05)'; ctx.stroke(dimP);
    ctx.lineWidth = 1.2; ctx.strokeStyle = 'rgba(0,255,210,0.45)'; ctx.stroke(actP);

    ctx.fillStyle = 'rgba(0,255,210,0.9)';
    for (let e = 0; e < edges.length; e++) {
      const a = edges[e][0], b = edges[e][1];
      if (Math.min(nodes[a].t, nodes[b].t) <= 0.15) continue;
      const f = flow[e];
      ctx.fillRect((px[a] + (px[b] - px[a]) * f) + ox - 1.3, (py[a] + (py[b] - py[a]) * f) + oy - 1.3, 2.6, 2.6);
    }

    for (let i = 0; i < N; i++) {
      const x = px[i] + ox, y = py[i] + oy, t = nodes[i].t;
      const col = colAt(t);
      const cs = `${col[0]|0},${col[1]|0},${col[2]|0}`;
      ctx.strokeStyle = `rgba(${cs},${(0.4 + t * 0.6).toFixed(3)})`;
      ctx.fillStyle = `rgba(${cs},${(t * 0.16).toFixed(3)})`;
      ctx.lineWidth = 1 + t * 0.6;
      if (t > 0.3 && t < 0.8) { ctx.shadowColor = 'rgba(0,255,210,0.7)'; ctx.shadowBlur = 9; } else ctx.shadowBlur = 0;
      ctx.beginPath();
      if (nodes[i].type === 1) { const r = 4.4; ctx.rect(x - r, y - r, r * 2, r * 2); }
      else ctx.arc(x, y, 5, 0, Math.PI * 2);
      ctx.fill(); ctx.stroke();
      ctx.shadowBlur = 0;
    }
  }

  let rafId = null, last = 0, acc = 0;
  const FRAME = 1 / 30;
  function loop(now) {
    rafId = requestAnimationFrame(loop);
    const dt = Math.min(0.05, (now - last) / 1000); last = now;
    acc += dt;
    if (acc < FRAME) return;
    update(acc); draw(); acc = 0;
  }
  function start() { if (!rafId && !REDUCED) { last = performance.now(); rafId = requestAnimationFrame(loop); } }
  function stop() { if (rafId) { cancelAnimationFrame(rafId); rafId = null; } }

  let heroVisible = true;
  const hero = document.getElementById('top');
  if (hero) new IntersectionObserver((e) => {
    heroVisible = e[0].isIntersecting;
    if (heroVisible && !document.hidden) start(); else stop();
  }, { threshold: 0 }).observe(hero);
  document.addEventListener('visibilitychange', () => { if (document.hidden) stop(); else if (heroVisible) start(); });

  if (REDUCED) {
    wave = 9999; phase = 'breach';
    for (let i = 0; i < N; i++) nodes[i].t = 1;
    draw();
  } else {
    start();
  }
})();

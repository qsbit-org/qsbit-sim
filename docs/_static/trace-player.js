/* Shared renderer for documentation examples and local trace replay. */
(() => {
  'use strict';
  const root = document.getElementById('trace-player');
  if (!root) return;
  const url = new URL(root.dataset.bundle || 'trace-examples.json', document.currentScript.src);
  const {Recording, instruction, hex, coreId} = window.QsbitTrace;
  const $ = id => root.querySelector(`#trace-${id}`);
  function element(tag, text, parent, cls) {
    const node = document.createElement(tag);
    if (text !== undefined) node.textContent = text;
    if (cls) node.className = cls;
    parent.append(node); return node;
  }
  function svgElement(tag, attrs, parent) {
    const node = document.createElementNS('http://www.w3.org/2000/svg', tag);
    Object.entries(attrs).forEach(([key, value]) => node.setAttribute(key, value));
    parent.append(node); return node;
  }
  root.innerHTML = `
    <div class="replay-heading"><div><span class="replay-eyebrow">QSBIT · EXECUTION EXPLORER</span>
    <h2>Inside the controller</h2></div><button id="trace-expand" type="button" aria-pressed="false">Expand view</button></div>
    <div class="player-controls"><label>Recording <select id="trace-example"></select></label>
    <label>Core <select id="trace-core"></select></label><span id="trace-backend" class="replay-tag"></span></div>
    <div class="replay-transport"><button id="trace-prev" type="button" aria-label="Previous time point">←</button>
    <button id="trace-play" type="button">Play</button><button id="trace-tick" type="button">Next time →</button>
    <label>Speed <select id="trace-speed"><option value="600">1×</option><option value="200">3×</option><option value="60">10×</option></select></label>
    <strong id="trace-time">0 ns</strong></div>
    <label class="scrubber"><span class="sr-only">Simulation time</span><input id="trace-position" type="range" min="0" max="0" value="0"></label>
    <div id="trace-chapters" class="replay-chapters" aria-label="Execution milestones"></div>
    <p id="trace-status" role="status">Loading recorded executions…</p>
    <div id="trace-clocks" class="replay-metrics"></div>
    <div class="replay-map" id="trace-map"><svg id="trace-wires" aria-hidden="true"></svg></div>
    <div class="replay-legend"><span><i class="legend-active"></i>Changed at this time</span>
    <span><i class="legend-held"></i>Last observed</span><span>CPU slots show end-of-edge occupancy.</span></div>
    <section class="replay-panel"><div class="replay-section-heading"><h3>Control outputs</h3>
    <span>Device ports · duration in ns</span></div><div class="timeline-scroll"><svg id="trace-timeline" role="group" aria-label="Control output intervals"></svg></div></section>
    <div class="replay-detail-grid"><section class="replay-panel"><h3>Retired instructions</h3>
    <div id="trace-instructions" class="replay-instructions"></div></section>
    <section class="replay-panel"><h3>Measurement delivery</h3><div id="trace-delivery"></div></section></div>
    <details class="replay-records"><summary>Trace records and configuration</summary>
    <div class="player-controls"><button id="trace-back" type="button">Previous record</button>
    <button id="trace-next" type="button">Next record</button><select id="trace-jump" aria-label="Jump to recorded event"></select></div>
    <pre id="trace-event"></pre><details><summary>Input assembly</summary><pre id="trace-program"></pre></details>
    <details><summary>Run configuration</summary><pre id="trace-config"></pre></details></details>`;
  const modules = [
    ['cpu', '01', 'CPU', 'cpu-cycle-model'], ['reserve', '02', 'Reserve phase', 'reserve-phase'],
    ['timing', '03', 'Timing Queue', 'timing-controller'], ['events', '04', 'Per-port Event Queues', 'timing-controller'],
    ['trigger', '05', 'Trigger phase', 'timing-controller'], ['output', '06', 'Control output', 'control-output'],
    ['measurement', '07', 'Measurement', 'control-output'], ['registers', '08', 'Measurement registers', 'measurement-registers'],
  ];
  const edges = [
    ['cpu', 'reserve', 'cw · wait', ['CodewordQueued', 'TimingPointSubmitted']],
    ['reserve', 'timing', 'time point', ['TimingPointEnqueued']], ['reserve', 'events', 'events', ['TimingPointEnqueued']],
    ['timing', 'trigger', 'due', ['TimingPointTriggered']], ['events', 'trigger', 'events', ['TimingPointTriggered']],
    ['trigger', 'output', 'codeword', ['CodewordTriggered']], ['output', 'measurement', 'acquire', ['MeasurementSampled']],
    ['measurement', 'registers', 'result', ['MeasurementRegisterUpdated']], ['registers', 'cpu', 'FMR', ['MeasurementRegisterRead']],
    ['measurement', 'trigger', 'flags', ['ExecutionFlagsUpdated']],
  ];
  const owner = kind => {
    if (['CpuPipelineUpdated', 'InstructionRetired', 'CpuStalled', 'PipelineFlushed'].includes(kind)) return 'cpu';
    if (['CodewordQueued', 'TimingPointSubmitted', 'EnqueueAcknowledged'].includes(kind)) return 'reserve';
    if (kind === 'TimingPointEnqueued') return 'timing';
    if (['TimingPointTriggered', 'ConditionCancelled', 'ExecutionFlagsUpdated', 'TimerPaused', 'TimerResumed', 'SyncBooked', 'SyncReceived', 'SyncCompleted'].includes(kind)) return 'trigger';
    if (['MeasurementSampled', 'ResultReady'].includes(kind)) return 'measurement';
    if (['MeasurementRegisterRead', 'MeasurementRegisterUpdated'].includes(kind)) return 'registers';
    if (['OperationStart', 'OperationEnd', 'CodewordTriggered', 'GateApplied'].includes(kind)) return 'output';
    return null;
  };
  modules.forEach(([id, number, title, page]) => {
    const card = element('section', undefined, $('map'), `replay-module module-${id}`); card.id = `trace-node-${id}`;
    const heading = element('h3', undefined, card); element('span', number, heading, 'module-number');
    const link = element(root.dataset.standalone ? 'span' : 'a', title, heading);
    if (!root.dataset.standalone) link.href = `modules/${page}.html`;
    element('div', undefined, card, 'module-body').id = `trace-state-${id}`;
  });
  let demos, demo, recording, index = 0, timer = null, selectedCore = 0, activeKinds = new Set();
  const stop = () => { clearInterval(timer); timer = null; $('play').textContent = 'Play'; root.classList.remove('is-playing'); };
  const qubits = e => (e.targets ?? []).map(q => `q${q}`).join(', ');
  const bit = e => e ? String(Number(Boolean(e.value))) : '—';
  const stamp = e => e ? `${e.tick} ns` : 'Not recorded';
  function row(parent, label, value) {
    const item = element('div', undefined, parent, 'state-row');
    element('span', label, item); element('strong', String(value), item);
  }
  const hint = (parent, text) => element('p', text, parent, 'state-hint');
  const chip = (parent, text, cls = '') => element('span', text, parent, `state-chip ${cls}`);
  function drawWires() {
    const svg = $('wires'); svg.replaceChildren();
    const bounds = $('map').getBoundingClientRect();
    svg.setAttribute('viewBox', `0 0 ${bounds.width} ${bounds.height}`);
    const defs = svgElement('defs', {}, svg);
    for (const [name, color] of [['idle', 'var(--replay-wire)'], ['active', 'var(--replay-accent)']]) {
      const marker = svgElement('marker', {id: `arrow-${name}`, viewBox: '0 0 10 10', refX: 9, refY: 5, markerWidth: 6, markerHeight: 6, orient: 'auto-start-reverse'}, defs);
      svgElement('path', {d: 'M 0 0 L 10 5 L 0 10 z', fill: color}, marker);
    }
    edges.forEach(([from, to, label, kinds]) => {
      const a = $(`node-${from}`).getBoundingClientRect(), b = $(`node-${to}`).getBoundingClientRect();
      const horizontal = Math.abs(a.left - b.left) > 20, forward = horizontal ? b.left > a.left : b.top > a.top;
      const flags = from === 'measurement' && to === 'trigger';
      const x1 = (horizontal ? forward ? a.right : a.left : (a.left + a.right) / 2) - bounds.left;
      const y1 = (horizontal ? flags ? a.top + 24 : (a.top + a.bottom) / 2 : forward ? a.bottom : a.top) - bounds.top;
      const x2 = (horizontal ? forward ? b.left : b.right : (b.left + b.right) / 2) - bounds.left;
      const y2 = (horizontal ? flags ? b.bottom - 24 : (b.top + b.bottom) / 2 : forward ? b.top : b.bottom) - bounds.top;
      const mx = (x1 + x2) / 2, my = (y1 + y2) / 2;
      const d = horizontal ? `M${x1},${y1} H${mx} V${y2} H${x2}` : `M${x1},${y1} V${my} H${x2} V${y2}`;
      const active = kinds.some(k => activeKinds.has(k));
      svgElement('path', {d, class: `replay-wire${active ? ' active' : ''}`, 'marker-end': `url(#arrow-${active ? 'active' : 'idle'})`}, svg);
      svgElement('text', {x: mx, y: my - 7, 'text-anchor': 'middle', class: 'wire-label'}, svg).textContent = label;
    });
  }
  function describe(events) {
    const sentences = [], find = kind => events.filter(e => e.kind === kind);
    const outputs = find('OperationStart');
    if (outputs.length) sentences.push(outputs.map(e => `${e.operation.toUpperCase()} starts on port ${e.port}${qubits(e) ? ` (${qubits(e)})` : ''}`).join('; ') + '.');
    for (const [kind, text] of [
      ['MeasurementSampled', e => `${qubits(e)} is sampled: ${e.value}.`],
      ['ResultReady', e => `${qubits(e)} result ${e.value} is ready for delivery.`],
      ['MeasurementRegisterUpdated', e => `${qubits(e)} result ${e.value} is now visible to the CPU.`],
      ['ExecutionFlagsUpdated', e => `${qubits(e)} execution flags are updated from result ${e.value}.`],
      ['MeasurementRegisterRead', e => `FMR reads ${e.value} from ${qubits(e)}.`],
      ['TimingPointEnqueued', e => `Timing point enters the queue; recorded occupancy is ${e.value}.`],
      ['CodewordQueued', e => `Codeword ${e.codeword} on local port ${e.port} is prepared for time point ${e.cycle}.`],
      ['SimulationCompleted', () => 'Execution completed.'], ['SessionReset', () => 'Reset clears the controller and active outputs.'],
      ['Fault', e => `Execution stopped: ${e.detail || 'fault'}.`],
    ]) for (const e of find(kind)) sentences.push(text(e));
    if (!sentences.length) {
      const retired = find('InstructionRetired').at(-1), stalled = find('CpuStalled').at(-1);
      if (retired) sentences.push(`CPU retires ${instruction(retired)} at ${hex(retired.pc)}.`);
      else if (stalled) sentences.push(`CPU is waiting for ${stalled.detail || 'an unrecorded dependency'}.`);
      else sentences.push(events.at(-1)?.kind.replace(/([a-z])([A-Z])/g, '$1 $2') ?? 'No event on this core.');
    }
    return sentences.join(' ');
  }
  function show(next, wholeTick = false) {
    index = Math.max(0, Math.min(next, demo.events.length - 1));
    if (wholeTick) index = recording.endOfTick(index);
    const event = demo.events[index], state = recording.at(index), c = state.cores[selectedCore], profile = recording.configuration(selectedCore);
    let start = index;
    while (start > 0 && demo.events[start - 1].tick === event.tick) start--;
    const current = demo.events.slice(start, index + 1).filter(e => e.core === undefined || coreId(e) === selectedCore);
    activeKinds = new Set(current.map(e => e.kind));
    const changed = new Set(current.map(e => owner(e.kind)));
    if (activeKinds.has('TimingPointEnqueued') || activeKinds.has('TimingPointTriggered')) changed.add('events');
    if (activeKinds.has('TimingPointTriggered')) changed.add('timing');
    if (current.some(e => e.kind === 'OperationStart' && e.operation === 'measure')) changed.add('measurement');
    if (demo.events.slice(start, index + 1).some(e => ['OperationStart', 'OperationEnd', 'GateApplied'].includes(e.kind))) changed.add('output');
    if (activeKinds.has('SessionReset')) modules.forEach(([id]) => changed.add(id));
    modules.forEach(([id]) => { $(`state-${id}`).replaceChildren(); $(`node-${id}`).classList.toggle('changed', changed.has(id)); });
    $('position').value = event.tick; $('time').textContent = `${event.tick} ns`;
    $('position').setAttribute('aria-valuetext', `${event.tick} nanoseconds`);
    $('status').textContent = `${describe(current)}${demo.events[index + 1]?.tick === event.tick ? ' · Within timestamp' : ''}`;
    $('event').textContent = JSON.stringify(event, null, 2);
    $('prev').disabled = $('back').disabled = index === 0;
    $('next').disabled = $('tick').disabled = index === demo.events.length - 1; $('jump').value = String(index);
    $('clocks').replaceChildren();
    row($('clocks'), 'Epoch', state.epoch ?? 'Not recorded');
    row($('clocks'), 'CPU edge · derived', profile?.cpu?.period > 0 && event.tick >= (profile.cpu.phase ?? 0) ? Math.floor((event.tick - (profile.cpu.phase ?? 0)) / profile.cpu.period) : '—');
    row($('clocks'), 'TCU cycle · recorded', c?.cycle ? `${c.cycle.cycle} at ${c.cycle.tick} ns` : '—');
    row($('clocks'), 'Records', `${index + 1} of ${demo.events.length}`);
    const cpu = $('state-cpu'), pipeline = element('div', undefined, cpu, 'cpu-stages');
    const snapshot = c?.pipeline?.pipeline;
    const previous = recording.at(Math.max(0, start - 1)).cores[selectedCore]?.pipeline?.pipeline;
    for (const [key, title] of [['fetch', 'Fetch request'], ['fetched', 'Fetch buffer'], ['decode', 'Decode'], ['execute', 'Execute']]) {
      const slot = snapshot?.[key];
      const changed = activeKinds.has('CpuPipelineUpdated') && JSON.stringify(slot) !== JSON.stringify(previous?.[key]);
      const stage = element('div', undefined, pipeline, `cpu-stage${slot ? ' occupied' : ''}${changed ? ' changed' : ''}${slot?.discarded ? ' discarded' : ''}`);
      stage.dataset.stage = key;
      element('span', title, stage, 'stage-title');
      if (slot) {
        element('code', `#${slot.id} · ${hex(slot.pc)}`, stage);
        if (slot.word !== null) element('code', instruction(slot), stage);
        if (slot.discarded) element('span', 'Discard on return', stage, 'stage-status');
        else if (key === 'execute' && c?.stall?.id === slot.id) element('span', 'Blocked', stage, 'stage-status');
      } else element('span', snapshot ? 'Empty' : 'Not recorded', stage, 'stage-status');
    }
    if (c?.pipeline) hint(cpu, `${snapshot.halted ? 'Halted' : 'Pipeline updated'} at ${c.pipeline.tick} ns`);
    const retirement = element('div', undefined, cpu, `retirement${activeKinds.has('InstructionRetired') ? ' changed' : ''}`);
    row(retirement, 'Retired', c?.retired ? `#${c.retired.id} · ${stamp(c.retired)}` : 'Not recorded');
    if (c?.stall) {
      row(cpu, 'Waiting', stamp(c.stall));
      hint(cpu, c.stall.detail);
    }
    if (c?.retired) {
      element('code', instruction(c.retired), cpu, 'retired-word'); hint(cpu, `${hex(c.retired.pc)} → ${hex(c.retired.next_pc)}`);
      const regs = element('div', undefined, cpu, 'register-grid');
      (c.retired.registers ?? []).forEach((value, r) => { if (value || (r && r === c.retired.rd)) chip(regs, `x${r} ${hex(value)}`, r === c.retired.rd ? 'changed' : ''); });
    } else hint(cpu, 'No retirement recorded.');
    const reserve = $('state-reserve'); row(reserve, 'Time point', c?.point?.cycle ?? '—'); row(reserve, 'Staged codewords', c?.staged.length ?? 0);
    for (const e of c?.staged ?? []) chip(reserve, `p${e.port} · cw ${e.codeword}`);
    if (c?.point) hint(reserve, `Recorded at ${c.point.tick} ns`);
    const timing = $('state-timing'); hint(timing, 'Enqueued time points → trigger');
    const queue = element('div', undefined, timing, 'queue-entries');
    for (const item of c?.queue ?? []) chip(queue, item.point === null ? `#${item.label} · time unknown` : `t = ${item.point}`, 'queue-entry');
    if (!c?.queue.length) hint(queue, 'No pending entries observed.');
    if (c?.occupancy) {
      row(timing, 'Pending entries', profile?.timing_capacity ? `${c.occupancy.value} of ${profile.timing_capacity}` : c.occupancy.value);
      if (profile?.timing_capacity) {
        const capacity = element('meter', undefined, timing); capacity.min = 0; capacity.max = profile.timing_capacity; capacity.value = c.occupancy.value;
        capacity.setAttribute('aria-label', 'Timing queue occupancy');
      }
      hint(timing, `Updated at ${c.occupancy.tick} ns`);
    }
    const events = $('state-events'), ports = recording.queuedEvents(selectedCore, c?.queue ?? []);
    for (const [port, entries] of ports) {
      const line = element('div', undefined, events, 'port-queue'); chip(line, `p${port}`, 'port-label');
      for (const {operation, point} of entries) chip(line, `${operation} @ ${point}`);
    }
    hint(events, !profile ? 'Port mapping unavailable.' : ports.size ? 'Mapped events · local port · time point' : 'No queued events observed.');
    const trigger = $('state-trigger'); row(trigger, 'Timer', c?.paused ? 'Paused' : c?.cycle ? 'Running' : 'Not recorded');
    row(trigger, 'Last trigger cycle', c?.triggered?.cycle ?? '—');
    for (const [q, flag] of Object.entries(c?.flags ?? {})) {
      row(trigger, `q${q} · last_one`, Number(flag.last_one)); row(trigger, `q${q} · last_zero`, Number(flag.last_zero)); hint(trigger, `Flags committed at ${flag.tick} ns`);
    }
    if (c?.sync) hint(trigger, `${c.sync.kind} · peer ${(c.sync.targets ?? []).join(', ')} · ${c.sync.tick} ns`);
    if (c?.cancelled) hint(trigger, `${c.cancelled.operation} ${qubits(c.cancelled)} skipped at ${c.cancelled.tick} ns`);
    const output = $('state-output'), active = Object.values(state.active);
    for (const e of active) {
      const item = element('div', undefined, output, 'active-output'); row(item, `Port ${e.port} · core ${coreId(e)}`, e.operation?.toUpperCase() ?? 'Operation');
      const progress = element('progress', undefined, item); progress.max = Math.max(e.value ?? 0, 1); progress.value = Math.max(0, event.tick - e.tick);
      progress.setAttribute('aria-label', `Port ${e.port} elapsed duration`); hint(item, `${qubits(e)} · started ${e.tick} ns · duration ${e.value ?? 'unknown'} ns`);
    }
    if (!active.length) hint(output, 'No active outputs observed.');
    if (state.gate) hint(output, `Last joint gate: ${state.gate.operation.toUpperCase()} ${qubits(state.gate)} at ${state.gate.tick} ns`);
    const measurements = Object.values(c?.measurements ?? {}).sort((a, b) => b.id - a.id), measurement = $('state-measurement');
    for (const m of measurements.slice(0, 2)) {
      row(measurement, qubits(m), m.ResultReady ? 'Result ready' : 'Sampled'); hint(measurement, `${bit(m.MeasurementSampled ?? m.ResultReady)} · ${stamp(m.ResultReady ?? m.MeasurementSampled)}`);
    }
    if (!measurements.length) hint(measurement, active.some(e => e.operation === 'measure') ? 'Acquisition in progress.' : 'No measurement sampled.');
    const registers = $('state-registers');
    for (const [q, e] of Object.entries(c?.registers ?? {})) { row(registers, `q${q}`, bit(e)); hint(registers, `CPU-visible at ${e.tick} ns`); }
    if (!Object.keys(c?.registers ?? {}).length) hint(registers, 'No result delivered to the CPU.');
    if (c?.read) hint(registers, `Last FMR: ${qubits(c.read)} = ${c.read.value} at ${c.read.tick} ns`);
    drawDelivery(measurements); drawInstructions(state.epoch);
    const cursor = $('cursor');
    if (cursor) { const x = 115 + event.tick / Number(cursor.dataset.max) * 850; cursor.setAttribute('x1', x); cursor.setAttribute('x2', x); }
    $('timeline').querySelectorAll('[data-start]').forEach(node => node.classList.toggle('future', Number(node.dataset.start) > event.tick));
    $('chapters').querySelectorAll('button').forEach(b => b.classList.toggle('reached', Number(b.dataset.index) <= index));
    drawWires(); if (index === demo.events.length - 1) stop();
  }
  function drawDelivery(measurements) {
    const host = $('delivery'); host.replaceChildren();
    if (!measurements.length) { hint(host, 'Sampling, result readiness and visibility appear here as they are recorded.'); return; }
    for (const m of measurements) {
      element('h4', `${qubits(m)} · measurement ${m.id}`, host); const steps = element('div', undefined, host, 'delivery-steps');
      for (const [kind, label] of [['MeasurementSampled', 'Sampled'], ['ResultReady', 'Ready'], ['MeasurementRegisterUpdated', 'CPU visible'], ['ExecutionFlagsUpdated', 'Flags updated']]) {
        const step = element('div', undefined, steps, m[kind] ? 'delivered' : 'pending'); element('span', label, step); element('strong', m[kind] ? `${m[kind].tick} ns` : '—', step);
      }
    }
  }
  function drawInstructions(epoch) {
    const host = $('instructions'); host.replaceChildren(); const items = [];
    for (let i = index; i >= 0 && items.length < 6; i--) {
      const e = demo.events[i]; if (e.epoch !== undefined && e.epoch !== epoch) break;
      if (e.kind === 'InstructionRetired' && coreId(e) === selectedCore) items.push({e, i});
    }
    if (!items.length) { hint(host, 'No instruction has retired in this epoch.'); return; }
    for (const {e, i} of items.reverse()) {
      const button = element('button', undefined, host, 'instruction-row'); button.type = 'button';
      element('span', `${e.tick} ns`, button); element('code', hex(e.pc), button); element('code', instruction(e), button);
      button.addEventListener('click', () => { stop(); show(i); });
    }
  }
  function drawTimeline() {
    const svg = $('timeline'); svg.replaceChildren();
    const ports = [...new Set(recording.intervals.map(i => i.event.port))].sort((a, b) => a - b);
    const height = Math.max(ports.length, 1) * 44 + 48, max = demo.events.at(-1).tick || 1, x = tick => 115 + tick / max * 850;
    svg.setAttribute('viewBox', `0 0 1000 ${height}`);
    for (let step = 0; step <= 5; step++) {
      const tick = max * step / 5; svgElement('line', {x1: x(tick), x2: x(tick), y1: 20, y2: height - 24, class: 'time-grid'}, svg);
      svgElement('text', {x: x(tick), y: 12, 'text-anchor': 'middle'}, svg).textContent = `${Math.round(tick)}`;
    }
    ports.forEach((port, row) => {
      const y = row * 44 + 28; svgElement('text', {x: 8, y: y + 18}, svg).textContent = `Port ${port}`;
      svgElement('line', {x1: 115, x2: 965, y1: y + 13, y2: y + 13, class: 'lane'}, svg);
      for (const item of recording.intervals.filter(i => i.event.port === port)) {
        const e = item.event, end = item.end ?? demo.events.at(-1).tick;
        const g = svgElement('g', {class: 'output-interval', tabindex: 0, role: 'button', 'data-start': e.tick,
          'aria-label': `Port ${port}, ${e.operation}, ${e.tick} to ${end} ns${item.end === null ? ', end not recorded' : ''}`}, svg);
        svgElement('rect', {x: x(e.tick), y, width: Math.max(4, x(end) - x(e.tick)), height: 26, rx: 5, class: e.operation === 'measure' ? 'acquire-interval' : 'gate-interval'}, g);
        svgElement('title', {}, g).textContent = `Core ${coreId(e)} · ${e.operation} ${qubits(e)} · ${e.tick}–${end} ns${item.aborted ? ' · aborted' : ''}`;
        if (x(end) - x(e.tick) > Math.min(32, e.operation.length * 8)) svgElement('text', {x: (x(e.tick) + x(end)) / 2, y: y + 17, 'text-anchor': 'middle', class: 'interval-label'}, g).textContent = e.operation;
        const seek = () => { stop(); show(item.index, true); }; g.addEventListener('click', seek);
        g.addEventListener('keydown', ev => { if (['Enter', ' '].includes(ev.key)) { ev.preventDefault(); seek(); } });
      }
    });
    if (!ports.length) svgElement('text', {x: 115, y: 46}, svg).textContent = 'No control output intervals recorded.';
    const cursor = svgElement('line', {id: 'trace-cursor', x1: 115, x2: 115, y1: 18, y2: height - 20, class: 'time-cursor'}, svg); cursor.dataset.max = max;
  }
  function selectDemo() {
    stop(); demo = demos[Number($('example').value)]; recording = new Recording(demo); $('core').replaceChildren();
    recording.cores.forEach(id => { element('option', `Core ${id}`, $('core')).value = id; }); selectedCore = recording.cores[0];
    $('backend').textContent = demo.backend === 'mock' ? 'Mock measurement outcomes' : demo.backend ? `Backend: ${demo.backend}` : 'Backend not recorded';
    $('program').textContent = demo.program || 'Assembly source not included.'; $('config').textContent = JSON.stringify(demo.cores ?? demo.configuration, null, 2);
    $('position').min = demo.events[0].tick; $('position').max = demo.events.at(-1).tick; $('jump').replaceChildren();
    demo.events.forEach((e, i) => { element('option', `${e.tick} ns · ${e.kind}${e.core !== undefined ? ` · core ${e.core}` : ''}`, $('jump')).value = i; });
    $('chapters').replaceChildren();
    for (const [kind, title] of [['CodewordQueued', 'Prepare'], ['TimingPointEnqueued', 'Enqueue'], ['OperationStart', 'Output'], ['MeasurementSampled', 'Measure'], ['MeasurementRegisterUpdated', 'CPU result'], ['ExecutionFlagsUpdated', 'Flags']]) {
      const i = demo.events.findIndex(e => e.kind === kind); if (i < 0) continue;
      const b = element('button', title, $('chapters')); b.type = 'button'; b.dataset.index = i; b.addEventListener('click', () => { stop(); show(i, true); });
    }
    drawTimeline(); show(0, true);
  }
  $('prev').addEventListener('click', () => { stop(); show(recording.previousTick(index)); });
  $('back').addEventListener('click', () => { stop(); show(index - 1); }); $('next').addEventListener('click', () => { stop(); show(index + 1); });
  $('tick').addEventListener('click', () => { stop(); show(recording.nextTick(index)); });
  $('position').addEventListener('input', e => { stop(); show(recording.indexAtTick(Number(e.target.value)), true); }); $('jump').addEventListener('change', e => { stop(); show(Number(e.target.value)); });
  $('play').addEventListener('click', () => {
    if (timer) { stop(); return; } if (index === demo.events.length - 1) show(0, true);
    $('play').textContent = 'Pause'; root.classList.add('is-playing'); timer = setInterval(() => show(recording.nextChange(index)), Number($('speed').value));
  });
  $('speed').addEventListener('change', stop); $('example').addEventListener('change', selectDemo);
  $('core').addEventListener('change', () => { selectedCore = Number($('core').value); show(index); });
  function expand(enabled) {
    root.classList.toggle('expanded', enabled); document.body.classList.toggle('replay-expanded', enabled);
    $('expand').textContent = enabled ? 'Close expanded view' : 'Expand view'; $('expand').setAttribute('aria-pressed', String(enabled)); requestAnimationFrame(drawWires);
  }
  $('expand').addEventListener('click', () => expand(!root.classList.contains('expanded')));
  document.addEventListener('keydown', e => { if (e.key === 'Escape' && root.classList.contains('expanded')) expand(false); });
  new ResizeObserver(() => { if (recording) drawWires(); }).observe($('map'));
  root.querySelectorAll('button, select, input').forEach(control => control.disabled = true);
  fetch(url).then(response => { if (!response.ok) throw new Error(`HTTP ${response.status}`); return response.json(); }).then(bundle => {
    if (bundle.schema !== 1 || !Array.isArray(bundle.examples) || !bundle.examples.length) throw new Error('Unsupported example bundle');
    demos = bundle.examples;
    demos.forEach((d, i) => {
      if (!Array.isArray(d.events) || !d.events.length || d.events.some((e, j) => e.schema !== 1 || !Number.isSafeInteger(e.tick) || e.tick < 0 || typeof e.kind !== 'string' || (j && e.tick < d.events[j - 1].tick))) throw new Error('Invalid trace records');
      element('option', d.name, $('example')).value = i;
    });
    root.querySelectorAll('button, select, input').forEach(control => control.disabled = false); selectDemo();
  }).catch(error => {
    root.querySelectorAll('button, select, input').forEach(control => control.disabled = true);
    $('status').textContent = `Cannot load execution data: ${error.message}. Serve the page over HTTP.`;
  });
})();

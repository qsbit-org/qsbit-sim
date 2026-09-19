/* Read-only playback of events recorded by qsbit-sim. */
(() => {
  'use strict';
  const root = document.getElementById('trace-player');
  if (!root) return;
  const script = document.currentScript;
  const url = new URL('trace-examples.json', script.src);
  const $ = id => document.getElementById(`trace-${id}`);
  const lanes = [
    ['cpu', 'CPU', 'cpu-cycle-model'],
    ['producer', 'Producer', 'timeline-reservation-manager'],
    ['tcu', 'TCU', 'tcu-timer-and-label-broadcaster'],
    ['device', 'Device', 'output-channels-and-resource-calendar'],
    ['feedback', 'Result delivery', 'measurement-scoreboard-and-cpu-feedback'],
    ['lifecycle', 'Run lifecycle', 'trace-recorder-and-stop-controller'],
  ];
  const lane = e => {
    if (['InstructionRetired', 'CpuStalled', 'PipelineFlushed'].includes(e.kind)) return 'cpu';
    if (['ProducerAccepted', 'GroupSubmitted', 'GroupReplyVisible', 'ResultConsumed'].includes(e.kind)) return 'producer';
    if (['GroupAdmitted', 'LabelFired', 'ConditionCancelled', 'EndOfStreamVisible'].includes(e.kind)) return 'tcu';
    if (['CpuResultVisible', 'FastResultVisible'].includes(e.kind)) return 'feedback';
    if (['CodewordTriggered', 'OperationStart', 'OperationEnd', 'MeasurementSampled', 'ResultReady'].includes(e.kind)) return 'device';
    return 'lifecycle';
  };
  let demos = [], demo, index = 0, timer = null;
  const stop = () => { clearInterval(timer); timer = null; $('play').textContent = 'Play'; };
  function element(tag, text, parent, cls) {
    const node = document.createElement(tag);
    if (text !== undefined) node.textContent = text;
    if (cls) node.className = cls;
    parent.append(node);
    return node;
  }
  function svgElement(tag, attrs, parent) {
    const node = document.createElementNS('http://www.w3.org/2000/svg', tag);
    Object.entries(attrs).forEach(([key, value]) => node.setAttribute(key, value));
    parent.append(node);
    return node;
  }
  function drawTimeline() {
    const svg = $('timeline');
    svg.replaceChildren();
    svg.setAttribute('viewBox', '0 0 940 260');
    const max = demo.events.at(-1).tick || 1;
    const x = tick => 105 + tick / max * 800;
    lanes.forEach(([key, label], row) => {
      const y = 28 + row * 34;
      svgElement('text', {x: 4, y: y + 4}, svg).textContent = label;
      svgElement('line', {x1: 105, y1: y, x2: 905, y2: y, class: 'lane'}, svg);
    });
    // CPU stall repetitions remain accessible in the event slider without obscuring milestones.
    demo.events.forEach((event, i) => {
      if (event.kind === 'CpuStalled') return;
      const row = lanes.findIndex(o => o[0] === lane(event));
      const dot = svgElement('circle', {cx: x(event.tick), cy: 28 + row * 34,
        r: 4, class: 'trace-dot', 'data-index': i, tabindex: 0, role: 'button',
        'aria-label': `${event.tick} ns ${event.kind}`}, svg);
      svgElement('title', {}, dot).textContent = `${event.tick} ns · ${event.kind} · ID ${event.id}`;
      dot.addEventListener('click', () => { stop(); show(i); });
      dot.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); stop(); show(i); } });
    });
    const cursor = svgElement('line', {id: 'trace-cursor', x1: 105, x2: 105, y1: 8, y2: 218, class: 'time-cursor'}, svg);
    cursor.dataset.max = max;
    svgElement('text', {x: 105, y: 248}, svg).textContent = '0 ns';
    svgElement('text', {x: 905, y: 248, 'text-anchor': 'end'}, svg).textContent = `${max} ns`;
  }
  function show(next) {
    index = Math.max(0, Math.min(next, demo.events.length - 1));
    const event = demo.events[index], profile = demo.configuration;
    $('position').value = index;
    $('status').textContent = `Event ${index + 1} / ${demo.events.length} · ${event.tick} ns · ${event.kind}`;
    $('event').textContent = JSON.stringify(event, null, 2);
    $('prev').disabled = index === 0;
    $('next').disabled = $('tick').disabled = index === demo.events.length - 1;
    $('owners').querySelectorAll('a').forEach(a => {
      a.classList.toggle('active', a.dataset.owner === lane(event));
      if (a.dataset.owner === lane(event)) a.setAttribute('aria-current', 'true'); else a.removeAttribute('aria-current');
    });
    $('clocks').replaceChildren();
    const values = [
      ['Global tick', `${event.tick} ns`],
      ['CPU edge index (derived)', event.tick < profile.cpu.phase ? 'Before first edge' : Math.floor((event.tick - profile.cpu.phase) / profile.cpu.period)],
      ['TCU logical cycle (derived)', event.tick < profile.start ? 'Before start' : Math.floor((event.tick - profile.start) / profile.tcu.period)],
    ];
    values.forEach(([label, value]) => { const card = element('div', undefined, $('clocks'), 'clock-card'); element('span', label, card); element('strong', String(value), card); });
    const observed = new Map();
    for (let i = 0; i <= index; i++) {
      const e = demo.events[i];
      if (['ProducerAccepted', 'GroupSubmitted'].includes(e.kind)) observed.set('Producer cursor (last recorded)', `${e.cycle} at ${e.tick} ns`);
      if (e.kind === 'InstructionRetired') observed.set('Retired instruction', `PC 0x${e.pc.toString(16)} → 0x${e.next_pc.toString(16)} at ${e.tick} ns`);
      if (e.kind === 'GroupAdmitted') observed.set('Timing queue occupancy (at last admission)', `${e.value} at ${e.tick} ns`);
      if (e.kind === 'LabelFired') observed.set('Last fired label', `${e.label} at ${e.tick} ns`);
      if (e.kind === 'CpuResultVisible') observed.set(`CPU measurement ${e.id}`, `${e.value} visible at ${e.tick} ns`);
      if (e.kind === 'FastResultVisible') observed.set(`TCU measurement ${e.id}`, `${e.value} committed at ${e.tick} ns; earliest condition edge (derived): ${e.tick + profile.tcu.period} ns`);
    }
    $('observed').replaceChildren();
    if (!observed.size) element('p', 'No state observations yet.', $('observed'));
    const list = element('dl', undefined, $('observed'));
    observed.forEach((value, key) => { element('dt', key, list); element('dd', value, list); });
    const cursor = $('cursor'), x = 105 + event.tick / Number(cursor.dataset.max) * 800;
    cursor.setAttribute('x1', x); cursor.setAttribute('x2', x);
    $('timeline').querySelectorAll('.trace-dot').forEach(dot => dot.classList.toggle('selected', Number(dot.dataset.index) === index));
    $('jump').value = ['CpuStalled', 'InstructionRetired'].includes(event.kind) ? '' : String(index);
    if (index === demo.events.length - 1) stop();
  }
  function selectDemo() {
    stop();
    demo = demos[Number($('example').value)];
    $('program').textContent = demo.program;
    $('config').textContent = JSON.stringify(demo.configuration, null, 2);
    $('position').max = demo.events.length - 1;
    $('jump').replaceChildren();
    element('option', 'Jump to milestone…', $('jump')).value = '';
    demo.events.forEach((e, i) => {
      if (!['CpuStalled', 'InstructionRetired'].includes(e.kind)) element('option', `${e.tick} ns · ${e.kind} · ${e.operation || e.label || e.id}`, $('jump')).value = i;
    });
    drawTimeline(); show(0);
  }
  $('prev').addEventListener('click', () => { stop(); show(index - 1); });
  $('next').addEventListener('click', () => { stop(); show(index + 1); });
  $('tick').addEventListener('click', () => { stop(); const next = demo.events.findIndex((e, i) => i > index && e.tick > demo.events[index].tick); show(next < 0 ? demo.events.length - 1 : next); });
  $('position').addEventListener('input', e => { stop(); show(Number(e.target.value)); });
  $('jump').addEventListener('change', e => { if (e.target.value !== '') { stop(); show(Number(e.target.value)); } });
  $('play').addEventListener('click', () => {
    if (timer) { stop(); return; }
    if (index === demo.events.length - 1) show(0);
    $('play').textContent = 'Pause';
    timer = setInterval(() => show(index + 1), Number($('speed').value));
  });
  $('speed').addEventListener('change', stop);
  $('example').addEventListener('change', selectDemo);
  lanes.forEach(([key, label, page]) => { const a = element('a', label, $('owners'), 'owner-card'); a.href = `modules/${page}.html`; a.dataset.owner = key; });
  root.querySelectorAll('button, select, input').forEach(control => control.disabled = true);
  fetch(url).then(response => { if (!response.ok) throw new Error(`HTTP ${response.status}`); return response.json(); }).then(bundle => {
    if (bundle.schema !== 1 || !Array.isArray(bundle.examples) || !bundle.examples.length) throw new Error('Unsupported example bundle');
    demos = bundle.examples;
    demos.forEach((d, i) => { if (!d.events.length || d.events.some(e => e.schema !== 1)) throw new Error('Unsupported trace schema'); element('option', d.name, $('example')).value = i; });
    root.querySelectorAll('button, select, input').forEach(control => control.disabled = false);
    selectDemo();
  }).catch(error => { $('status').textContent = `Cannot load execution data: ${error.message}. Serve this site over local HTTP as described in the website build guide.`; });
})();

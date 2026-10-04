/* State reconstructed from recorded transitions, independently of the view. */
(() => {
  'use strict';
  const coreId = e => e.core ?? 0;
  const freshCore = () => ({retired: null, pipeline: null, stall: null, point: null, staged: [], submitted: {},
    queue: [], occupancy: null, cycle: null, triggered: null, registers: {}, flags: {},
    measurements: {}, read: null, sync: null, paused: null});
  const fresh = () => ({epoch: null, cores: {}, active: {}, lastOutput: {}, gate: null, terminal: null});
  const outputKey = e => `${coreId(e)}:${e.id}`;
  function reduce(state, e) {
    if (e.kind === 'SessionStarted' || e.kind === 'SessionReset' ||
        (e.epoch !== undefined && state.epoch !== null && e.epoch !== state.epoch)) {
      Object.assign(state, fresh(), {epoch: e.epoch ?? null});
    }
    if (e.epoch !== undefined) state.epoch = e.epoch;
    const c = state.cores[coreId(e)] ??= freshCore();
    switch (e.kind) {
    case 'CpuPipelineUpdated': c.pipeline = e; break;
    case 'InstructionRetired': c.retired = e; c.stall = null; break;
    case 'CpuStalled': c.stall = e; break;
    case 'PipelineFlushed': c.stall = null; break;
    case 'CodewordQueued': c.point = e; c.staged.push(e); break;
    case 'TimingPointSubmitted':
      c.point = e;
      c.submitted[e.label] = {label: e.label, point: e.cycle, codewords: c.staged, tick: e.tick};
      c.staged = [];
      break;
    case 'TimingPointEnqueued':
      c.cycle = e;
      c.queue.push(c.submitted[e.label] ?? {label: e.label, point: null, codewords: [], tick: e.tick});
      delete c.submitted[e.label];
      c.occupancy = e;
      break;
    case 'TimingPointTriggered':
      c.cycle = e; c.triggered = e;
      c.queue = c.queue.filter(item => item.label !== e.label);
      if (c.occupancy) c.occupancy = {...e, value: Math.max(0, c.occupancy.value - 1)};
      break;
    case 'ExecutionFlagsUpdated':
      c.cycle = e;
      for (const q of e.targets ?? []) {
        const previous = c.flags[q];
        c.flags[q] = {...e, last_one: Boolean(e.value), last_zero: !e.value,
          equal: previous ? previous.value === e.value : null};
      }
      break;
    case 'ConditionCancelled': c.cancelled = e; break;
    case 'MeasurementRegisterUpdated':
      for (const q of e.targets ?? []) c.registers[q] = e;
      break;
    case 'MeasurementRegisterRead': c.read = e; break;
    case 'TimerPaused': c.paused = e; break;
    case 'TimerResumed': c.paused = null; break;
    case 'SyncBooked': case 'SyncReceived': case 'SyncCompleted': c.sync = e; break;
    case 'CodewordTriggered': c.output = e; break;
    case 'OperationStart': state.active[outputKey(e)] = e; state.lastOutput[e.port] = e; break;
    case 'OperationEnd': case 'ResetAborted': delete state.active[outputKey(e)]; break;
    case 'GateApplied': state.gate = e; break;
    case 'SimulationCompleted': case 'Fault': state.terminal = e; break;
    }
    if (['MeasurementSampled', 'ResultReady', 'MeasurementRegisterUpdated', 'ExecutionFlagsUpdated'].includes(e.kind)) {
      const m = c.measurements[e.id] ??= {id: e.id, targets: e.targets ?? []};
      m[e.kind] = e;
      const ids = Object.keys(c.measurements).sort((a, b) => Number(b) - Number(a));
      for (const id of ids.slice(4)) delete c.measurements[id];
    }
    return state;
  }
  class Recording {
    constructor(demo) {
      this.demo = demo;
      this.events = demo.events;
      this.checkpoints = new Map();
      this.ticks = [];
      this.steps = [];
      this.intervals = [];
      const active = new Map();
      let state = fresh();
      const stalls = new Map();
      let changed = false;
      this.events.forEach((e, i) => {
        if (e.kind === 'CpuStalled') {
          const signature = `${e.epoch}:${e.id}:${e.detail}`;
          if (stalls.get(coreId(e)) !== signature) changed = true;
          stalls.set(coreId(e), signature);
        } else {
          changed = true;
          if (e.kind === 'InstructionRetired') stalls.delete(coreId(e));
        }
        if (this.events[i + 1]?.tick !== e.tick) {
          this.ticks.push(i);
          if (changed) this.steps.push(i);
          changed = false;
        }
        if (e.kind === 'SessionReset') {
          for (const interval of active.values()) { interval.end = e.tick; interval.aborted = true; }
          active.clear();
        }
        const key = outputKey(e);
        if (e.kind === 'OperationStart') {
          const interval = {event: e, index: i, end: null, aborted: false};
          active.set(key, interval); this.intervals.push(interval);
        }
        if (['OperationEnd', 'ResetAborted'].includes(e.kind) && active.has(key)) {
          Object.assign(active.get(key), {end: e.tick, aborted: e.kind === 'ResetAborted'});
          active.delete(key);
        }
        reduce(state, e);
        if (i % 128 === 0) this.checkpoints.set(i, structuredClone(state));
      });
      this.cores = [...new Set(this.events.filter(e => e.core !== undefined).map(coreId))];
      if (!this.cores.length) this.cores = [0];
    }
    at(index) {
      const checkpoint = Math.floor(index / 128) * 128;
      const state = structuredClone(this.checkpoints.get(checkpoint));
      for (let i = checkpoint + 1; i <= index; i++) reduce(state, this.events[i]);
      return state;
    }
    endOfTick(index) {
      while (index + 1 < this.events.length && this.events[index + 1].tick === this.events[index].tick) index++;
      return index;
    }
    nextTick(index) { return this.endOfTick(Math.min(this.events.length - 1, this.endOfTick(index) + 1)); }
    nextChange(index) { return this.steps.find(i => i > index) ?? this.events.length - 1; }
    indexAtTick(tick) {
      let lo = 0, hi = this.events.length;
      while (lo < hi) {
        const mid = Math.floor((lo + hi) / 2);
        if (this.events[mid].tick <= tick) lo = mid + 1; else hi = mid;
      }
      return Math.max(0, lo - 1);
    }
    previousTick(index) {
      while (index > 0 && this.events[index - 1].tick === this.events[index].tick) index--;
      return Math.max(0, index - 1);
    }
    configuration(core) {
      return this.demo.cores?.find(c => c.id === Number(core))?.configuration ?? this.demo.configuration;
    }
    queuedEvents(core, queue) {
      const ports = new Map(), profile = this.configuration(core);
      for (const item of queue) for (const e of item.codewords) {
        const mapping = profile?.mappings?.find(m => m.port === e.port && m.codeword === e.codeword);
        for (const action of mapping?.actions ?? []) {
          if (!ports.has(action.port)) ports.set(action.port, []);
          ports.get(action.port).push({point: item.point, operation: action.operation ?? action.gate ?? action.kind});
        }
      }
      return ports;
    }
  }
  const hex = n => `0x${(n >>> 0).toString(16).padStart(8, '0')}`;
  function instruction(e) {
    const w = e.word >>> 0, op = w & 127, rd = (w >>> 7) & 31;
    const f3 = (w >>> 12) & 7, rs1 = (w >>> 15) & 31, rs2 = (w >>> 20) & 31;
    if (op === 11) {
      if (f3 === 0) return `cw ${(w & (1 << 25)) ? rs1 : `x${rs1}`}, ${(w & (1 << 26)) ? rs2 : `x${rs2}`}`;
      if (f3 === 1) return `wait x${rs1}`;
      if (f3 === 2) return `wait ${w >>> 15}`;
      if (f3 === 3) return `FMR x${rd}, q${rs1}`;
      if (f3 === 6) return `sync ${w >>> 15}`;
    }
    if (op === 43) {
      const slots = [w >>> 7, w >>> 19].map(slot => {
        const port = slot & 31, codeword = (slot >>> 5) & 31;
        return `${slot & 1024 ? port : `x${port}`}, ${slot & 2048 ? codeword : `x${codeword}`}`;
      });
      return `cw.bundle [${slots.join('] [')}]`;
    }
    if (op === 19 && f3 === 0) return `addi x${rd}, x${rs1}, ${w >> 20}`;
    if (op === 55) return `lui x${rd}, ${hex(w >>> 12)}`;
    if (op === 23) return `auipc x${rd}, ${hex(w >>> 12)}`;
    if (w === 0x73) return 'ecall';
    if (w === 0x100073) return 'ebreak';
    if (op === 99) return `${['beq', 'bne', null, null, 'blt', 'bge', 'bltu', 'bgeu'][f3] ?? 'branch'} x${rs1}, x${rs2}`;
    if (op === 3) return `${['lb', 'lh', 'lw', null, 'lbu', 'lhu'][f3] ?? 'load'} x${rd}, ${w >> 20}(x${rs1})`;
    if (op === 35) return `${['sb', 'sh', 'sw'][f3] ?? 'store'} x${rs2}, ${((w >> 25) << 5) | rd}(x${rs1})`;
    if (op === 111) return `jal x${rd}`;
    if (op === 103) return `jalr x${rd}, ${w >> 20}(x${rs1})`;
    return `.word ${hex(w)}`;
  }
  window.QsbitTrace = {Recording, instruction, hex, coreId};
})();

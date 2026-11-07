import { describe, it, expect } from 'vitest';
import { wizardStart, wizardBegin, wizardSent, wizardOnStatus } from './wizard';
import type { StatusJson } from '../types';

function st(o: {
  state?: 'idle' | 'busy' | 'holding';
  uptime?: number | null;
  ph4_ready?: boolean;
  mseq?: number;
  cseq?: number;
  ph4?: number;
  ph7?: number;
} = {}): StatusJson {
  return {
    title: 't', interval_min: 10, uptime_s: o.uptime === undefined ? 500 : o.uptime, free_heap: 1, count: 0,
    last_reading: null,
    averages: { ph: [null, null], ec: [null, null], wt: [null, null] },
    cycle: {
      state: o.state ?? 'idle', step: '', job: '', warmup_elapsed_ms: null, warmup_ms: null,
      samples: null, sample_total: null, ph4_ready: o.ph4_ready ?? false, pending_ph4_adc: null,
    },
    manual: { seq: o.mseq ?? 0, ph: null, ec: null, wt: null },
    calibration: { seq: o.cseq ?? 0, ph4_adc: o.ph4 ?? 2368, ph7_adc: o.ph7 ?? 1880 },
    diag: { last_ph_adc: null, ph_samples: [], last_ec_adc: null },
    api: { queued: 0, last_http: null, failures: 0, delivered: 0 },
  };
}

/** Calibrate wizard that has seen holding, i.e. in `ready`. */
function calReady() {
  let w = wizardBegin(wizardStart('calibrate', st()));
  w = wizardOnStatus(w, st({ state: 'holding' }));
  expect(w.step).toBe('ready');
  return w;
}

describe('wizard', () => {
  it('start captures seq, uptime and old values', () => {
    const w = wizardStart('calibrate', st({ cseq: 4, uptime: 77, ph4: 2000, ph7: 1500 }));
    expect(w).toMatchObject({ kind: 'calibrate', step: 'intro', startSeq: 4, startUptime: 77, oldPh4: 2000, oldPh7: 1500 });
  });

  it('starting becomes ready when holding', () => {
    expect(calReady().step).toBe('ready');
  });

  it('restart_during_calibration_cancels', () => {
    let w = wizardStart('calibrate', st({ uptime: 500 }));
    w = wizardBegin(w);
    w = wizardOnStatus(w, st({ state: 'idle', uptime: 3 }));
    expect(w.step).toBe('cancelled');
    expect(w.message).toBe('Calibration cancelled');
  });

  it('restart_during_measure_says_measurement', () => {
    const w = wizardOnStatus(wizardBegin(wizardStart('measure', st({ uptime: 500 }))), st({ uptime: 3 }));
    expect(w.message).toBe('Measurement cancelled');
  });

  it('unknown_previous_uptime_is_not_a_restart', () => {
    let w = wizardBegin(wizardStart('calibrate', st({ uptime: null })));
    w = wizardOnStatus(w, st({ state: 'holding', uptime: 3 }));
    expect(w.step).toBe('ready');
  });

  it('ph4_completes_when_ready', () => {
    let w = wizardSent(calReady());
    expect(w.step).toBe('ph4');
    w = wizardOnStatus(w, st({ state: 'busy' }));
    expect(w.step).toBe('ph4');
    w = wizardOnStatus(w, st({ state: 'holding', ph4_ready: true }));
    expect(w.step).toBe('ph4done');
  });

  it('calibration_done_when_seq_increments', () => {
    let w = wizardSent(wizardOnStatus(wizardSent(calReady()), st({ state: 'holding', ph4_ready: true })));
    expect(w.step).toBe('ph7');
    w = wizardOnStatus(w, st({ state: 'busy' }));
    w = wizardOnStatus(w, st({ state: 'idle', cseq: 1 }));
    expect(w.step).toBe('done');
  });

  it('measure_done_when_manual_seq_increments', () => {
    let w = wizardOnStatus(wizardBegin(wizardStart('measure', st())), st({ state: 'holding' }));
    expect(w.step).toBe('ready');
    w = wizardSent(w);
    w = wizardOnStatus(w, st({ state: 'busy' }));
    w = wizardOnStatus(w, st({ state: 'idle', mseq: 1 }));
    expect(w.step).toBe('done');
  });

  it('seq_wins_over_idle_in_ready', () => {
    const w = wizardOnStatus(wizardOnStatus(wizardBegin(wizardStart('measure', st())), st({ state: 'holding' })), st({ state: 'idle', mseq: 1 }));
    expect(w.step).toBe('done');
  });

  it('idle_mid_wizard_cancels', () => {
    const w = wizardOnStatus(calReady(), st({ state: 'idle' }));
    expect(w.step).toBe('cancelled');
    expect(w.message).toBe('Calibration cancelled');
    const w2 = wizardOnStatus(wizardOnStatus(wizardSent(calReady()), st({ state: 'holding', ph4_ready: true })), st({ state: 'idle' }));
    expect(w2.step).toBe('cancelled');
  });

  it('stale_idle_after_send_is_ignored', () => {
    let w = wizardBegin(wizardStart('calibrate', st()));
    w = wizardOnStatus(w, st({ state: 'idle' }));
    expect(w.step).toBe('starting');
    w = wizardOnStatus(w, st({ state: 'holding' }));
    w = wizardSent(w);
    w = wizardOnStatus(w, st({ state: 'idle' }));
    expect(w.step).toBe('ph4');
    w = wizardOnStatus(w, st({ state: 'holding', ph4_ready: true }));
    w = wizardSent(w);
    w = wizardOnStatus(w, st({ state: 'idle' }));
    expect(w.step).toBe('ph7');
    w = wizardOnStatus(w, st({ state: 'idle', cseq: 1 }));
    expect(w.step).toBe('done');
  });

  it('persistent_idle_after_send_eventually_cancels', () => {
    let w = wizardSent(calReady());
    for (let i = 0; i < 3; i++) w = wizardOnStatus(w, st({ state: 'idle' }));
    expect(w.step).toBe('cancelled');
  });
});

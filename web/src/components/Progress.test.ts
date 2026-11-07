import { describe, expect, it } from 'vitest';
import { progressInfo } from './Progress';
import { sourceText } from './ReviewPanel';
import type { CycleStatus, PiReportBody } from '../types';

const base: CycleStatus = { state: 'busy', step: 'warmup', job: 'reading', warmup_elapsed_ms: 12000, warmup_ms: 20000, samples: 0, sample_total: 20, ph4_ready: false, pending_ph4_adc: null };

describe('progressInfo', () => {
  it('labels warmup', () => expect(progressInfo(base)?.label).toBe('Warming pH probe 12/20 s'));
  it('labels sampling', () => expect(progressInfo({ ...base, step: 'sampling', samples: 7 })?.label).toBe('Sampling 7/20'));
  it('is null when idle', () => expect(progressInfo({ ...base, state: 'idle' })).toBeNull());
});

describe('sourceText', () => {
  const r = { source: 'model', model: 'qwen2.5:1.5b', duration_s: 52.2 } as PiReportBody;
  it('names model and duration', () => expect(sourceText(r)).toBe('qwen2.5:1.5b · 52 s'));
  it('says template', () => expect(sourceText({ ...r, source: 'template', model: null })).toBe('template'));
});

import type { MetricRange, Ranges } from '../types';

export const BUILTIN_RANGES: Ranges = {
  ph: { valid: [2, 10], target: [5.5, 6.5] },
  ec: { valid: [0, 5000], target: [200, 1000] },
  temp: { valid: [30, 99], target: [60, 80] },
};

export type Level = 'ok' | 'warn' | 'fault';

export function levelFor(value: number | null, r: MetricRange): Level {
  if (value === null || Number.isNaN(value)) return 'fault';
  if (value < r.valid[0] || value > r.valid[1]) return 'fault';
  if (value < r.target[0] || value > r.target[1]) return 'warn';
  return 'ok';
}

export function tagsFor(
  metric: 'ph' | 'ec' | 'temp',
  flags: { id: string }[],
): ('drifting' | 'noisy')[] {
  const out: ('drifting' | 'noisy')[] = [];
  if (flags.some((f) => f.id === `${metric}_drift`)) out.push('drifting');
  if (flags.some((f) => f.id === `${metric}_noisy`)) out.push('noisy');
  return out;
}

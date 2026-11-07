import type { MetricRange } from '../types';

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

import type { CycleStatus } from '../types';

export interface ProgressStatus {
  cycle: CycleStatus;
}

export function progressInfo(cycle: CycleStatus): { label: string; frac: number | null } | null {
  if (cycle.state !== 'busy') return null;
  if (cycle.step === 'warmup') {
    const done = Math.round((cycle.warmup_elapsed_ms ?? 0) / 1000);
    const total = Math.round((cycle.warmup_ms ?? 0) / 1000);
    return { label: `Warming pH probe ${done}/${total} s`, frac: total > 0 ? done / total : null };
  }
  if (cycle.step === 'sampling') {
    const n = cycle.samples ?? 0;
    const total = cycle.sample_total ?? 0;
    return { label: `Sampling ${n}/${total}`, frac: total > 0 ? n / total : null };
  }
  if (cycle.step === 'restarting') return { label: 'Restarting', frac: null };
  return { label: 'Working', frac: null };
}

export function Progress({ status }: { status: ProgressStatus }) {
  const p = progressInfo(status.cycle);
  if (!p) return null;
  const pct = p.frac === null ? 100 : Math.max(0, Math.min(1, p.frac)) * 100;
  return (
    <div class="progress" role="status">
      <div class="progress-label">{p.label}</div>
      <div class={'progress-bar' + (p.frac === null ? ' indeterminate' : '')}>
        <div class="progress-fill" style={{ width: `${pct}%` }} />
      </div>
    </div>
  );
}

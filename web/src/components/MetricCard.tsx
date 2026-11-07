import type { Level } from '../logic/status';
import { DASH, fmt, slope } from '../logic/format';

export interface MetricCardProps {
  label: string;
  unit: string;
  slopeUnit: string;
  digits: number;
  value: number | null;
  mean: number | null;
  spread: number | null;
  slopePerHour: number | null;
  level: Level;
  tags: string[];
  extra?: string | null;
}

export function MetricCard(p: MetricCardProps) {
  return (
    <div class={`card level-${p.level}`}>
      <div class="card-label">
        {p.label}
        {p.unit && <span class="unit"> {p.unit}</span>}
      </div>
      {p.level === 'fault' ? (
        <div class="card-value">
          Sensor fault <small>{p.value === null ? DASH : fmt(p.value, p.digits)}</small>
        </div>
      ) : (
        <div class="card-value">{fmt(p.value, p.digits)}</div>
      )}
      {p.extra && <div class="card-line">{p.extra}</div>}
      <div class="card-line">
        Avg {fmt(p.mean, p.digits)} ± {fmt(p.spread, p.digits)}
      </div>
      <div class="card-line">24 h: {slope(p.slopePerHour, p.slopeUnit)}</div>
      {p.tags.length > 0 && (
        <div class="tags">
          {p.tags.map((t) => (
            <span class="tag" key={t}>
              {t}
            </span>
          ))}
        </div>
      )}
    </div>
  );
}

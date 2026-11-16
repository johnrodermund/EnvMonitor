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
  /** Show the average and 24 h trend lines (Appearance → Dashboard tiles) */
  details?: boolean;
}

const LEVEL_TEXT: Record<Level, string> = { ok: 'In range', warn: 'Off target', fault: 'Sensor fault' };

/** One reading: a large value in body-text colour, with the range status as a stripe and a badge. */
export function MetricCard(p: MetricCardProps) {
  const details = p.details ?? true;
  return (
    <div class={`card level-${p.level}`}>
      <div class="card-head">
        <div class="card-label">{p.label}</div>
        <span class={`level-badge level-${p.level}`}>{LEVEL_TEXT[p.level]}</span>
      </div>
      <div class="card-value">
        {p.level === 'fault' && p.value === null ? DASH : fmt(p.value, p.digits)}
        {p.unit && <small class="unit"> {p.unit}</small>}
        {p.extra && <small> {p.extra}</small>}
      </div>
      {details && (
        <>
          <div class="card-line">
            Average <strong>{fmt(p.mean, p.digits)}</strong> ± {fmt(p.spread, p.digits)}
          </div>
          <div class="card-line">
            24 h trend <strong>{slope(p.slopePerHour, p.slopeUnit)}</strong>
          </div>
        </>
      )}
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

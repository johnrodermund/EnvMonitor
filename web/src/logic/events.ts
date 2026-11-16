import type { EventKind, TankEvent } from '../types';

const KIND_LABELS: Record<EventKind, string> = {
  topup: 'Top-up',
  feeding: 'Feeding',
  buffer: 'Buffer',
  water_change: 'Water change',
  other: 'Other',
};

export const EVENT_KINDS = Object.keys(KIND_LABELS) as EventKind[];

export function kindLabel(kind: EventKind): string {
  return KIND_LABELS[kind] ?? kind;
}

/** Event times are UTC ISO strings; epoch seconds, or null when unreadable. */
export function eventSeconds(e: TankEvent): number | null {
  const ms = Date.parse(e.occurred_at);
  return Number.isNaN(ms) ? null : ms / 1000;
}

/** Events with startSec <= time <= endSec. */
export function eventsInWindow(events: TankEvent[], startSec: number, endSec: number): TankEvent[] {
  return events.filter((e) => {
    const t = eventSeconds(e);
    return t !== null && t >= startSec && t <= endSec;
  });
}

export function eventLabel(e: TankEvent): string {
  const base = kindLabel(e.kind);
  if (e.kind !== 'topup' || !e.detail) return base;
  return `${base} (${e.detail === 'di' ? 'DI' : e.detail})`;
}

/** After minus before, or null when either side is missing. */
export function eventDelta(e: TankEvent, metric: 'ec' | 'ph'): number | null {
  const before = metric === 'ec' ? e.ec_before : e.ph_before;
  const after = metric === 'ec' ? e.ec_after : e.ph_after;
  return before === null || after === null ? null : after - before;
}

/** color is a CSS custom property name; suggested (unreviewed) events are dashed. */
export function markerStyle(e: TankEvent): { color: string; dashed: boolean } {
  const color = e.kind === 'topup' ? '--event-topup' : e.kind === 'feeding' ? '--event-feeding' : '--event-other';
  return { color, dashed: e.status === 'suggested' };
}

const pad = (n: number) => String(n).padStart(2, '0');

/** An ISO time as the "YYYY-MM-DDTHH:MM" a datetime-local input shows, in the browser's zone. */
export function toLocalInput(iso: string): string {
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return '';
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

/** A datetime-local value as ISO with the browser's offset, which the device needs; null when empty or bad. */
export function fromLocalInput(value: string): string | null {
  const m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})$/.exec(value);
  if (!m) return null;
  const d = new Date(Number(m[1]), Number(m[2]) - 1, Number(m[3]), Number(m[4]), Number(m[5]));
  if (Number.isNaN(d.getTime())) return null;
  const off = -d.getTimezoneOffset();
  const sign = off >= 0 ? '+' : '-';
  const abs = Math.abs(off);
  return `${m[1]}-${m[2]}-${m[3]}T${m[4]}:${m[5]}:00${sign}${pad(Math.floor(abs / 60))}:${pad(abs % 60)}`;
}

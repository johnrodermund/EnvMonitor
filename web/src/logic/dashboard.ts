import { DASH } from './format';
import type { PiEntry, StatusJson } from '../types';

export function clock(d: Date): string {
  return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
}

export function nextReading(time: string | null, intervalMin: number | null): string {
  if (!time || !intervalMin) return DASH;
  const m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})/.exec(time);
  if (!m) return DASH;
  return clock(new Date(+m[1], +m[2] - 1, +m[3], +m[4], +m[5] + intervalMin));
}

/** The single line the review panel shows when the Pi data is stale, missing or empty. */
export function reviewLine<T>(entry: PiEntry<T> | null, now: number): string | null {
  if (!entry) return 'No Pi data yet';
  if (entry.none) return 'No reviews yet';
  if (!entry.body) return 'No Pi data yet';
  if (entry.error) {
    const when = entry.age_s === null ? DASH : clock(new Date(now - entry.age_s * 1000));
    return `Pi data from ${when} · ${entry.error}`;
  }
  return null;
}

/** False shows "No readings yet"; a fault value still counts as a reading and gets a red card. */
export function hasReadings(s: Pick<StatusJson, 'last_reading' | 'count'>): boolean {
  return !!s.last_reading && s.last_reading.time !== null && s.count !== 0;
}

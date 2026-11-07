import { describe, expect, it } from 'vitest';
import { reportLocalTime } from './format';
import { clock, hasReadings, nextReading, reviewLine } from './dashboard';
import type { PiEntry, PiReportBody } from '../types';

const body = { headline: 'x' } as PiReportBody;
const entry = (p: Partial<PiEntry<PiReportBody>>): PiEntry<PiReportBody> => ({ age_s: 0, error: null, none: false, body, ...p });

describe('nextReading', () => {
  it('adds the interval', () => expect(nextReading('2026-11-06T09:40:00', 10)).toBe('09:50'));
  it('crosses the hour', () => expect(nextReading('2026-11-06T09:55:00', 10)).toBe('10:05'));
  it('is a dash without data', () => {
    expect(nextReading(null, 10)).toBe('—');
    expect(nextReading('2026-11-06T09:40:00', null)).toBe('—');
  });
});

describe('reportLocalTime', () => {
  it('shows local time in the localTime format', () => {
    const iso = '2026-11-06T13:00:12Z';
    const d = new Date(iso);
    const p = (n: number) => String(n).padStart(2, '0');
    expect(reportLocalTime(iso)).toBe(`${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`);
  });
  it('is a dash for garbage', () => expect(reportLocalTime('nope')).toBe('—'));
});

describe('reviewLine', () => {
  const now = new Date(2026, 10, 6, 15, 0).getTime();
  it('stale with a body shows time and error as given', () => {
    const l = reviewLine(entry({ age_s: 5400, error: 'Pi unreachable: timed out' }), now);
    expect(l).toBe(`Pi data from ${clock(new Date(now - 5400000))} · Pi unreachable: timed out`);
  });
  it('null body, not none', () => expect(reviewLine(entry({ body: null }), now)).toBe('No Pi data yet'));
  it('none', () => expect(reviewLine(entry({ body: null, none: true }), now)).toBe('No reviews yet'));
  it('fresh has no line', () => expect(reviewLine(entry({}), now)).toBeNull());
  it('null entry', () => expect(reviewLine(null, now)).toBe('No Pi data yet'));
});

describe('hasReadings', () => {
  const s = (last: unknown, count: number | null) => ({ last_reading: last, count }) as never;
  it('false with no last reading', () => expect(hasReadings(s(null, 0))).toBe(false));
  it('false with count 0', () => expect(hasReadings(s({ time: 'x' }, 0))).toBe(false));
  it('true for a fault reading, so the red card shows', () => expect(hasReadings(s({ time: '2026-11-06T09:40:00', ph: 15.6 }, 5))).toBe(true));
});

import type { MetricRange, Ranges } from '../types';

// Mirrors src/ranges.h and validateSettings in src/api_json.h, so the form can explain a problem before the device does.
const LIMIT = 100000;
const METRICS = ['ph', 'ec', 'temp'] as const;
export type MetricKey = (typeof METRICS)[number];
/** Form fields per metric, as text: valid low, valid high, target low, target high. */
export type RangesText = Record<MetricKey, string[]>;

const round3 = (v: number) => Math.fround(Math.round(v * 1000) / 1000);

function metricError(name: string, m: MetricRange): string | null {
  const values = [...m.valid, ...m.target];
  if (values.some((v) => !Number.isFinite(v))) return `${name}: values must be finite numbers`;
  if (values.some((v) => v < -LIMIT || v > LIMIT)) return `${name}: values must be between -100000 and 100000`;
  const [vl, vh, tl, th] = values.map((v) => round3(Math.fround(v)));
  if (!(vl < vh)) return `${name}: valid low must be below valid high`;
  if (!(tl < th)) return `${name}: target low must be below target high`;
  if (tl < vl || th > vh) return `${name}: target must sit inside valid`;
  return null;
}

export function validateRangesForm(r: Ranges): string | null {
  for (const k of METRICS) {
    const e = metricError(k, r[k]);
    if (e) return e;
  }
  return null;
}

const num = (s: string) => (s.trim() === '' ? NaN : Number(s));

export function parseRangesForm(t: RangesText): Ranges {
  const one = (f: string[]): MetricRange => ({ valid: [num(f[0]), num(f[1])], target: [num(f[2]), num(f[3])] });
  return { ph: one(t.ph), ec: one(t.ec), temp: one(t.temp) };
}

export function rangesToText(r: Ranges): RangesText {
  const one = (m: MetricRange) => [...m.valid, ...m.target].map(String);
  return { ph: one(r.ph), ec: one(r.ec), temp: one(r.temp) };
}

export function validateInterval(s: string): string | null {
  const n = Number(s);
  return /^\d{1,4}$/.test(s) && n >= 2 && n <= 720 ? null : 'interval must be a whole number of minutes from 2 to 720';
}

export function validateTitle(t: string): string | null {
  const bytes = new TextEncoder().encode(t);
  const bad = bytes.length < 1 || bytes.length > 40 || bytes.some((b) => b < 0x20);
  return bad ? 'title must be 1-40 characters with no control characters' : null;
}

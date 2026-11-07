import type { Row } from '../types';

export function toCsv(rows: Row[]): string {
  let out = 'time,ph,ec_uS_cm,water_temp_F\n';
  for (const r of rows) {
    out += r.map((v) => (v === null ? '' : String(v))).join(',') + '\n';
  }
  return out;
}

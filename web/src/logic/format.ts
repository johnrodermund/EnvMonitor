export const DASH = '—';

export function fmt(value: number | null, digits: number): string {
  if (value === null || Number.isNaN(value)) return DASH;
  return value.toFixed(digits);
}

export function ago(seconds: number | null): string {
  if (seconds === null || Number.isNaN(seconds) || seconds < 0) return DASH;
  if (seconds < 60) return 'just now';
  if (seconds < 3600) return `${Math.floor(seconds / 60)} min ago`;
  return `${Math.floor(seconds / 3600)} h ago`;
}

export function slope(perHour: number | null, unit: string): string {
  if (perHour === null || Number.isNaN(perHour)) return DASH;
  if (Math.abs(perHour) < 0.005) return `0.00 ${unit}/h →`;
  const mag = Math.abs(perHour).toFixed(Math.abs(perHour) >= 1 ? 1 : 2);
  return perHour > 0 ? `+${mag} ${unit}/h ↑` : `−${mag} ${unit}/h ↓`;
}

const pad2 = (n: number) => String(n).padStart(2, '0');

/** "14:05", or "2:05 PM" in 12 h format. */
export function hm(hours: number, minutes: number, format: '24' | '12' = '24'): string {
  if (format === '24') return `${pad2(hours)}:${pad2(minutes)}`;
  return `${hours % 12 || 12}:${pad2(minutes)} ${hours < 12 ? 'AM' : 'PM'}`;
}

export function clock(d: Date, format: '24' | '12' = '24'): string {
  return hm(d.getHours(), d.getMinutes(), format);
}

/** Device times are local ISO strings with no zone; show them as "YYYY-MM-DD HH:MM". */
export function localTime(iso: string | null, format: '24' | '12' = '24'): string {
  if (!iso) return DASH;
  const m = /^(\d{4}-\d{2}-\d{2})T(\d{2}):(\d{2})/.exec(iso);
  return m ? `${m[1]} ${hm(Number(m[2]), Number(m[3]), format)}` : iso;
}

/** Pi report and event times are UTC ISO strings; show them as local "YYYY-MM-DD HH:MM". */
export function reportLocalTime(iso: string, format: '24' | '12' = '24'): string {
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return DASH;
  return `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())} ${clock(d, format)}`;
}

/** EC unit label: the device refers EC to 25 C whenever its temperature coefficient isn't 0. */
export function ecUnit(ecTempCoef: number | null | undefined): string {
  return ecTempCoef ? 'µS/cm at 25 °C' : 'µS/cm';
}

/** The EC temperature coefficient, % per °C: 0 (off) to 5. */
export function validateEcTempCoef(text: string): string | null {
  const v = Number(text);
  return text.trim() === '' || !Number.isFinite(v) || v < 0 || v > 5 ? 'Enter a number from 0 to 5' : null;
}

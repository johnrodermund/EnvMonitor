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

/** Device times are local ISO strings with no zone; show them as "YYYY-MM-DD HH:MM". */
export function localTime(iso: string | null): string {
  if (!iso) return DASH;
  const m = /^(\d{4}-\d{2}-\d{2})T(\d{2}:\d{2})/.exec(iso);
  return m ? `${m[1]} ${m[2]}` : iso;
}

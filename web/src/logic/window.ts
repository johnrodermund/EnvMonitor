import type { Row } from '../types';

/** Parse a device time (local ISO, no zone) to epoch seconds, or null. */
export function rowSeconds(t: string | null): number | null {
  if (!t) return null;
  const ms = new Date(t).getTime();
  return Number.isNaN(ms) ? null : ms / 1000;
}

/**
 * Drop rows with no time and keep those within `hours` of the newest row.
 * Device time has no zone, so the window is relative to the newest row, never the browser clock.
 * `_nowMs` is accepted for interface stability and ignored.
 */
export function windowRows(rows: Row[], hours: number | null, _nowMs: number): Row[] {
  const timed = rows.filter((r) => rowSeconds(r[0]) !== null);
  if (hours === null || timed.length === 0) return timed;
  let newest = -Infinity;
  for (const r of timed) newest = Math.max(newest, rowSeconds(r[0]) as number);
  const cutoff = newest - hours * 3600;
  return timed.filter((r) => (rowSeconds(r[0]) as number) >= cutoff);
}

/** True when the x scale shows less than the full data extent. */
export function isZoomed(min: number | null | undefined, max: number | null | undefined, dataMin: number | undefined, dataMax: number | undefined): boolean {
  if (min == null || max == null || dataMin === undefined || dataMax === undefined) return false;
  return min > dataMin || max < dataMax;
}

/**
 * uPlot's setData(d, false) skips the redraw, so a zoomed chart needs an explicit redraw;
 * an unzoomed chart resets scales so the x range extends to new readings.
 */
export function nextDataMode(zoomed: boolean): { resetScales: boolean; redraw: boolean } {
  return zoomed ? { resetScales: false, redraw: true } : { resetScales: true, redraw: false };
}

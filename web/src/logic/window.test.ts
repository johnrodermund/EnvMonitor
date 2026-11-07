import { describe, it, expect } from 'vitest';
import { windowRows } from './window';
import type { Row } from '../types';

const rows: Row[] = [
  ['2026-11-05T00:00:00', 6, 600, 70],
  [null, 6, 600, 70],
  ['2026-11-05T18:00:00', 6, 600, 70],
  ['2026-11-05T23:59:00', 6, 600, 70],
  ['2026-11-06T00:00:00', 6, 600, 70],
];

describe('windowRows', () => {
  it('keeps rows within the window of the newest row, not the browser clock', () => {
    const out = windowRows(rows, 6, 0);
    expect(out.map((r) => r[0])).toEqual(['2026-11-05T18:00:00', '2026-11-05T23:59:00', '2026-11-06T00:00:00']);
  });
  it('drops null-time rows and keeps all with null hours', () => {
    expect(windowRows(rows, null, 0)).toHaveLength(4);
  });
  it('returns empty for no rows', () => {
    expect(windowRows([], 6, 0)).toEqual([]);
  });
});

import { isZoomed, nextDataMode } from './window';

describe('zoom handling', () => {
  it('detects zoom against the data extent', () => {
    expect(isZoomed(100, 200, 100, 200)).toBe(false);
    expect(isZoomed(120, 200, 100, 200)).toBe(true);
    expect(isZoomed(100, 150, 100, 200)).toBe(true);
    expect(isZoomed(100, 200, undefined, undefined)).toBe(false);
  });
  it('resets scales when not zoomed, keeps them and redraws when zoomed', () => {
    expect(nextDataMode(false)).toEqual({ resetScales: true, redraw: false });
    expect(nextDataMode(true)).toEqual({ resetScales: false, redraw: true });
  });
});

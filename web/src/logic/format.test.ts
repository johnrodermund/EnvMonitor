import { describe, it, expect } from 'vitest';
import { fmt, ago, slope, localTime } from './format';

describe('format', () => {
  it('fmt', () => {
    expect(fmt(null, 2)).toBe('—');
    expect(fmt(6.234, 1)).toBe('6.2');
  });
  it('slope', () => {
    expect(slope(0.0234, 'pH')).toBe('+0.02 pH/h ↑');
    expect(slope(0.001, 'pH')).toBe('0.00 pH/h →');
    expect(slope(-1.5, 'µS/cm')).toBe('−1.5 µS/cm/h ↓');
  });
  it('ago', () => {
    expect(ago(30)).toBe('just now');
    expect(ago(300)).toBe('5 min ago');
    expect(ago(10800)).toBe('3 h ago');
    expect(ago(null)).toBe('—');
  });
  it('localTime', () => {
    expect(localTime('2026-11-04T16:33:06')).toBe('2026-11-04 16:33');
    expect(localTime(null)).toBe('—');
  });
});

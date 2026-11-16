import { describe, it, expect } from 'vitest';
import { fmt, ago, slope, localTime, ecUnit, validateEcTempCoef } from './format';

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
  it('ecUnit says when EC is referred to 25 C', () => {
    expect(ecUnit(2)).toBe('µS/cm at 25 °C');
    expect(ecUnit(0)).toBe('µS/cm');
    expect(ecUnit(null)).toBe('µS/cm');
    expect(ecUnit(undefined)).toBe('µS/cm');
  });
  it('validateEcTempCoef takes 0 to 5', () => {
    expect(validateEcTempCoef('1.67')).toBeNull();
    expect(validateEcTempCoef('0')).toBeNull();
    expect(validateEcTempCoef('5')).toBeNull();
    expect(validateEcTempCoef('5.1')).toBe('Enter a number from 0 to 5');
    expect(validateEcTempCoef('-1')).toBe('Enter a number from 0 to 5');
    expect(validateEcTempCoef('')).toBe('Enter a number from 0 to 5');
    expect(validateEcTempCoef('x')).toBe('Enter a number from 0 to 5');
  });
});

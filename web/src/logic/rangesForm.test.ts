import { describe, it, expect } from 'vitest';
import { BUILTIN_RANGES } from './status';
import { parseRangesForm, validateRangesForm, validateInterval, validateTitle } from './rangesForm';
import type { Ranges } from '../types';

const clone = (): Ranges => JSON.parse(JSON.stringify(BUILTIN_RANGES));

describe('validateRangesForm', () => {
  it('accepts the built-in ranges', () => {
    expect(validateRangesForm(BUILTIN_RANGES)).toBeNull();
  });
  it('rejects a target outside valid', () => {
    const r = clone();
    r.ph.target = [1, 6.5];
    expect(validateRangesForm(r)).toBe('ph: target must sit inside valid');
  });
  it('rejects reversed ranges', () => {
    const r = clone();
    r.ec.valid = [5000, 0];
    expect(validateRangesForm(r)).toBe('ec: valid low must be below valid high');
    const t = clone();
    t.temp.target = [80, 60];
    expect(validateRangesForm(t)).toBe('temp: target low must be below target high');
  });
  it('rejects non-finite and out-of-limit values', () => {
    const r = clone();
    r.ph.valid = [NaN, 10];
    expect(validateRangesForm(r)).toBe('ph: values must be finite numbers');
    const s = clone();
    s.ec.valid = [0, 100001];
    expect(validateRangesForm(s)).toBe('ec: values must be between -100000 and 100000');
  });
  it('compares values rounded to 3 decimals', () => {
    const r = clone();
    r.ph.valid = [2, 2.0004];
    r.ph.target = [2, 2.0004];
    expect(validateRangesForm(r)).toBe('ph: valid low must be below valid high');
  });
});

describe('parseRangesForm', () => {
  const text = (r: Ranges) => ({
    ph: [...r.ph.valid, ...r.ph.target].map(String),
    ec: [...r.ec.valid, ...r.ec.target].map(String),
    temp: [...r.temp.valid, ...r.temp.target].map(String),
  });
  it('parses text fields, empty becomes NaN', () => {
    expect(parseRangesForm(text(BUILTIN_RANGES))).toEqual(BUILTIN_RANGES);
    const t = text(BUILTIN_RANGES);
    t.ph[0] = ' ';
    expect(validateRangesForm(parseRangesForm(t))).toBe('ph: values must be finite numbers');
  });
});

describe('settings validation', () => {
  it('interval is a whole number from 2 to 720', () => {
    expect(validateInterval('10')).toBeNull();
    expect(validateInterval('2')).toBeNull();
    expect(validateInterval('720')).toBeNull();
    for (const bad of ['1', '721', '2.5', '', 'abc'])
      expect(validateInterval(bad)).toBe('interval must be a whole number of minutes from 2 to 720');
  });
  it('title is 1-40 bytes with no control characters', () => {
    expect(validateTitle('Reservoir')).toBeNull();
    expect(validateTitle('a'.repeat(40))).toBeNull();
    const msg = 'title must be 1-40 characters with no control characters';
    expect(validateTitle('')).toBe(msg);
    expect(validateTitle('a'.repeat(41))).toBe(msg);
    expect(validateTitle('é'.repeat(21))).toBe(msg);
    expect(validateTitle('a\nb')).toBe(msg);
  });
});

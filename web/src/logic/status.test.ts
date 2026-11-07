import { describe, it, expect } from 'vitest';
import { levelFor, tagsFor } from './status';

const ph = { valid: [0, 14] as [number, number], target: [5.5, 6.5] as [number, number] };

describe('levelFor', () => {
  it('classifies values', () => {
    expect(levelFor(6.0, ph)).toBe('ok');
    expect(levelFor(5.5, ph)).toBe('ok');
    expect(levelFor(7.0, ph)).toBe('warn');
    expect(levelFor(15.6, ph)).toBe('fault');
    expect(levelFor(null, ph)).toBe('fault');
    expect(levelFor(NaN, ph)).toBe('fault');
  });
});

describe('tagsFor', () => {
  const flags = [{ id: 'ph_drift' }, { id: 'temp_noisy' }];
  it('matches by metric', () => {
    expect(tagsFor('ph', flags)).toEqual(['drifting']);
    expect(tagsFor('temp', flags)).toEqual(['noisy']);
    expect(tagsFor('ec', flags)).toEqual([]);
  });
});

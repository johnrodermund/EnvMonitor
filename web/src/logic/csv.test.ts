import { describe, it, expect } from 'vitest';
import { toCsv } from './csv';

describe('toCsv', () => {
  it('writes header and empty cells for nulls', () => {
    expect(toCsv([['2026-11-04T16:33:06', 6.2, null, 75.2]])).toBe(
      'time,ph,ec_uS_cm,water_temp_F\n2026-11-04T16:33:06,6.2,,75.2\n',
    );
  });
});

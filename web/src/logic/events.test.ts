import { describe, expect, it } from 'vitest';
import { eventDelta, eventLabel, eventSeconds, eventsInWindow, kindLabel, LOGGABLE_KINDS, markersFor, markerStyle, rowActions, toLocalInput, fromLocalInput } from './events';
import type { TankEvent } from '../types';

function ev(p: Partial<TankEvent>): TankEvent {
  return {
    seq: 1,
    occurred_at: '2026-11-05T12:00:00Z',
    kind: 'topup',
    detail: 'unknown',
    source: 'detected',
    status: 'suggested',
    ec_before: 250,
    ec_after: 244,
    ph_before: 6.5,
    ph_after: 6.62,
    note: '',
    backed_up: true,
    refused: false,
    ...p,
  };
}

const at = (iso: string) => Date.parse(iso) / 1000;

describe('events logic', () => {
  it('keeps only events inside the window', () => {
    const events = [
      ev({ seq: 1, occurred_at: '2026-11-05T11:59:59Z' }),
      ev({ seq: 2, occurred_at: '2026-11-05T12:00:00Z' }),
      ev({ seq: 3, occurred_at: '2026-11-05T18:00:00Z' }),
      ev({ seq: 4, occurred_at: '2026-11-05T18:00:01Z' }),
      ev({ seq: 5, occurred_at: 'garbage' }),
    ];
    const kept = eventsInWindow(events, at('2026-11-05T12:00:00Z'), at('2026-11-05T18:00:00Z'));
    expect(kept.map((e) => e.seq)).toEqual([2, 3]);
    expect(eventSeconds(events[1])).toBe(at('2026-11-05T12:00:00Z'));
    expect(eventSeconds(events[4])).toBeNull();
  });

  it('labels top-up detail and feeding', () => {
    expect(eventLabel(ev({ detail: 'di' }))).toBe('Top-up (DI)');
    expect(eventLabel(ev({ detail: 'tap' }))).toBe('Top-up (tap)');
    expect(eventLabel(ev({ detail: 'unknown' }))).toBe('Top-up (unknown)');
    expect(eventLabel(ev({ detail: null }))).toBe('Top-up');
    expect(eventLabel(ev({ kind: 'feeding', detail: null }))).toBe('Feeding');
    expect(eventLabel(ev({ kind: 'buffer', detail: null }))).toBe('Buffer');
    expect(eventLabel(ev({ kind: 'water_change', detail: null }))).toBe('Water change');
    expect(eventLabel(ev({ kind: 'other', detail: null }))).toBe('Other');
  });

  it('delta is after minus before, and null when a side is missing', () => {
    expect(eventDelta(ev({}), 'ec')).toBeCloseTo(-6);
    expect(eventDelta(ev({}), 'ph')).toBeCloseTo(0.12);
    expect(eventDelta(ev({ ec_after: null }), 'ec')).toBeNull();
    expect(eventDelta(ev({ ph_before: null }), 'ph')).toBeNull();
  });

  it('suggested events are dashed', () => {
    expect(markerStyle(ev({ status: 'suggested' })).dashed).toBe(true);
    expect(markerStyle(ev({ status: 'confirmed' })).dashed).toBe(false);
    expect(markerStyle(ev({ kind: 'topup' })).color).toBe('--event-topup');
    expect(markerStyle(ev({ kind: 'feeding' })).color).toBe('--event-feeding');
    expect(markerStyle(ev({ kind: 'buffer' })).color).toBe('--event-other');
  });

  it('round-trips a datetime-local value as an ISO time with the browser offset', () => {
    const iso = fromLocalInput('2026-11-05T14:03');
    expect(iso).toMatch(/^2026-11-05T14:03:00[+-]\d\d:\d\d$/);
    expect(Date.parse(iso as string)).toBe(new Date(2026, 10, 5, 14, 3).getTime());
    expect(fromLocalInput('')).toBeNull();
    expect(toLocalInput(new Date(2026, 10, 5, 14, 3, 59).toISOString())).toBe('2026-11-05T14:03');
  });

  it('offers reject on every detected event that is not rejected, delete only on manual ones', () => {
    expect(rowActions(ev({ status: 'suggested' }))).toEqual(['confirm_di', 'confirm_tap', 'reject', 'edit']);
    expect(rowActions(ev({ kind: 'feeding', detail: null, status: 'suggested' }))).toEqual(['confirm', 'reject', 'edit']);
    expect(rowActions(ev({ status: 'confirmed' }))).toEqual(['reject', 'edit']);
    expect(rowActions(ev({ status: 'rejected' }))).toEqual(['confirm', 'edit']);
    expect(rowActions(ev({ source: 'manual', status: 'confirmed' }))).toEqual(['delete', 'edit']);
  });
});

describe('pH swing events', () => {
  it('labels the new kinds', () => {
    expect(kindLabel('ph_swing')).toBe('pH swing');
    expect(kindLabel('nutrients')).toBe('Nutrients');
    expect(eventLabel(ev({ kind: 'ph_swing', detail: null }))).toBe('pH swing');
  });

  it('offers every kind but pH swing in the Log event form', () => {
    expect(LOGGABLE_KINDS).toContain('nutrients');
    expect(LOGGABLE_KINDS).toContain('topup');
    expect(LOGGABLE_KINDS).not.toContain('ph_swing');
  });

  it('marks pH swings on the pH chart only, and drops rejected events', () => {
    const events = [
      ev({ seq: 1, kind: 'ph_swing', status: 'confirmed', detail: null }),
      ev({ seq: 2, kind: 'topup' }),
      ev({ seq: 3, kind: 'buffer', status: 'rejected' }),
      ev({ seq: 4, kind: 'nutrients', detail: null }),
    ];
    expect(markersFor(events, 'ph').map((e) => e.seq)).toEqual([1, 2, 4]);
    expect(markersFor(events, 'ec').map((e) => e.seq)).toEqual([2, 4]);
  });

  it('draws the swing marker thin, in its own colour, never dashed', () => {
    const style = markerStyle(ev({ kind: 'ph_swing', status: 'suggested' }));
    expect(style).toEqual({ color: '--event-swing', dashed: false, thin: true });
    expect(markerStyle(ev({ kind: 'topup' })).thin).toBe(false);
  });
});

import { useContext, useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { getEvents, getRanges, getReadings } from '../api';
import { MetricChart } from '../components/MetricChart';
import { markersFor } from '../logic/events';
import { toCsv } from '../logic/csv';
import { ecUnit } from '../logic/format';
import { BUILTIN_RANGES } from '../logic/status';
import { rowSeconds, windowRows } from '../logic/window';
import { StatusContext } from '../status-context';
import { usePoll } from '../usePoll';
import type { Row } from '../types';
import { AppearanceContext } from '../appearance-context';
import { chartHours, displayTemp, tempUnitLabel } from '../logic/appearance';

const WINDOWS: { label: string; hours: number | null }[] = [
  { label: '6 h', hours: 6 },
  { label: '24 h', hours: 24 },
  { label: 'All', hours: null },
];

function today(): string {
  const d = new Date();
  const p = (n: number) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}`;
}

const column = (rows: Row[], i: 1 | 2 | 3) => rows.map((r) => r[i]);

export function Charts() {
  const { data: status } = useContext(StatusContext);
  const readings = usePoll(getReadings, 600000);
  const rangesPoll = usePoll(getRanges, 60000);
  const eventsPoll = usePoll(getEvents, 60000);
  const { appearance } = useContext(AppearanceContext);
  const [hours, setHours] = useState<number | null>(chartHours(appearance.chart_window));
  // The device's appearance can arrive after the page opens; follow it until the owner picks a window here
  const picked = useRef(false);
  useEffect(() => {
    if (!picked.current) setHours(chartHours(appearance.chart_window));
  }, [appearance.chart_window]);
  const unit = appearance.temp_unit;

  const count = status?.count ?? null;
  const lastTime = status?.last_reading?.time ?? null;
  const mounted = useRef(false);
  useEffect(() => {
    // usePoll already fetches on mount; only refresh on later changes.
    if (!mounted.current) {
      mounted.current = true;
      return;
    }
    readings.refresh();
  }, [count, lastTime]);

  const all = readings.data;
  const ranges = rangesPoll.data?.ranges ?? BUILTIN_RANGES;
  const rows = useMemo(() => windowRows(all ?? [], hours, Date.now()), [all, hours]);
  const times = useMemo(() => rows.map((r) => rowSeconds(r[0]) as number), [rows]);
  const ph = useMemo(() => column(rows, 1), [rows]);
  const ec = useMemo(() => column(rows, 2), [rows]);
  const wt = useMemo(() => column(rows, 3).map((v) => displayTemp(v, unit)), [rows, unit]);
  const tempTarget = useMemo(
    () => ranges.temp.target.map((v) => displayTemp(v, unit) as number) as [number, number],
    [ranges.temp.target[0], ranges.temp.target[1], unit],
  );
  // Rejected events are kept in the table but not marked on the charts; pH swings mark the pH chart only
  const phMarkers = useMemo(() => markersFor(eventsPoll.data?.events ?? [], 'ph'), [eventsPoll.data]);
  const ecMarkers = useMemo(() => markersFor(eventsPoll.data?.events ?? [], 'ec'), [eventsPoll.data]);


  const download = () => {
    const blob = new Blob([toCsv(all ?? [])], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `envmonitor-${today()}.csv`;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  };

  if (!all) {
    return (
      <div>
        <h1>Charts</h1>
        <p class="muted">{readings.error ? 'Could not load readings' : 'Loading…'}</p>
      </div>
    );
  }

  return (
    <div>
      <h1>Charts</h1>
      {rows.length === 0 ? (
        <p class="empty">No readings yet</p>
      ) : (
        <>
          <div class="actions chart-bar">
            {WINDOWS.map((w) => (
              <button key={w.label} class={hours === w.hours ? 'primary' : ''} aria-pressed={hours === w.hours} onClick={() => {
                picked.current = true;
                setHours(w.hours);
              }}>
                {w.label}
              </button>
            ))}
            <button onClick={download}>Download CSV</button>
          </div>
          {!rangesPoll.data && rangesPoll.error && <p class="warn-text small">Using built-in ranges (device ranges unavailable)</p>}
          <p class="muted small">
            Shaded band is the target range; dots outside it are marked. Vertical lines are events (dashed: not yet reviewed; thin on pH: the daily pH swing). Drag to zoom,
            double-click to reset.
          </p>
          <p class="muted small">
            Events are listed on the <a href="#/events">Events</a> tab.
          </p>
          <div class="chart-wrap" key={`${hours}-${appearance.theme}-${appearance.accent}-${appearance.time_format}-${unit}`}>
            <MetricChart title="pH" unit="" digits={2} times={times} values={ph} target={ranges.ph.target} events={phMarkers} timeFormat={appearance.time_format} />
            <MetricChart title="EC" unit={ecUnit(status?.ec_temp_coef)} digits={0} times={times} values={ec} target={ranges.ec.target} events={ecMarkers} timeFormat={appearance.time_format} />
            <MetricChart title="Temp" unit={tempUnitLabel(unit)} digits={1} times={times} values={wt} target={tempTarget} timeFormat={appearance.time_format} />
          </div>
        </>
      )}
    </div>
  );
}

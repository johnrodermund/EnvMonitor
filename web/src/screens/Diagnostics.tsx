import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';
import { useContext, useEffect, useRef } from 'preact/hooks';
import { getPi } from '../api';
import { ago, DASH } from '../logic/format';
import { StatusContext } from '../status-context';
import { usePoll } from '../usePoll';
import type { Num, PiEntry } from '../types';

const raw = (v: Num | undefined) => (v === null || v === undefined ? DASH : String(Math.round(v)));

function uptime(s: Num | undefined): string {
  if (s === null || s === undefined) return DASH;
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  return d ? `${d} d ${h} h` : h ? `${h} h ${m} min` : `${m} min`;
}

const H = 140;

function SampleChart({ samples }: { samples: Num[] }) {
  const host = useRef<HTMLDivElement>(null);
  const chart = useRef<uPlot | null>(null);
  const data = (): uPlot.AlignedData => [samples.map((_, i) => i + 1), samples];
  const latest = useRef(data);
  latest.current = data;

  useEffect(() => {
    const el = host.current;
    if (!el) return;
    const s = getComputedStyle(document.documentElement);
    const muted = s.getPropertyValue('--muted').trim() || '#888';
    const border = s.getPropertyValue('--border').trim() || '#ccc';
    const axis = { stroke: muted, grid: { stroke: border, width: 1 }, ticks: { stroke: border } };
    chart.current = new uPlot(
      {
        width: el.clientWidth || 300,
        height: H,
        legend: { show: false },
        cursor: { show: false },
        scales: { x: { time: false } },
        axes: [axis, { ...axis, size: 48 }],
        series: [{}, { stroke: s.getPropertyValue('--accent').trim() || '#1f6f4a', width: 2, points: { size: 5 } }],
      },
      latest.current(),
      el,
    );
    const ro = new ResizeObserver(() => el.clientWidth > 0 && chart.current?.setSize({ width: el.clientWidth, height: H }));
    ro.observe(el);
    return () => {
      ro.disconnect();
      chart.current?.destroy();
      chart.current = null;
    };
  }, []);

  useEffect(() => {
    chart.current?.setData(latest.current());
  }, [samples]);
  return (
    <div class="chart">
      <div ref={host} aria-label="pH ADC samples chart" />
    </div>
  );
}

function cacheLine(name: string, e: PiEntry<unknown> | undefined) {
  return (
    <li key={name}>
      {name}: {!e ? DASH : e.none ? 'never fetched' : `fetched ${ago(e.age_s)}`}
      {e?.error && <span class="err"> · {e.error}</span>}
    </li>
  );
}

export function Diagnostics() {
  const { data: s, error } = useContext(StatusContext);
  const pi = usePoll(getPi, 60000);
  if (!s) return <p class="muted">{error ? 'Device not responding' : 'Loading…'}</p>;
  const samples = s.diag.ph_samples ?? [];

  return (
    <div>
      <h1>Diagnostics</h1>
      <section class="panel">
        <h2>Calibration (raw ADC)</h2>
        <p>
          pH 4: <strong>{raw(s.calibration.ph4_adc)}</strong> · pH 7: <strong>{raw(s.calibration.ph7_adc)}</strong>
        </p>
      </section>
      <section class="panel">
        <h2>Last readings (raw ADC)</h2>
        <p>
          pH average: <strong>{raw(s.diag.last_ph_adc)}</strong> · EC: <strong>{raw(s.diag.last_ec_adc)}</strong>
        </p>
        {samples.length > 0 ? <SampleChart samples={samples} /> : <p class="muted small">No pH samples yet</p>}
      </section>
      <section class="panel">
        <h2>Device</h2>
        <p>
          Free heap: <strong>{s.free_heap === null ? DASH : `${Math.round(s.free_heap / 1024)} KB`}</strong> · Uptime:{' '}
          <strong>{uptime(s.uptime_s)}</strong>
        </p>
      </section>
      <section class="panel">
        <h2>Pi cache</h2>
        {pi.data ? (
          <ul class="flags">
            {cacheLine('Stats', pi.data.stats)}
            {cacheLine('Review', pi.data.report)}
            {cacheLine('Thresholds', pi.data.thresholds)}
          </ul>
        ) : (
          <p class="muted small">{pi.error ? pi.error.message : 'Loading…'}</p>
        )}
      </section>
      <p>
        <a href="/diag">Open /diag</a>
      </p>
    </div>
  );
}

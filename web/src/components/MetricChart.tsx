import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';
import { useEffect, useRef } from 'preact/hooks';
import { eventSeconds, markerStyle } from '../logic/events';
import { isZoomed, nextDataMode } from '../logic/window';
import type { TankEvent } from '../types';

export interface MetricChartProps {
  title: string;
  unit: string;
  digits: number;
  times: number[];
  values: (number | null)[];
  target: [number, number];
  /** Drawn as vertical markers; suggested ones dashed. Never changes the scales. */
  events?: TankEvent[];
}

const HEIGHT = 190;

function css(name: string): string {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || '#888';
}

function outsideOf(values: (number | null)[], [lo, hi]: [number, number]): (number | null)[] {
  return values.map((v) => (v !== null && (v < lo || v > hi) ? v : null));
}

function drawEvents(u: uPlot, events: TankEvent[] | undefined) {
  if (!events?.length) return;
  const { ctx } = u;
  const min = u.scales.x.min ?? -Infinity;
  const max = u.scales.x.max ?? Infinity;
  const top = u.bbox.top;
  const bottom = u.bbox.top + u.bbox.height;
  const dpr = devicePixelRatio || 1;
  ctx.save();
  ctx.lineWidth = 2 * dpr;
  for (const e of events) {
    const t = eventSeconds(e);
    if (t === null || t < min || t > max) continue;
    const x = Math.round(u.valToPos(t, 'x', true));
    const style = markerStyle(e);
    ctx.strokeStyle = css(style.color);
    ctx.fillStyle = ctx.strokeStyle;
    ctx.setLineDash(style.dashed ? [5 * dpr, 4 * dpr] : []);
    ctx.beginPath();
    ctx.moveTo(x, top);
    ctx.lineTo(x, bottom);
    ctx.stroke();
    // A small flag at the top so a marker is visible over the line
    ctx.beginPath();
    ctx.moveTo(x - 4 * dpr, top);
    ctx.lineTo(x + 4 * dpr, top);
    ctx.lineTo(x, top + 6 * dpr);
    ctx.closePath();
    ctx.fill();
  }
  ctx.restore();
}

function build(el: HTMLElement, latest: { current: MetricChartProps }, width: number, zoomed: { current: boolean }): uPlot {
  const p = latest.current;
  const text = css('--muted');
  const grid = css('--border');
  const accent = css('--accent');
  const warn = css('--warn');
  const ok = css('--ok');
  const [lo, hi] = p.target;
  const fmtV = (_u: uPlot, v: number | null) => (v === null ? '—' : v.toFixed(p.digits));
  const axis = { stroke: text, grid: { stroke: grid, width: 1 }, ticks: { stroke: grid } };
  const opts: uPlot.Options = {
    width,
    height: HEIGHT,
    cursor: { sync: { key: 'env', scales: ['x', null] }, drag: { x: true, y: false } },
    scales: {
      x: { time: true },
      y: {
        // Keep the whole target band in view.
        range: (_u, min, max) => {
          const a = Math.min(min ?? lo, lo);
          const b = Math.max(max ?? hi, hi);
          const pad = (b - a || 1) * 0.08;
          return [a - pad, b + pad];
        },
      },
    },
    axes: [axis, { ...axis, size: 56, label: p.unit ? `${p.title} (${p.unit})` : p.title, labelSize: 18 }],
    series: [
      { label: 'Time' },
      { label: p.title, stroke: accent, width: 2, points: { show: false }, value: fmtV },
      {
        label: 'Outside target',
        stroke: warn,
        paths: () => null,
        points: { show: true, size: 7, fill: warn, stroke: warn },
        value: fmtV,
      },
    ],
    hooks: {
      setScale: [
        (u, key) => {
          if (key !== 'x') return;
          const xs = u.data[0];
          zoomed.current = isZoomed(u.scales.x.min, u.scales.x.max, xs[0], xs[xs.length - 1]);
        },
      ],
      init: [
        (u) => {
          // Hide the helper series from the legend; its marks still draw.
          const rows = u.root.querySelectorAll('.u-series');
          (rows[2] as HTMLElement | undefined)?.style.setProperty('display', 'none');
        },
      ],
      draw: [(u) => drawEvents(u, latest.current.events)],
      drawClear: [
        (u) => {
          const { ctx } = u;
          const top = u.valToPos(hi, 'y', true);
          const bot = u.valToPos(lo, 'y', true);
          ctx.save();
          ctx.globalAlpha = 0.16;
          ctx.fillStyle = ok;
          ctx.fillRect(u.bbox.left, top, u.bbox.width, bot - top);
          ctx.restore();
        },
      ],
    },
  };
  return new uPlot(opts, [p.times, p.values, outsideOf(p.values, p.target)], el);
}

export function MetricChart(props: MetricChartProps) {
  const host = useRef<HTMLDivElement>(null);
  const chart = useRef<uPlot | null>(null);
  const zoomed = useRef(false);
  const latest = useRef(props);
  latest.current = props;
  const targetKey = props.target.join(',');

  // Recreate on target or theme change; zoom survives plain data refreshes.
  useEffect(() => {
    const el = host.current;
    if (!el) return;
    const make = () => {
      chart.current?.destroy();
      chart.current = build(el, latest, el.clientWidth || 300, zoomed);
    };
    make();
    const mq = window.matchMedia('(prefers-color-scheme: dark)');
    mq.addEventListener('change', make);
    const ro = new ResizeObserver(() => {
      if (chart.current && el.clientWidth > 0) chart.current.setSize({ width: el.clientWidth, height: HEIGHT });
    });
    ro.observe(el);
    return () => {
      mq.removeEventListener('change', make);
      ro.disconnect();
      chart.current?.destroy();
      chart.current = null;
    };
  }, [targetKey]);

  useEffect(() => {
    const u = chart.current;
    if (!u) return;
    const mode = nextDataMode(zoomed.current);
    u.setData([props.times, props.values, outsideOf(props.values, props.target)], mode.resetScales);
    if (mode.redraw) u.redraw();
  }, [props.times, props.values]);

  useEffect(() => {
    chart.current?.redraw(false, false);
  }, [props.events]);

  // The padding lives on the outer box so the host's width is the drawable width.
  return (
    <div class="chart">
      <div ref={host} aria-label={`${props.title} chart`} />
    </div>
  );
}

import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';
import { useEffect, useRef } from 'preact/hooks';

export interface MetricChartProps {
  title: string;
  unit: string;
  digits: number;
  times: number[];
  values: (number | null)[];
  target: [number, number];
}

const HEIGHT = 190;

function css(name: string): string {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || '#888';
}

function outsideOf(values: (number | null)[], [lo, hi]: [number, number]): (number | null)[] {
  return values.map((v) => (v !== null && (v < lo || v > hi) ? v : null));
}

function build(el: HTMLElement, p: MetricChartProps, width: number): uPlot {
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
      init: [
        (u) => {
          // Hide the helper series from the legend; its marks still draw.
          const rows = u.root.querySelectorAll('.u-series');
          (rows[2] as HTMLElement | undefined)?.style.setProperty('display', 'none');
        },
      ],
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
  const latest = useRef(props);
  latest.current = props;
  const targetKey = props.target.join(',');

  // Recreate on target or theme change; zoom survives plain data refreshes.
  useEffect(() => {
    const el = host.current;
    if (!el) return;
    const make = () => {
      chart.current?.destroy();
      chart.current = build(el, latest.current, el.clientWidth || 300);
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
    chart.current?.setData([props.times, props.values, outsideOf(props.values, props.target)], false);
  }, [props.times, props.values]);

  return <div class="chart" ref={host} aria-label={`${props.title} chart`} />;
}

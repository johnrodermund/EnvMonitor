export type Num = number | null;

export interface LastReading {
  time: string | null;
  ph: Num;
  ph_pm: Num;
  ec: Num;
  wt: Num;
}

export type MeanStd = [Num, Num];

export interface CycleStatus {
  state: 'idle' | 'busy' | 'holding';
  step: string;
  job: string;
  warmup_elapsed_ms: Num;
  warmup_ms: Num;
  samples: Num;
  sample_total: Num;
  ph4_ready: boolean;
  pending_ph4_adc: Num;
}

export interface StatusJson {
  title: string;
  interval_min: Num;
  uptime_s: Num;
  free_heap: Num;
  count: Num;
  last_reading: LastReading | null;
  averages: { ph: MeanStd; ec: MeanStd; wt: MeanStd };
  cycle: CycleStatus;
  manual: { seq: Num; ph: Num; ec: Num; wt: Num };
  calibration: { seq: Num; ph4_adc: Num; ph7_adc: Num };
  diag: { last_ph_adc: Num; ph_samples: Num[]; last_ec_adc: Num };
  api: { queued: Num; last_http: Num; failures: Num; delivered: Num };
}

/** [time, ph, ec, water temp F], oldest first. */
export type Row = [string | null, number | null, number | null, number | null];

export interface PiEntry<T> {
  age_s: number | null;
  error: string | null;
  none: boolean;
  body: T | null;
}

export interface PiFlag {
  id: string;
  evidence: unknown;
}

export type Severity = 'ok' | 'watch' | 'act';

export interface MetricStats {
  latest: Num;
  mean: Num;
  std: Num;
  min: Num;
  max: Num;
  slope_per_hour: Num;
  count: Num;
}

export interface PiStatsBody {
  device: string;
  window: string;
  start: string;
  end: string;
  stats: { ph: MetricStats; ec: MetricStats; water_temp_f: MetricStats };
  flags: PiFlag[];
  severity: Severity;
}

export interface PiReportBody {
  id: number | string;
  device: string;
  created_at: string;
  window_start: string;
  window_end: string;
  severity: Severity;
  flags: PiFlag[];
  headline: string;
  summary: string;
  source: 'model' | 'template';
  model: string | null;
  duration_s: number | null;
}

export interface MetricRange {
  valid: [number, number];
  target: [number, number];
}

export interface Ranges {
  ph: MetricRange;
  ec: MetricRange;
  temp: MetricRange;
}

export interface RangesResponse {
  source: 'device' | 'pi' | 'builtin';
  ranges: Ranges;
  pi: Ranges | null;
}

export interface PiResponse {
  stats: PiEntry<PiStatsBody>;
  report: PiEntry<PiReportBody>;
  thresholds: PiEntry<Record<string, unknown>>;
}

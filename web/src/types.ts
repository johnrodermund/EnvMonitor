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
  /** % per °C; EC values are referred to 25 °C when not 0. Absent on older firmware. */
  ec_temp_coef?: Num;
  count: Num;
  last_reading: LastReading | null;
  averages: { ph: MeanStd; ec: MeanStd; wt: MeanStd };
  cycle: CycleStatus;
  manual: { seq: Num; ph: Num; ec: Num; wt: Num };
  calibration: { seq: Num; ph4_adc: Num; ph7_adc: Num };
  diag: { last_ph_adc: Num; ph_samples: Num[]; last_ec_adc: Num; last_warmup_ms?: Num };
  api: { queued: Num; last_http: Num; failures: Num; delivered: Num };
  events?: EventsStatus;
}

export interface EventsStatus {
  count: Num;
  unsynced: Num;
  dropped_unsynced: Num;
  clock_ahead: Num;
  /** Events whose current version the Pi refused (400/422) */
  refused: Num;
  last_sync_http: Num;
}

export type EventKind = 'topup' | 'feeding' | 'buffer' | 'water_change' | 'other';
export type EventDetail = 'di' | 'tap' | 'unknown';
export type EventStatus = 'suggested' | 'confirmed' | 'rejected';

/** One tank event as GET /api/events returns it. occurred_at is UTC ISO. */
export interface TankEvent {
  seq: number;
  occurred_at: string;
  kind: EventKind;
  detail: EventDetail | null;
  source: 'detected' | 'manual';
  status: EventStatus;
  ec_before: Num;
  ec_after: Num;
  ph_before: Num;
  ph_after: Num;
  note: string;
  backed_up: boolean;
  /** The Pi refused this version; editing it tries again. */
  refused: boolean;
}

export interface EventsResponse {
  events: TankEvent[];
  /** The device left out its oldest events to keep the reply small */
  truncated?: boolean;
}

/** POST /api/events fields. occurred_at is ISO with an offset. A field left out stays unchanged. */
export interface EventChange {
  op: 'create' | 'update' | 'delete' | 'reject' | 'confirm';
  seq?: number;
  kind?: EventKind;
  detail?: EventDetail | '';
  occurred_at?: string;
  note?: string;
  status?: EventStatus;
}

export interface DeviceSettings {
  interval_min: number;
  title: string;
  topup_min_drop?: number;
  feeding_min_rise?: number;
  feeding_enabled?: boolean;
  ec_temp_coef?: number;
  theme?: string;
  accent?: string;
  text_size?: string;
  temp_unit?: string;
  time_format?: string;
  chart_window?: string;
  tile_details?: boolean;
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

export type CommandName =
  | 'reading'
  | 'measure_start'
  | 'measure'
  | 'calibrate_start'
  | 'calibrate_ph4'
  | 'calibrate_ph7'
  | 'cancel'
  | 'restart'
  | 'reset';

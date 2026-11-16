import type { CommandName, DeviceSettings, EventChange, EventsResponse, PiResponse, Ranges, RangesResponse, Row, StatusJson } from './types';

export class ApiError extends Error {
  status: number;
  constructor(status: number, message: string) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
  }
}

const TIMEOUT_MS = 10000;
const FORM = { 'Content-Type': 'application/x-www-form-urlencoded' };

async function call(method: string, path: string, form?: Record<string, string>): Promise<unknown> {
  const init: RequestInit = { method };
  if (form) {
    init.headers = FORM;
    init.body = new URLSearchParams(form).toString();
  }
  const ctl = new AbortController();
  init.signal = ctl.signal;
  let timedOut = false;
  const timer = setTimeout(() => {
    timedOut = true;
    ctl.abort();
  }, TIMEOUT_MS);
  let res: Response;
  let text: string;
  try {
    res = await fetch(path, init);
    text = await res.text().catch(() => '');
  } catch (e) {
    throw new ApiError(0, timedOut ? 'timed out' : e instanceof Error ? e.message : 'network error');
  } finally {
    clearTimeout(timer);
  }
  let body: unknown = null;
  try {
    body = text ? JSON.parse(text) : null;
  } catch {
    body = null;
  }
  if (res.ok && method === 'GET' && body === null) throw new ApiError(res.status, 'bad response');
  if (!res.ok) {
    const msg =
      body && typeof body === 'object' && 'error' in body && typeof (body as { error: unknown }).error === 'string'
        ? (body as { error: string }).error
        : res.statusText || `HTTP ${res.status}`;
    throw new ApiError(res.status, msg);
  }
  return body;
}

const get = <T>(path: string) => call('GET', path) as Promise<T>;
const post = async (path: string, form: Record<string, string>) => {
  await call('POST', path, form);
};

export const getStatus = () => get<StatusJson>('/api/status');
export const getReadings = () => get<Row[]>('/api/readings');
export const getPi = () => get<PiResponse>('/api/pi');
export const getRanges = () => get<RangesResponse>('/api/ranges');
export const getSettings = () => get<DeviceSettings>('/api/settings');
export const getEvents = () => get<EventsResponse>('/api/events');

/** Creates, edits, confirms, rejects or deletes an event; resolves to its seq. */
export const postEvent = async (change: EventChange): Promise<number> => {
  const f: Record<string, string> = {};
  for (const [k, v] of Object.entries(change)) if (v !== undefined) f[k] = String(v);
  const body = (await call('POST', '/api/events', f)) as { seq?: number } | null;
  return body?.seq ?? 0;
};
export const getApiConfig = () => get<{ url: string; device: string; token_set: boolean }>('/api/apiconfig');

export const saveSettings = (p: {
  interval?: number;
  title?: string;
  topup_min_drop?: number;
  feeding_min_rise?: number;
  feeding_enabled?: boolean;
}) => {
  const f: Record<string, string> = {};
  if (p.interval !== undefined) f.interval = String(p.interval);
  if (p.title !== undefined) f.title = p.title;
  if (p.topup_min_drop !== undefined) f.topup_min_drop = String(p.topup_min_drop);
  if (p.feeding_min_rise !== undefined) f.feeding_min_rise = String(p.feeding_min_rise);
  if (p.feeding_enabled !== undefined) f.feeding_enabled = String(p.feeding_enabled);
  return post('/api/settings', f);
};

export const command = (cmd: CommandName) => post('/api/command', { cmd });

export const saveRanges = (r: Ranges) =>
  post('/api/ranges', {
    ph_valid_lo: String(r.ph.valid[0]),
    ph_valid_hi: String(r.ph.valid[1]),
    ph_target_lo: String(r.ph.target[0]),
    ph_target_hi: String(r.ph.target[1]),
    ec_valid_lo: String(r.ec.valid[0]),
    ec_valid_hi: String(r.ec.valid[1]),
    ec_target_lo: String(r.ec.target[0]),
    ec_target_hi: String(r.ec.target[1]),
    temp_valid_lo: String(r.temp.valid[0]),
    temp_valid_hi: String(r.temp.valid[1]),
    temp_target_lo: String(r.temp.target[0]),
    temp_target_hi: String(r.temp.target[1]),
  });

export const resetRanges = async () => {
  await call('DELETE', '/api/ranges');
};

export const saveApiConfig = (p: { url?: string; device?: string; token?: string }) => {
  const f: Record<string, string> = {};
  if (p.url !== undefined) f.url = p.url;
  if (p.device !== undefined) f.device = p.device;
  if (p.token !== undefined) f.token = p.token;
  return post('/api/apiconfig', f);
};

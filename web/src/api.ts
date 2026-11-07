import type { CommandName, PiResponse, Ranges, RangesResponse, Row, StatusJson } from './types';

export class ApiError extends Error {
  status: number;
  constructor(status: number, message: string) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
  }
}

const FORM = { 'Content-Type': 'application/x-www-form-urlencoded' };

async function call(method: string, path: string, form?: Record<string, string>): Promise<unknown> {
  const init: RequestInit = { method };
  if (form) {
    init.headers = FORM;
    init.body = new URLSearchParams(form).toString();
  }
  let res: Response;
  try {
    res = await fetch(path, init);
  } catch (e) {
    throw new ApiError(0, e instanceof Error ? e.message : 'network error');
  }
  const text = await res.text().catch(() => '');
  let body: unknown = null;
  try {
    body = text ? JSON.parse(text) : null;
  } catch {
    body = null;
  }
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
export const getSettings = () => get<{ interval_min: number; title: string }>('/api/settings');
export const getApiConfig = () => get<{ url: string; device: string; token_set: boolean }>('/api/apiconfig');

export const saveSettings = (p: { interval?: number; title?: string }) => {
  const f: Record<string, string> = {};
  if (p.interval !== undefined) f.interval = String(p.interval);
  if (p.title !== undefined) f.title = p.title;
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

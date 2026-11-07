import type { Plugin } from 'vite';
import statusIdle from './fixtures/status-idle.json';
import statusBusy from './fixtures/status-busy.json';
import statusHolding from './fixtures/status-holding.json';
import statusFault from './fixtures/status-fault.json';
import statusEmpty from './fixtures/status-empty.json';
import readings from './fixtures/readings.json';
import readingsEmpty from './fixtures/readings-empty.json';
import pi from './fixtures/pi.json';
import piNone from './fixtures/pi-none.json';
import piStale from './fixtures/pi-stale.json';
import piFault from './fixtures/pi-fault.json';
import ranges from './fixtures/ranges.json';

type Json = unknown;
// @types/node is not installed, so describe the little of the Node request used here.
interface Req {
  url?: string;
  method?: string;
  headers: { cookie?: string };
  on(ev: 'data', cb: (chunk: { toString(): string }) => void): void;
  on(ev: 'end', cb: () => void): void;
}
const STATUS: Record<string, Json> = {
  idle: statusIdle, busy: statusBusy, holding: statusHolding, fault: statusFault,
  empty: statusEmpty, nopi: statusIdle, stale: statusIdle,
};

let settings = { interval_min: 10, title: 'Reservoir' };
let apiConfig = { url: 'http://192.168.12.230:8000', device: 'envmonitor-1', token_set: true };
let busyUntil = 0;
// Small stateful wizard model for the idle scenario: busy for 3 s, then the next state.
const wiz = { state: 'idle', ph4_ready: false, pending: null as number | null, mseq: 0, cseq: 0, ph4: 2368, ph7: 1880, endAt: 0, onEnd: (): void => {} };
function wizTick() {
  if (wiz.endAt && Date.now() >= wiz.endAt) {
    wiz.endAt = 0;
    wiz.onEnd();
  }
}
function wizStatus() {
  wizTick();
  const s = JSON.parse(JSON.stringify(statusIdle));
  s.cycle.state = wiz.endAt ? 'busy' : wiz.state;
  if (wiz.endAt) {
    s.cycle.step = 'sampling';
    s.cycle.samples = 5;
    s.cycle.sample_total = 20;
  }
  s.cycle.ph4_ready = wiz.ph4_ready;
  s.cycle.pending_ph4_adc = wiz.pending;
  s.manual = { seq: wiz.mseq, ph: wiz.mseq ? 6.12 : null, ec: wiz.mseq ? 701 : null, wt: wiz.mseq ? 70.4 : null };
  s.calibration = { seq: wiz.cseq, ph4_adc: wiz.ph4, ph7_adc: wiz.ph7 };
  return s;
}
function wizCommand(cmd: string): boolean {
  wizTick();
  const run = (end: () => void) => {
    wiz.endAt = Date.now() + 3000;
    wiz.onEnd = end;
  };
  if (cmd === 'measure_start' || cmd === 'calibrate_start') {
    wiz.state = 'holding';
    wiz.ph4_ready = false;
  } else if (cmd === 'measure') {
    run(() => { wiz.mseq++; wiz.state = 'idle'; });
  } else if (cmd === 'calibrate_ph4') {
    run(() => { wiz.ph4_ready = true; wiz.pending = 2300; wiz.state = 'holding'; });
  } else if (cmd === 'calibrate_ph7') {
    run(() => { wiz.cseq++; wiz.ph4 = 2300; wiz.ph7 = 1850; wiz.state = 'idle'; });
  } else if (cmd === 'cancel') {
    wiz.endAt = 0;
    wiz.state = 'idle';
    wiz.ph4_ready = false;
  } else return false;
  return true;
}

function scenarioOf(cookie: string | undefined): string {
  const m = /(?:^|;\s*)scenario=([a-z]+)/.exec(cookie ?? '');
  return m ? m[1] : 'idle';
}

export function mockApi(): Plugin {
  return {
    name: 'envmonitor-mock-api',
    apply: 'serve',
    configureServer(server) {
      server.middlewares.use((rawReq, res, next) => {
        const req = rawReq as unknown as Req;
        const path = (req.url ?? '').split('?')[0];
        if (!path.startsWith('/api/')) return next();
        const scenario = scenarioOf(req.headers.cookie);
        const method = req.method ?? 'GET';
        const send = (code: number, body: Json) => {
          res.statusCode = code;
          res.setHeader('Content-Type', 'application/json');
          res.end(JSON.stringify(body));
        };
        if (scenario === 'offline') return send(503, { error: 'offline' });

        const finish = (form: URLSearchParams) => {
          if (method === 'GET') {
            if (path === '/api/status') {
              if (scenario === 'idle') return send(200, Date.now() < busyUntil ? statusBusy : wizStatus());
              return send(200, STATUS[scenario] ?? statusIdle);
            }
            if (path === '/api/readings') return send(200, scenario === 'empty' ? readingsEmpty : readings);
            if (path === '/api/pi') {
              if (scenario === 'nopi') return send(200, piNone);
              if (scenario === 'stale') return send(200, piStale);
              return send(200, scenario === 'fault' ? piFault : pi);
            }
            if (path === '/api/ranges') return send(200, ranges);
            if (path === '/api/settings') return send(200, settings);
            if (path === '/api/apiconfig') return send(200, apiConfig);
            return send(404, { error: 'not found' });
          }
          if (method === 'POST') {
            if (path === '/api/command') {
              // The real device accepts cancel while busy.
              if (form.get('cmd') === 'cancel') {
                if (scenario === 'idle') wizCommand('cancel');
                return send(202, { ok: true });
              }
              if (scenario === 'busy' || Date.now() < busyUntil || (scenario === 'idle' && wiz.endAt && Date.now() < wiz.endAt)) return send(409, { error: 'busy: warmup' });
              if (scenario === 'idle' && wizCommand(form.get('cmd') ?? '')) return send(202, { ok: true });
              if (scenario === 'idle') busyUntil = Date.now() + 5000;
              return send(202, { ok: true });
            }
            if (path === '/api/settings') {
              if (form.has('interval')) settings.interval_min = Number(form.get('interval'));
              if (form.has('title')) settings.title = form.get('title') ?? '';
              return send(200, { ok: true });
            }
            if (path === '/api/apiconfig') {
              apiConfig = {
                url: form.get('url') ?? apiConfig.url,
                device: form.get('device') ?? apiConfig.device,
                token_set: apiConfig.token_set || !!form.get('token'),
              };
              return send(200, { ok: true });
            }
            if (path === '/api/ranges') return send(200, { ok: true });
          }
          if (method === 'DELETE' && path === '/api/ranges') return send(200, { ok: true });
          return send(404, { error: 'not found' });
        };

        if (method === 'POST') {
          let raw = '';
          req.on('data', (c) => (raw += c.toString()));
          req.on('end', () => finish(new URLSearchParams(raw)));
        } else {
          finish(new URLSearchParams());
        }
      });
    },
  };
}

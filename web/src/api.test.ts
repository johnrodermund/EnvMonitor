import { afterEach, describe, expect, it, vi } from 'vitest';
import { ApiError, command, getStatus } from './api';

afterEach(() => vi.unstubAllGlobals());

describe('api', () => {
  it('posts a command as form data', async () => {
    const f = vi.fn().mockResolvedValue(new Response('{"ok":true}', { status: 202 }));
    vi.stubGlobal('fetch', f);
    await command('reading');
    const [url, init] = f.mock.calls[0];
    expect(url).toBe('/api/command');
    expect(init.method).toBe('POST');
    expect(init.headers['Content-Type']).toBe('application/x-www-form-urlencoded');
    expect(String(init.body)).toBe('cmd=reading');
  });

  it('throws ApiError with the device message', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('{"error":"busy: warmup"}', { status: 409 })));
    await expect(command('reading')).rejects.toMatchObject({ status: 409, message: 'busy: warmup' });
    await expect(command('reading')).rejects.toBeInstanceOf(ApiError);
  });

  it('throws ApiError status 0 on network failure', async () => {
    vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new TypeError('Failed to fetch')));
    await expect(command('reading')).rejects.toMatchObject({ status: 0 });
  });

  it('times out after 10 s with status 0', async () => {
    vi.useFakeTimers();
    vi.stubGlobal('fetch', vi.fn((_u: string, init: RequestInit) =>
      new Promise((_r, rej) => init.signal!.addEventListener('abort', () => rej(new DOMException('aborted', 'AbortError'))))));
    const p = getStatus();
    const check = expect(p).rejects.toMatchObject({ status: 0, message: 'timed out' });
    await vi.advanceTimersByTimeAsync(10000);
    await check;
    vi.useRealTimers();
  });

  it('rejects a 2xx GET with a non-JSON body as bad response', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('<html>', { status: 200 })));
    await expect(getStatus()).rejects.toMatchObject({ status: 200, message: 'bad response' });
  });
});

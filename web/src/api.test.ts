import { afterEach, describe, expect, it, vi } from 'vitest';
import { ApiError, command } from './api';

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
});

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { ApiError } from './api';
import { createPoller } from './poller';

beforeEach(() => vi.useFakeTimers());
afterEach(() => vi.useRealTimers());

const flush = () => vi.advanceTimersByTimeAsync(0);

describe('createPoller', () => {
  it('polls immediately, then every interval', async () => {
    const fn = vi.fn().mockResolvedValue(1);
    const onUpdate = vi.fn();
    const p = createPoller(fn, 1000, onUpdate);
    expect(fn).toHaveBeenCalledTimes(1);
    await flush();
    expect(onUpdate).toHaveBeenLastCalledWith(expect.objectContaining({ data: 1, error: null }));
    await vi.advanceTimersByTimeAsync(1000);
    expect(fn).toHaveBeenCalledTimes(2);
    p.stop();
  });

  it('keeps the last good data when a poll fails', async () => {
    const fn = vi.fn().mockResolvedValueOnce('ok').mockRejectedValueOnce(new ApiError(503, 'offline'));
    const onUpdate = vi.fn();
    const p = createPoller(fn, 1000, onUpdate);
    await flush();
    await vi.advanceTimersByTimeAsync(1000);
    const last = onUpdate.mock.calls[onUpdate.mock.calls.length - 1][0];
    expect(last.data).toBe('ok');
    expect(last.error).toMatchObject({ status: 503 });
    expect(last.at).not.toBeNull();
    p.stop();
  });

  it('stop cancels the timer and ignores a late resolve', async () => {
    let resolve!: (v: number) => void;
    const fn = vi.fn().mockReturnValue(new Promise<number>((r) => (resolve = r)));
    const onUpdate = vi.fn();
    const p = createPoller(fn, 1000, onUpdate);
    p.stop();
    resolve(5);
    await flush();
    await vi.advanceTimersByTimeAsync(5000);
    expect(onUpdate).not.toHaveBeenCalled();
    expect(fn).toHaveBeenCalledTimes(1);
  });

  it('stop after a success leaves no timer', async () => {
    const fn = vi.fn().mockResolvedValue(1);
    const p = createPoller(fn, 1000, vi.fn());
    await flush();
    p.stop();
    await vi.advanceTimersByTimeAsync(5000);
    expect(fn).toHaveBeenCalledTimes(1);
  });

  it('refresh polls now and does not double-schedule', async () => {
    const fn = vi.fn().mockResolvedValue(1);
    const p = createPoller(fn, 1000, vi.fn());
    await flush();
    await vi.advanceTimersByTimeAsync(600);
    p.refresh();
    expect(fn).toHaveBeenCalledTimes(2);
    await flush();
    await vi.advanceTimersByTimeAsync(999);
    expect(fn).toHaveBeenCalledTimes(2);
    await vi.advanceTimersByTimeAsync(1);
    expect(fn).toHaveBeenCalledTimes(3);
    await vi.advanceTimersByTimeAsync(1000);
    expect(fn).toHaveBeenCalledTimes(4);
    p.stop();
  });
});

import type { ApiError as ApiErrorType } from './api';
import { ApiError } from './api';

export interface PollState<T> {
  data: T | null;
  error: ApiErrorType | null;
  at: number | null;
}

export function createPoller<T>(
  fn: () => Promise<T>,
  intervalMs: number,
  onUpdate: (s: PollState<T>) => void,
): { refresh(): void; stop(): void } {
  let stopped = false;
  let timer: ReturnType<typeof setTimeout> | undefined;
  let gen = 0;
  const state: PollState<T> = { data: null, error: null, at: null };

  const run = async () => {
    if (timer !== undefined) clearTimeout(timer);
    timer = undefined;
    const mine = ++gen;
    try {
      const d = await fn();
      if (stopped || mine !== gen) return;
      state.data = d;
      state.error = null;
      state.at = Date.now();
    } catch (e) {
      if (stopped || mine !== gen) return;
      state.error = e instanceof ApiError ? e : new ApiError(0, e instanceof Error ? e.message : 'error');
    }
    onUpdate({ ...state });
    timer = setTimeout(run, intervalMs);
  };
  run();

  return {
    refresh: () => {
      if (!stopped) run();
    },
    stop: () => {
      stopped = true;
      if (timer !== undefined) clearTimeout(timer);
      timer = undefined;
    },
  };
}

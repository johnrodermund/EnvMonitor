import { createContext } from 'preact';
import type { ApiError } from './api';
import type { StatusJson } from './types';

export interface StatusState {
  data: StatusJson | null;
  error: ApiError | null;
  at: number | null;
  refresh: () => void;
}

export const StatusContext = createContext<StatusState>({ data: null, error: null, at: null, refresh: () => {} });

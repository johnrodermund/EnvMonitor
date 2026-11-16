import { createContext } from 'preact';
import { DEFAULT_APPEARANCE, type Appearance } from './logic/appearance';

export interface AppearanceState {
  appearance: Appearance;
  /** Shows a change at once (after the device accepted it); a failed save calls it again with the old look. */
  setAppearance: (a: Appearance) => void;
}

export const AppearanceContext = createContext<AppearanceState>({ appearance: DEFAULT_APPEARANCE, setAppearance: () => {} });

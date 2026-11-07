import type { StatusJson } from '../types';

export type Kind = 'measure' | 'calibrate';
export type Step = 'intro' | 'starting' | 'ready' | 'measuring' | 'ph4' | 'ph4done' | 'ph7' | 'done' | 'cancelled';

export interface WizardState {
  kind: Kind;
  step: Step;
  startSeq: number;
  startUptime: number;
  message?: string;
  /** Calibration values when the wizard started, for the old-against-new result. */
  oldPh4?: number | null;
  oldPh7?: number | null;
  /** Idle statuses with no seq change still tolerated, because a poll that began before a command can be stale. */
  grace?: number;
}

const GRACE = 2;

const seqOf = (kind: Kind, s: StatusJson) => (kind === 'measure' ? s.manual.seq : s.calibration.seq) ?? 0;
const cancelMsg = (kind: Kind) => (kind === 'calibrate' ? 'Calibration cancelled' : 'Measurement cancelled');

export function wizardStart(kind: Kind, s: StatusJson): WizardState {
  return {
    kind,
    step: 'intro',
    startSeq: seqOf(kind, s),
    // -1 means unknown: no uptime reading can be below it.
    startUptime: s.uptime_s ?? -1,
    oldPh4: s.calibration.ph4_adc,
    oldPh7: s.calibration.ph7_adc,
  };
}

/** The start command (measure_start or calibrate_start) was sent. */
export function wizardBegin(w: WizardState): WizardState {
  return { ...w, step: 'starting', grace: GRACE };
}

/** The next command was sent from `ready` or `ph4done`. */
export function wizardSent(w: WizardState): WizardState {
  if (w.step === 'ready') return { ...w, step: w.kind === 'measure' ? 'measuring' : 'ph4', grace: GRACE };
  if (w.step === 'ph4done') return { ...w, step: 'ph7', grace: GRACE };
  return w;
}

export function wizardOnStatus(w: WizardState, s: StatusJson): WizardState {
  if (w.step === 'intro' || w.step === 'done' || w.step === 'cancelled') return w;
  const cancel = (): WizardState => ({ ...w, step: 'cancelled', message: cancelMsg(w.kind) });
  if (s.uptime_s !== null && s.uptime_s < w.startUptime) return cancel();

  // A seq increase arrives in the same status as idle, so check it before idle means cancelled.
  const resultStep = w.step === 'ready' || w.step === 'measuring' || w.step === 'ph7';
  if (resultStep && seqOf(w.kind, s) > w.startSeq) return { ...w, step: 'done' };

  const state = s.cycle.state;
  if (state === 'idle') {
    // The device should be holding here, so idle can only mean a cancel or a timeout.
    if (w.step === 'ready' || w.step === 'ph4done') return cancel();
    const g = w.grace ?? 0;
    if (g > 0) return { ...w, grace: g - 1 };
    return cancel();
  }
  // The device is acting on the command, so the stale window is over.
  const live = { ...w, grace: 0 };
  if (w.step === 'starting' && state === 'holding') return { ...live, step: 'ready' };
  if (w.step === 'ph4' && state === 'holding' && s.cycle.ph4_ready) return { ...live, step: 'ph4done' };
  return live;
}

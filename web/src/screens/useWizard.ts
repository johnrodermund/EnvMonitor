import { useContext, useEffect, useState } from 'preact/hooks';
import { ApiError, command } from '../api';
import { wizardBegin, wizardCancelling, wizardOnStatus, wizardSent, wizardStart } from '../logic/wizard';
import type { Kind, WizardState } from '../logic/wizard';
import { StatusContext } from '../status-context';
import type { CommandName } from '../types';

/** Shared state machine plumbing for the Measure and Calibrate screens. */
export function useWizard(kind: Kind) {
  const { data, error, refresh } = useContext(StatusContext);
  const [w, setW] = useState<WizardState | null>(null);
  const [msg, setMsg] = useState<string | null>(null);
  const [sending, setSending] = useState(false);

  useEffect(() => {
    if (!data) return;
    setW((cur) => (cur ? wizardOnStatus(cur, data) : wizardStart(kind, data)));
  }, [data]);

  const send = async (cmd: CommandName, next: (w: WizardState) => WizardState) => {
    if (!data || !w) return;
    setSending(true);
    setMsg(null);
    try {
      await command(cmd);
      setW((cur) => (cur ? next(cur) : cur));
    } catch (e) {
      setMsg(e instanceof ApiError ? e.message : 'Request failed');
    }
    setSending(false);
    refresh();
  };

  return {
    w,
    data,
    msg,
    sending,
    offline: !!error,
    busy: data?.cycle.state === 'busy',
    /** Sends the start command; the wizard captures old values from the status current right now. */
    begin: (cmd: CommandName) => send(cmd, () => wizardBegin(wizardStart(kind, data!))),
    next: (cmd: CommandName) => send(cmd, wizardSent),
    cancel: async () => {
      if (!data) return;
      setSending(true);
      setMsg(null);
      try {
        await command('cancel');
        setW((cur) => (cur ? wizardCancelling(cur) : cur));
      } catch (e) {
        setMsg(e instanceof ApiError ? e.message : 'Request failed');
      }
      setSending(false);
      refresh();
    },
    /** Back to the intro step, with fresh start values. */
    reset: () => {
      setMsg(null);
      if (data) setW(wizardStart(kind, data));
    },
  };
}

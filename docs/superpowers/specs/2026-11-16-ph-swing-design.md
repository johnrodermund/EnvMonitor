# pH swing tracking and buffer/nutrient suggestions

Date: 2026-11-16. Repo: EnvMonitor (device and web app), plus a one-line change in PiServer (new event kinds).

## Why

The owner judges buffer and nutrient levels by how pH behaves. A tank with enough buffer and nutrients keeps a steady daily pH pattern. When pH starts swinging erratically, or jumps more than usual when the lights change, the water is losing buffering capacity, and it needs buffer, nutrients or a water change. EC tells which: above about 275 µS there are enough nutrients, so the buffer is depleted and the fix is buffer or a water change.

The owner also wants a daily record of the pH swing, to see whether the swings always line up with the light changes. The lights are on a fixed timer that occasionally switches between 12/12 and 16/8.

## Evidence (Pi data, Nov 5–15, 10-minute readings)

- pH swings 0.6–1.9 pH every day with no intervention: the daily CO₂/light cycle.
- The EC top-up rule (compare 6 readings before with 6 after) applied to pH fires 2–5 times a day at a 0.2–0.3 threshold, and still about once a day at 0.5. A step detector for pH would flood the owner with false events, the events spec's main anti-goal.
- Water temperature never moved 1 °F within an hour; its daily range is 0.5–2 °F. Temperature problems show as slow drift, not steps.
- The biggest one-hour pH fall usually lands between 08:30 and 11:00, which suggests a light change. The biggest rise is scattered; part of that is the per-reading noise before the pH hum fix.
- Readings before 2026-11-16 are raw EC (about 235–252). Since then the device shows EC referred to 25 °C (about 250 at the current 20–21 °C water).

## Goals

1. Every day, record a **pH swing event**: the day's swing, the biggest one-hour rise and fall, their times, and how far each was from a light change.
2. When pH swings erratically or jumps more than usual at a light change, **suggest a fix**: buffer or a water change when EC is above the nutrients-sufficient level, otherwise nutrients (or buffer, or a water change).
3. **Warn when water temperature stays outside its target range** for a while.
4. Everything runs on the device with no Pi needed, shows on the Events tab and the charts, and syncs to the Pi like other events.

## Non-goals

- Detecting pH or temperature steps the way EC top-ups are detected (the evidence above rules it out).
- Learning the light schedule from data. It comes from Settings.
- Dosing or any control output.

## Anti-goals

- Daily nagging. At most one buffer/nutrient suggestion per day, and none for 2 days after a buffer, nutrient or water-change event of any status except rejected.
- A suggestion based on too little history: none until 5 good days are known.
- Breaking the device limits (one slow step per pass, no I/O under locks, little RAM).

## Approaches considered

- **A. Daily metrics compared with the tank's own recent days (chosen).** Once a day, compute the swing, the light-change jump and the jitter, keep the last 14 days, and suggest when today is well above the median of past days. It adapts to the tank, to the hum fix and to photoperiod changes, and needs only small settings.
- **B. Fixed thresholds (for example "swing above 1.2 pH").** Simpler, but the tank already swings 0.6–1.9 pH; a fixed number would either fire constantly or never.
- **C. Step detection like EC.** Rejected by the evidence.

## Design

### Settings (Settings tab, Event detection panel; NVS; `/api/settings`)

- `lights_on`, `lights_off`: local `HH:MM`, default `07:00` and `19:00`. The owner updates them when the timer changes photoperiod.
- `nutrients_ok_ec`: µS/cm as the app shows it (referred to 25 °C when compensation is on), default 275.
- `swing_alert_factor`: how much worse than usual counts, default 1.5 (50 % above the median of past days).

### Daily pH swing event

- **When:** at the first recorded reading of a new local calendar day, for the day just ended. It is skipped when that day has under 75 % of its expected readings (from the interval) or the clock wasn't set. After several missed midnights (device off), only the most recent day is recorded. On the two DST days a 23- or 25-hour day is accepted as is. *(Builder's calls.)*
- **Smoothing:** pH median of 3 consecutive readings, against single-reading noise.
- **Metrics:**
  - swing = max − min of the smoothed pH
  - biggest rise and biggest fall = the largest increase and decrease between readings about 1 hour apart (the first reading at least 60 minutes later), with the time each starts
  - light-change jump = the largest absolute 1-hour change starting from 30 minutes before to 2 hours after lights-on or lights-off, and which change it followed
  - jitter = median absolute difference between consecutive raw readings
- **Record:** kind `ph_swing`, source `detected`, status `confirmed` (informational; the owner can still reject it), `occurred_at` = the start of the day's biggest one-hour change, `ph_before` and `ph_after` = pH at the start and end of that change, EC fields empty. The note (at most 200 characters) reads like: `Swing 1.38 (4.96–6.34). Fall −0.52 at 08:43, 1:43 after lights on. Rise +0.32 at 17:23, 1:37 before lights off. Jitter 0.014.`
- It isn't subject to the 2-hour rules of top-ups. It shares the 100-event store with no separate cap: the store drops the oldest backed-up events first, and one swing event a day leaves room for months of other events. *(Builder's call.)* There is one per local day; the device remembers the last day it processed (in NVS) so a reboot doesn't repeat it.

### Buffer/nutrient suggestion (same day, owner's choice)

- **When:** each light change is judged on its own, at the first recorded reading at least 2 hours after it. Lights on at 07:00 is judged at about 09:00, so the suggestion arrives that morning. The judgement uses the readings already in the log; nothing waits for midnight.
- **What's judged:**
  - the jump for that change: the largest absolute 1-hour change (smoothed pH) starting from 30 minutes before to 2 hours after the change
  - the jitter over the 12 hours before the judgement: the median absolute difference between consecutive raw readings
- **History:** the last 14 values of the lights-on jump, the last 14 of the lights-off jump, and the last 14 jitter values. They're kept in NVS, and every judged change adds its values whether or not it triggered. The value being judged is compared with the values before it, never with itself. A median of 14 only moves after about a week of bad days, so a sudden problem stands out, and a photoperiod change is absorbed in about a week. *(Builder's call.)*
- **Trigger:** with at least 5 past values of that kind, the jump is above `swing_alert_factor` × the median of past jumps for the same change (on or off), or the jitter is above `swing_alert_factor` × the median of past jitter.
- **Skipped:** a change whose window has under 75 % of its expected readings, or that passed while the clock wasn't set. Each change is judged once; the device remembers the last change it judged (in NVS).
- **Choice:** the median EC of the last 12 hours (as the app shows it) above `nutrients_ok_ec` → kind `buffer`, note like `pH jumped 0.81 at lights on, 1.6× usual. EC 281 is above 275, so nutrients look fine: add buffer or do a water change.` Otherwise → kind `nutrients`, note like `... EC 248 is below 275: add nutrients, buffer, or do a water change.`
- **Record:** source `detected`, status `suggested`, `occurred_at` = the start of the light-change jump. Confirming it means the owner did it; Edit can relabel it (for example to `water_change`) or move its time.
- **Quiet period:** no suggestion when a `buffer`, `nutrients` or `water_change` event (any status except rejected) lies within the past 2 days. So there's at most one suggestion every 2 days, even though there are two judgements a day.
- **EC scale:** the reading log keeps raw EC, and the device applies the current temperature coefficient whenever it reads it. So the comparison with `nutrients_ok_ec` is always on the scale the app shows. *(Answered by the existing design.)*

### Light-timer shift check (added after the first flash, owner's request)

- The settings panel says why the light times matter: in a planted tank the lights drive the daily pH swing.
- Each daily record also keeps when the day's biggest 1-hour fall started and when its low was, as minutes from the lights-on setting. In this tank both follow lights on (fall mostly 0:40–2:40 after it, low 10:00–13:00); the daily high is too scattered to use.
- With 10 days kept, when the median of the last 3 has moved 60 minutes or more from the 7 before, for both times and the same way, the device adds a suggested `other` event: `pH has been falling about 1:20 earlier than usual for 3 days (around 07:50 instead of 09:10). Lights on is still set to 08:00. Did the lights get adjusted? If so, update the light times in Settings.`
- The history then starts over, so it asks at most once every 10 days. Changing the light setting also clears it, since the times are kept against the setting.
- Limits: shifts under about an hour are lost in the day-to-day spread (about ±1 h), and a change that only moves lights off isn't seen.

### Temperature warning

- When every reading for the last 2 hours is outside the active target range for temperature, on either side, add one suggested event. The range is the one the Dashboard uses (device, Pi or built-in, all in °F, as the log is). The event is: kind `other`, note like `Water 82.1 °F has been above target (80 °F) for 2 h: check the heater.`
- It re-arms after temperature has been back inside the range for 1 hour, so one excursion gives one warning. That state lives in RAM, so a reboot during a long excursion may warn once more. *(Builder's call: acceptable.)*

### Where it runs

- In `detectEvents()` after each recorded reading, on the copy of the reading log it already makes, outside every lock except the brief ones it takes now. The day metrics, history and trigger logic live in a new host-tested header (`src/ph_swing.h`); the sketch adds the settings, NVS storage and calls.
- The reading log covers about 33 hours at a 10-minute interval, so a full previous day is available at midnight. Under about 6 minutes the 200-reading log holds less than 75 % of a day, so days are skipped by the 75 % rule. The Settings text says so. *(Corrected after review: the first draft had this backwards.)*

### Pi (PiServer)

- `EventIn.kind` also accepts `ph_swing` and `nutrients`. No schema change: `kind` is a text column. This must be deployed before the device firmware. Otherwise the Pi refuses the new kinds with 422, and those events stay "refused by the Pi" until edited.

### Web app

- Events tab: new labels "pH swing" and "Nutrients"; the Log event form offers Nutrients too.
- Charts: `ph_swing` events draw as thin markers on the pH chart only, in their own colour.
- Settings: the four new fields with short help text.

## Constraints

- EnvMonitor `CLAUDE.md`: locks, one slow step per pass, no I/O under locks, host tests for logic in `src/`, `npm run build && npm run embed` before a firmware build, and never upload `data/`.
- The note limit is 200 characters (device and Pi).

## Left to the builder

- The exact note wording within 200 characters, marker colours, and test structure.
- The storage format of the 14-day history and last-processed day in NVS.

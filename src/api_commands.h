#pragma once

#include <string>
#include "reading_cycle.h"

struct CommandResult {
  int httpCode;
  std::string message;
};

inline CommandResult commandAccepted() {
  return {202, "accepted"};
}

inline CommandResult commandRejected(const std::string &message) {
  return {409, message};
}

inline std::string busyStepName(const ReadingCycle &cycle) {
  switch (cycle.step()) {
    case CycleStep::Warmup:
      return "warmup";
    case CycleStep::Sampling:
      return "sampling";
    case CycleStep::Restarting:
      return "restarting";
    default:
      return "pending";  //a command is queued but not yet applied
  }
}

//Maps a web API command to a ReadingCycle request, or says why it can't run now.
inline CommandResult dispatchCommand(const std::string &cmd, ReadingCycle &cycle) {
  bool known = cmd == "cancel" || cmd == "restart" || cmd == "reset" || cmd == "reading" ||
               cmd == "measure_start" || cmd == "calibrate_start" || cmd == "measure" ||
               cmd == "calibrate_ph4" || cmd == "calibrate_ph7";
  if (!known) {
    return {400, "unknown command"};
  }

  if (cmd == "restart") {
    cycle.request(CycleCommand::Restart);
    return commandAccepted();
  }
  if (cmd == "reset") {
    cycle.request(CycleCommand::WipeAndRestart);
    return commandAccepted();
  }
  if (cmd == "cancel") {
    if (cycle.step() == CycleStep::Restarting) {
      return commandRejected("restarting");
    }
    cycle.request(CycleCommand::Resume);
    return commandAccepted();
  }

  CycleStatus status = cycle.status();
  if (status == CycleStatus::Busy) {
    return commandRejected("busy: " + busyStepName(cycle));
  }

  if (cmd == "reading") {
    if (status != CycleStatus::Idle) {
      return commandRejected("finish or cancel the current step first");
    }
    cycle.request(CycleCommand::TakeReading);
    return commandAccepted();
  }
  if (cmd == "measure_start" || cmd == "calibrate_start") {
    cycle.request(CycleCommand::Hold);
    return commandAccepted();
  }

  if (status != CycleStatus::Holding) {
    return commandRejected("start first");
  }
  if (cmd == "measure") {
    cycle.request(CycleCommand::MeasureManual);
  } else if (cmd == "calibrate_ph4") {
    cycle.request(CycleCommand::MeasurePh4);
  } else {
    if (!cycle.ph4Ready()) {
      return commandRejected("measure pH 4 first");
    }
    cycle.request(CycleCommand::MeasurePh7);
  }
  return commandAccepted();
}

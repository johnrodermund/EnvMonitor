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
  if (cmd == "restart") {
    cycle.request(CycleCommand::Restart);
    return commandAccepted();
  }
  if (cmd == "reset") {
    cycle.request(CycleCommand::WipeAndRestart);
    return commandAccepted();
  }
  if (cmd == "cancel") {
    CycleCommand queued = cycle.pendingCommand();
    if (cycle.step() == CycleStep::Restarting || queued == CycleCommand::Restart ||
        queued == CycleCommand::WipeAndRestart) {
      return commandRejected("restarting");
    }
    cycle.request(CycleCommand::Resume);
    return commandAccepted();
  }

  //What the cycle must be doing before the command can run
  enum class Need { Idle, IdleOrHolding, Holding, HoldingWithPh4 };
  CycleCommand request;
  Need need;
  if (cmd == "reading") {
    request = CycleCommand::TakeReading;
    need = Need::Idle;
  } else if (cmd == "measure_start" || cmd == "calibrate_start") {
    request = CycleCommand::Hold;
    need = Need::IdleOrHolding;
  } else if (cmd == "measure") {
    request = CycleCommand::MeasureManual;
    need = Need::Holding;
  } else if (cmd == "calibrate_ph4") {
    request = CycleCommand::MeasurePh4;
    need = Need::Holding;
  } else if (cmd == "calibrate_ph7") {
    request = CycleCommand::MeasurePh7;
    need = Need::HoldingWithPh4;
  } else {
    return {400, "unknown command"};
  }

  CycleStatus status = cycle.status();
  if (status == CycleStatus::Busy) {
    return commandRejected("busy: " + busyStepName(cycle));
  }
  if (need == Need::Idle && status != CycleStatus::Idle) {
    return commandRejected("finish or cancel the current step first");
  }
  if ((need == Need::Holding || need == Need::HoldingWithPh4) && status != CycleStatus::Holding) {
    return commandRejected("start first");
  }
  if (need == Need::HoldingWithPh4 && !cycle.ph4Ready()) {
    return commandRejected("measure pH 4 first");
  }
  cycle.request(request);
  return commandAccepted();
}

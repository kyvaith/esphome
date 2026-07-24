#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"

namespace esphome::task_runtime_profiler {

class TaskRuntimeProfiler : public Component {
 public:
  void log();

 protected:
  struct RuntimeState;
  RuntimeState *state_{nullptr};
};

template<typename... Ts> class TaskRuntimeProfilerLogAction : public Action<Ts...>, public Parented<TaskRuntimeProfiler> {
 public:
  void play(Ts... x) override { this->parent_->log(); }
};

}  // namespace esphome::task_runtime_profiler

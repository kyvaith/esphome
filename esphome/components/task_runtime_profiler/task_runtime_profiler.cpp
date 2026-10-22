#include "task_runtime_profiler.h"

#include "esphome/core/log.h"

#include <cstring>

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace esphome::task_runtime_profiler {

static const char *const TAG = "task_runtime_profiler";

static constexpr UBaseType_t MAX_TASKS = 64;
static constexpr size_t TOP_TASKS = 10;

struct TaskRuntimePrev {
  UBaseType_t number{0};
  uint64_t runtime{0};
  bool valid{false};
};

struct TaskRuntimeRow {
  const char *name{nullptr};
  uint64_t delta{0};
  UBaseType_t priority{0};
  uint32_t stack_watermark{0};
  int32_t core{-2};
  eTaskState state{eInvalid};
};

struct TaskRuntimeProfiler::RuntimeState {
  TaskStatus_t *tasks{nullptr};
  TaskRuntimePrev *previous{nullptr};
  bool initialized{false};
};

static const char *task_state_name(eTaskState state) {
  switch (state) {
    case eRunning:
      return "run";
    case eReady:
      return "ready";
    case eBlocked:
      return "block";
    case eSuspended:
      return "suspend";
    case eDeleted:
      return "delete";
    default:
      return "?";
  }
}

static bool task_is_idle(const char *name) {
  return name != nullptr && (std::strstr(name, "IDLE") != nullptr || std::strstr(name, "Idle") != nullptr ||
                             std::strstr(name, "idle") != nullptr);
}

static int32_t task_core_id(const TaskStatus_t &task) {
#if (configNUMBER_OF_CORES > 1)
  return static_cast<int32_t>(xTaskGetCoreID(task.xHandle));
#else
  return 0;
#endif
}

static void insert_top_task(TaskRuntimeRow *top, size_t top_count, const TaskRuntimeRow &row) {
  for (size_t i = 0; i < top_count; i++) {
    if (row.delta <= top[i].delta) {
      continue;
    }
    for (size_t j = top_count - 1; j > i; j--) {
      top[j] = top[j - 1];
    }
    top[i] = row;
    break;
  }
}

void TaskRuntimeProfiler::log() {
#if configGENERATE_RUN_TIME_STATS
  if (this->state_ == nullptr) {
    this->state_ = static_cast<RuntimeState *>(
        heap_caps_calloc(1, sizeof(RuntimeState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (this->state_ == nullptr) {
      ESP_LOGW(TAG, "Profiler state allocation failed");
      return;
    }
  }

  if (this->state_->tasks == nullptr) {
    this->state_->tasks = static_cast<TaskStatus_t *>(
        heap_caps_calloc(MAX_TASKS, sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    this->state_->previous = static_cast<TaskRuntimePrev *>(
        heap_caps_calloc(MAX_TASKS, sizeof(TaskRuntimePrev), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (this->state_->tasks == nullptr || this->state_->previous == nullptr) {
      if (this->state_->tasks != nullptr) {
        heap_caps_free(this->state_->tasks);
      }
      if (this->state_->previous != nullptr) {
        heap_caps_free(this->state_->previous);
      }
      this->state_->tasks = nullptr;
      this->state_->previous = nullptr;
      ESP_LOGW(TAG, "Task data allocation failed");
      return;
    }
  }

  configRUN_TIME_COUNTER_TYPE total_runtime = 0;
  const UBaseType_t count = uxTaskGetSystemState(this->state_->tasks, MAX_TASKS, &total_runtime);
  if (count == 0) {
    ESP_LOGW(TAG, "Task profile unavailable: no task stats");
    return;
  }

  uint64_t sum_delta = 0;
  uint64_t idle_delta = 0;
  TaskRuntimeRow top[TOP_TASKS]{};

  for (UBaseType_t i = 0; i < count; i++) {
    const UBaseType_t number = this->state_->tasks[i].xTaskNumber;
    const uint64_t runtime = static_cast<uint64_t>(this->state_->tasks[i].ulRunTimeCounter);
    uint64_t last_runtime = runtime;
    bool found = false;

    for (UBaseType_t j = 0; j < MAX_TASKS; j++) {
      auto &previous = this->state_->previous[j];
      if (previous.valid && previous.number == number) {
        last_runtime = previous.runtime;
        previous.runtime = runtime;
        found = true;
        break;
      }
    }

    if (!found) {
      for (UBaseType_t j = 0; j < MAX_TASKS; j++) {
        auto &previous = this->state_->previous[j];
        if (!previous.valid) {
          previous.number = number;
          previous.runtime = runtime;
          previous.valid = true;
          break;
        }
      }
    }

    if (!this->state_->initialized) {
      continue;
    }

    const uint64_t delta = runtime >= last_runtime ? runtime - last_runtime : 0;
    sum_delta += delta;
    if (task_is_idle(this->state_->tasks[i].pcTaskName)) {
      idle_delta += delta;
    }

    TaskRuntimeRow row;
    row.name = this->state_->tasks[i].pcTaskName;
    row.delta = delta;
    row.priority = this->state_->tasks[i].uxCurrentPriority;
    row.stack_watermark = static_cast<uint32_t>(this->state_->tasks[i].usStackHighWaterMark);
    row.core = task_core_id(this->state_->tasks[i]);
    row.state = this->state_->tasks[i].eCurrentState;
    insert_top_task(top, TOP_TASKS, row);
  }

  if (!this->state_->initialized) {
    this->state_->initialized = true;
    ESP_LOGW(TAG, "Task profiler initialized: tasks=%u total_runtime=%llu", static_cast<unsigned>(count),
             static_cast<unsigned long long>(total_runtime));
    return;
  }

  if (sum_delta == 0) {
    ESP_LOGW(TAG, "Task profile has no runtime delta: tasks=%u total_runtime=%llu", static_cast<unsigned>(count),
             static_cast<unsigned long long>(total_runtime));
    return;
  }

  const uint32_t busy_x10 = static_cast<uint32_t>(((sum_delta - idle_delta) * 1000ULL) / sum_delta);
  const uint32_t idle_x10 = static_cast<uint32_t>((idle_delta * 1000ULL) / sum_delta);
  ESP_LOGW(TAG, "Task profile: busy=%u.%u%% idle=%u.%u%% tasks=%u internal=%uKiB psram=%uKiB",
           static_cast<unsigned>(busy_x10 / 10), static_cast<unsigned>(busy_x10 % 10),
           static_cast<unsigned>(idle_x10 / 10), static_cast<unsigned>(idle_x10 % 10), static_cast<unsigned>(count),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

  for (size_t i = 0; i < TOP_TASKS && top[i].delta > 0; i++) {
    const uint32_t pct_x10 = static_cast<uint32_t>((top[i].delta * 1000ULL) / sum_delta);
    ESP_LOGW(TAG, "#%u %s %u.%u%% core=%d prio=%u stack=%u state=%s", static_cast<unsigned>(i + 1),
             top[i].name == nullptr ? "<null>" : top[i].name, static_cast<unsigned>(pct_x10 / 10),
             static_cast<unsigned>(pct_x10 % 10), static_cast<int>(top[i].core),
             static_cast<unsigned>(top[i].priority),
             static_cast<unsigned>(top[i].stack_watermark), task_state_name(top[i].state));
  }
#else
  ESP_LOGW(TAG, "Task profiler disabled: enable CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS");
#endif
}

}  // namespace esphome::task_runtime_profiler

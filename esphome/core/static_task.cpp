#include "esphome/core/static_task.h"

#ifdef USE_ESP32

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {

ESPHOME_LOG_TAG(TAG, "static_task");

bool StaticTask::reserve(uint32_t stack_size, bool use_psram) {
  if (this->handle_ != nullptr) {
    // Task is already created; must call destroy() first
    return false;
  }

  if (this->stack_buffer_ != nullptr && (stack_size > this->stack_size_ || use_psram != this->use_psram_)) {
    // Existing buffer is too small or wrong memory type; free it to reallocate below
    this->stack_buffer_.reset();
  }

  if (this->stack_buffer_ == nullptr) {
    this->stack_size_ = stack_size;
    this->use_psram_ = use_psram;
    RAMAllocator<StackType_t> allocator(use_psram ? RAMAllocator<StackType_t>::ALLOC_EXTERNAL
                                                  : RAMAllocator<StackType_t>::ALLOC_INTERNAL);
    this->stack_buffer_ = allocator.make_unique_array_for_overwrite(stack_size);
  }
  if (this->stack_buffer_ == nullptr) {
    const uint32_t caps = (use_psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL) | MALLOC_CAP_8BIT;
    ESP_LOGE(TAG, "Failed to allocate %u-byte %s task stack: free=%u largest=%u", static_cast<unsigned>(stack_size),
             use_psram ? "external" : "internal", static_cast<unsigned>(heap_caps_get_free_size(caps)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(caps)));
    return false;
  }

  if (this->tcb_ == nullptr) {
    RAMAllocator<StaticTask_t> allocator(RAMAllocator<StaticTask_t>::ALLOC_INTERNAL);
    this->tcb_ = allocator.make_unique();
  }
  if (this->tcb_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate %u-byte internal task control block",
             static_cast<unsigned>(sizeof(StaticTask_t)));
    this->deallocate();
    return false;
  }

  return true;
}

bool StaticTask::create(TaskFunction_t fn, const char *name, uint32_t stack_size, void *param, UBaseType_t priority,
                        bool use_psram, BaseType_t core_id, bool retain_resources_on_create_failure) {
  if (!this->reserve(stack_size, use_psram)) {
    return false;
  }

  this->handle_ = xTaskCreateStaticPinnedToCore(fn, name, this->stack_size_, param, priority, this->stack_buffer_.get(),
                                                this->tcb_.get(), core_id);
  if (this->handle_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create task '%s' with reserved resources", name);
    if (!retain_resources_on_create_failure) {
      this->deallocate();
    }
    return false;
  }
  return true;
}

bool StaticTask::destroy() {
  if (this->handle_ == nullptr) {
    return true;
  }

  // Suspending takes the task off the ready and event lists, so nothing can schedule it again. It only asks
  // the other core to yield though, so the task may still be running on it for a moment.
  vTaskSuspend(this->handle_);
  if (eTaskGetState(this->handle_) != eSuspended) {
    // The task is still running on the other core and using its stack. Deleting it now would only put it on
    // the termination list and return, so the caller has to try again once it has been swapped out.
    return false;
  }

  // The task cannot run again, so the delete completes right away instead of being left to the idle task.
  TaskHandle_t handle = this->handle_;
  this->handle_ = nullptr;
  vTaskDelete(handle);
  return true;
}

bool StaticTask::deallocate() {
  if (!this->destroy()) {
    return false;
  }
  this->stack_buffer_.reset();
  this->stack_size_ = 0;
  this->tcb_.reset();
  return true;
}

}  // namespace esphome

#endif  // USE_ESP32

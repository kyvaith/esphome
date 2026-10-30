#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string_view>
#include <thread>

inline bool run_real_power_inflight_case(Fixture &f, std::string_view name) {
  if (name != "inflight_retire_during_press")
    return false;
#ifdef USE_ESP32
  f.state(true);
  f.scalar.loop();
  const auto previous_completion = f.scalar.completion();
  std::mutex mutex;
  std::condition_variable condition;
  bool submitted = false, permit_retirement = false, worker_finished = false;
  std::thread::id worker_id;
  f.lvgl.on_submit = [&]() {
    std::unique_lock lock(mutex);
    submitted = true;
    condition.notify_all();
    assert(condition.wait_for(lock, std::chrono::seconds(2), [&]() { return permit_retirement; }));
  };
  test_notify_hook = [&]() {
    if (f.scalar.completion() != previous_completion)
      f.scalar.stop_worker_for_test();
  };
  test_delay_hook = [&]() {
    if (std::this_thread::get_id() == worker_id) {
      f.scalar.stop_worker_for_test();
    } else {
      // Deterministic host scheduling: the source really retires on the actual
      // worker while the main task is inside its bounded drain, before any
      // native pressed-style flush. No completion or busy atomic is fabricated.
      std::unique_lock lock(mutex);
      permit_retirement = true;
      condition.notify_all();
      assert(condition.wait_for(lock, std::chrono::seconds(2), [&]() { return worker_finished; }));
    }
  };
  std::thread worker([&]() {
    {
      std::lock_guard lock(mutex);
      worker_id = std::this_thread::get_id();
    }
    f.scalar.run_worker_for_test();
    {
      std::lock_guard lock(mutex);
      worker_finished = true;
    }
    condition.notify_all();
  });
  {
    std::unique_lock lock(mutex);
    assert(condition.wait_for(lock, std::chrono::seconds(2), [&]() { return submitted; }));
  }
  f.pointer.press(278, 600);  // Begins the production drain while source is borrowed.
  worker.join();
  test_notify_hook = {};
  test_delay_hook = {};
  f.lvgl.on_submit = {};
  assert(f.control.power_captured() && f.control.owner().is_view_ready());
  assert(f.scalar.completion() != previous_completion && f.scalar.accepted());
  assert(!f.scalar.active());
  lv_refr_now(f.display.get());
  assert(f.control.power_captured());
  f.pointer.release();
  real_power_settle(f);
  assert(f.commands() == 1 && f.control.owner().is_view_ready() && f.control.owner().is_scalar_ready());
  assert(!f.control.view_lifecycle().native_power_input_active());
  assert(f.messages.back().identity == f.admitted_identity);
  assert(f.messages.back().operation == ControlOperation::CONTROL_OPERATION_TURN_ON);
  assert(!lv_obj_has_state(f.widgets.on, LV_STATE_PRESSED));
  std::cout << "NEGATIVE " << name << " PASS actual_worker_retired=1 commands=1\n";
  return true;
#else
  (void) f;
  return false;
#endif
}

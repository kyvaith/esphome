#pragma once

// Include after the real-LVGL driver's Fixture definition. These cases use the
// actual owner/component/renderer and native pointer. Only the two explicitly
// named driver seams model unavailable acceleration or an outstanding worker.
#include <cassert>
#include <iostream>
#include <string_view>

inline void real_power_settle(Fixture &f) {
  f.scalar.loop();
  if (f.scalar.active() && f.scalar.request_ready()) {
    f.scalar.run_one();
    f.scalar.loop();
  }
}

inline bool run_real_power_negative_case(Fixture &f, std::string_view name) {
  const auto initial_generation = f.control.get_selection_generation();
  const auto initial_state = f.control.owner().state();
  assert(initial_generation != 0 && f.commands() == 0);

  if (name == "press_lost") {
    f.pointer.press(278, 600);
    assert(f.control.power_captured());
    // Native wait-for-release cancellation generates PRESS_LOST during release.
    // Moving alone is not a cancel when the native button keeps press lock.
    lv_indev_wait_release(f.pointer.get());
    f.pointer.release();
    assert(!f.control.power_captured());
    real_power_settle(f);
    assert(f.commands() == 0 && f.control.owner().is_view_ready());
    assert(!f.control.view_lifecycle().native_power_input_active());
    assert(!lv_obj_has_state(f.widgets.on, LV_STATE_PRESSED));
  } else if (name == "invalidate") {
    f.pointer.press(278, 600);
    assert(f.control.power_captured());
    assert(f.control.invalidate(ControlReason::CONTROL_REASON_SELECTION_CHANGED));
    f.pointer.release();
    real_power_settle(f);
    f.scalar.loop();
    assert(f.commands() == 0 && !f.control.owner().has_selection());
    assert(!f.control.owner().is_view_ready() && !f.control.power_captured());
    assert(!f.control.view_lifecycle().native_power_input_active());
  } else if (name == "new_grant") {
    f.pointer.press(278, 600);
    assert(f.control.power_captured());
    assert(f.control.invalidate(ControlReason::CONTROL_REASON_SELECTION_CHANGED));
    assert(f.control.open(6, "card-light-6", 3));
    ControlMessage replacement;
    replacement.kind = ControlKind::CONTROL_KIND_GRANT;
    replacement.identity = f.messages.back().identity;
    assert(replacement.identity.token.assign("cccccccccccccccccccccccccccccccc"));
    replacement.state = initial_state;
    replacement.state.state_seq = 1;
    f.receive(replacement);
    const auto replacement_generation = f.control.get_selection_generation();
    assert(replacement_generation != 0 && replacement_generation != initial_generation);
    f.pointer.release();
    real_power_settle(f);
    f.scalar.loop();
    assert(f.commands() == 0 && f.control.is_selection_current(replacement_generation));
    // The new grant needs its own explicit lifecycle admission; an old release
    // must not make it ready.
    assert(!f.control.owner().is_view_ready() && !f.control.power_captured());
    assert(!f.control.view_lifecycle().native_power_input_active());
  } else if (name == "snapshot_suspend") {
    f.pointer.press(278, 600);
    assert(f.control.power_captured());
    assert(f.scalar.suspend_for_snapshot());
    f.pointer.release();
    real_power_settle(f);
    f.scalar.loop();
    assert(f.commands() == 0 && f.control.is_selection_current(initial_generation));
    assert(!f.control.owner().is_view_ready() && !f.control.power_captured());
    assert(!f.control.view_lifecycle().native_power_input_active());
  } else if (name == "navigation_closed") {
    f.pointer.press(278, 600);
    assert(f.control.power_captured());
    assert(f.scalar.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CLOSE, &f.application));
    f.application.call_on_prepare_close_callbacks();
    f.application.call_on_closed_callbacks();
    f.pointer.release();
    real_power_settle(f);
    f.scalar.loop();
    assert(f.commands() == 0 && !f.control.owner().has_selection());
    assert(!f.control.owner().is_view_ready() && !f.control.power_captured());
    assert(!f.control.view_lifecycle().native_power_input_active());
  } else if (name == "stale_current") {
    f.state(true);
    f.scalar.loop();
    f.scalar.run_one();  // Accepted current completion, not yet owner-consumed.
    assert(f.scalar.accepted());
    const auto accepted_base = f.scalar.completed_base();
    lv_label_set_text(f.widgets.title, "Explicit stale-completion boundary");
    lv_refr_now(f.display.get());
    assert(f.lvgl.generation != accepted_base);
    f.pointer.press(278, 600);
    assert(!f.control.power_captured());
    assert(!f.control.owner().is_view_ready());
    f.pointer.release();
    real_power_settle(f);
    f.scalar.loop();
    assert(f.commands() == 0 && !f.control.owner().is_view_ready());
    assert(!f.control.view_lifecycle().native_power_input_active());
  } else if (name == "drain_failure") {
    // Same named atomic seam used by the older host worker ownership tests.
    // It models a worker/source that does not retire within the bounded drain.
    f.scalar.set_borrowed_for_test(true);
    const auto delay_before = test_delay_total;
    f.pointer.press(278, 600);
    assert(test_delay_total - delay_before == 250);
    assert(!f.control.power_captured() && !f.control.owner().is_view_ready());
    assert(!f.control.view_lifecycle().native_power_input_active());
    f.scalar.set_borrowed_for_test(false);  // Resource now retired for cleanup.
    f.pointer.release();
    real_power_settle(f);
    const auto cleanup_delay = test_delay_total;
    f.scalar.loop();
    f.scalar.loop();
    assert(test_delay_total == cleanup_delay);  // No repeated drain or retry.
    assert(f.commands() == 0 && !f.control.owner().is_view_ready());
    assert(!f.control.view_lifecycle().native_power_input_active());
  } else if (name == "brightness_waits_for_fence_completion") {
    f.pointer.press(278, 600);
    f.pointer.release();
    assert(f.commands() == 1 && f.control.view_lifecycle().native_power_input_active());
    ControlMessage terminal;
    terminal.kind = ControlKind::CONTROL_KIND_RESULT;
    terminal.identity = f.control.owner().identity();
    terminal.command_seq = 1;
    terminal.state.state_seq = f.control.owner().state().state_seq;
    terminal.result = ControlResult::CONTROL_RESULT_ALREADY_MATCHED;
    terminal.reason = ControlReason::CONTROL_REASON_NONE;
    f.receive(terminal);
    assert(!f.control.owner().is_command_pending());
    // The real scalar hit surface is ready by owner flags, but the previous
    // native release boundary has not yet been consumed by the renderer loop.
    f.pointer.press(400, 480);
    assert(!f.control.owner().is_dragging());
    f.pointer.move(500, 480);
    f.pointer.release();
    assert(f.commands() == 1 && !f.control.owner().is_dragging());
    assert(f.control.view_lifecycle().native_power_input_active());
    real_power_settle(f);
    assert(f.commands() == 1 && f.control.owner().is_view_ready() && f.control.owner().is_scalar_ready());
    assert(!f.control.view_lifecycle().native_power_input_active());
  } else if (name == "duplicate_release") {
    f.pointer.press(278, 600);
    assert(f.control.power_captured());
    f.pointer.release();
    real_power_settle(f);
    assert(f.commands() == 1);
    const auto request = f.control.get_render_request_id();
    f.pointer.release();  // Real indev sees already-released state.
    f.scalar.loop();
    assert(f.commands() == 1 && f.control.get_render_request_id() == request);
    const auto &command = f.messages.back();
    assert(command.kind == ControlKind::CONTROL_KIND_COMMAND && command.command_seq == 1);
    assert(command.operation == ControlOperation::CONTROL_OPERATION_TURN_ON);
    assert(command.identity == f.admitted_identity);
    assert(!lv_obj_has_state(f.widgets.on, LV_STATE_PRESSED));
  } else if (name == "fallback_no_scalar_loop" || name == "no_acceleration_two_power") {
    // This focused seam proves that the declared no-fence backend capability
    // has no Scalar.loop dependency. A separately compiled no-acceleration
    // case should additionally prove the real failed-setup construction path.
    const bool real_failed_setup = name == "no_acceleration_two_power";
    if (real_failed_setup) {
      assert(f.scalar.is_failed());
      assert(f.control.owner().is_view_ready() && !f.control.owner().is_scalar_ready());
    } else {
      f.scalar.set_setup_ready_for_test(false);
    }
    f.pointer.press(278, 600);
    f.pointer.release();
    assert(f.commands() == 1 && !f.control.view_lifecycle().native_power_input_active());
    ControlMessage terminal;
    terminal.kind = ControlKind::CONTROL_KIND_RESULT;
    terminal.identity = f.control.owner().identity();
    terminal.command_seq = 1;
    terminal.state.state_seq = f.control.owner().state().state_seq;
    terminal.result = ControlResult::CONTROL_RESULT_ALREADY_MATCHED;
    terminal.reason = ControlReason::CONTROL_REASON_NONE;
    f.receive(terminal);
    assert(!f.control.owner().is_command_pending());
    f.pointer.press(522, 600);
    f.pointer.release();
    assert(f.commands() == 2 && !f.control.view_lifecycle().native_power_input_active());
    const auto &command = f.messages.back();
    assert(command.kind == ControlKind::CONTROL_KIND_COMMAND && command.command_seq == 2);
    assert(command.operation == ControlOperation::CONTROL_OPERATION_TURN_OFF);
    assert(command.identity == f.admitted_identity);
    assert(!lv_obj_has_state(f.widgets.off, LV_STATE_PRESSED));
    if (!real_failed_setup)
      f.scalar.set_setup_ready_for_test(true);  // Restore ordinary teardown only.
  } else {
    return false;
  }

  std::cout << "NEGATIVE " << name << " PASS commands=" << f.commands()
            << " selected=" << f.control.owner().has_selection()
            << " view=" << f.control.owner().is_view_ready()
            << " fence=" << f.control.view_lifecycle().native_power_input_active() << '\n';
  return true;
}

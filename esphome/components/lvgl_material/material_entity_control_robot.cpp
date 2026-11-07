#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
namespace esphome::lvgl_material {
namespace {
using Op = ControlOperation;
bool interrupt(Op operation) {
  return operation == Op::CONTROL_OPERATION_ROBOT_PAUSE || operation == Op::CONTROL_OPERATION_ROBOT_STOP;
}
bool interruptible(Op operation) {
  return operation == Op::CONTROL_OPERATION_ROBOT_START || operation == Op::CONTROL_OPERATION_ROBOT_DOCK ||
         operation == Op::CONTROL_OPERATION_ROBOT_SPOT || operation == Op::CONTROL_OPERATION_ROBOT_AREA;
}
uint8_t allowed_actions(uint8_t type, uint8_t activity) {
  // Fixed v1 admission mirrors the standard domain policy; supplied eligibility
  // may narrow it, never broaden it. Mower v1 has no Stop or idle capability.
  if (type == 1) {
    switch (activity) {
      case 2:
        return 1;
      case 4:
        return 1 | 8;
      case 5:
        return 2;
      case 7:
        return 2 | 8;
      default:
        return 0;
    }
  }
  switch (activity) {
    case 1:
      return 2 | 4 | 8 | 16;
    case 2:
      return 1 | 16 | 32;
    case 3:
      return 1 | 8 | 16 | 32;
    case 4:
      return 1 | 4 | 8 | 16 | 32;
    case 5:
      return 2 | 4 | 16;
    case 6:
      return 16;
    default:
      return 0;
  }
}
bool operation_allowed(const ControlSnapshot::Robot &robot, Op operation) {
  if (operation < Op::CONTROL_OPERATION_ROBOT_START || operation > Op::CONTROL_OPERATION_ROBOT_SPOT)
    return false;
  const uint8_t bit = 1U << (static_cast<uint8_t>(operation) - static_cast<uint8_t>(Op::CONTROL_OPERATION_ROBOT_START));
  return robot.available && (robot.eligible_actions & bit) != 0;
}
}  // namespace

bool control_valid_robot_snapshot(const ControlSnapshot &snapshot) {
  const auto *robot_payload = snapshot.family_if<ControlSnapshot::Robot>();
  if (robot_payload == nullptr)
    return false;
  const auto &robot = *robot_payload;
  constexpr auto unknown = ControlSnapshot::Robot::UNKNOWN;
  if (snapshot.state_seq == 0 || snapshot.state_seq > CONTROL_MAX_SEQUENCE || snapshot.ttl_ms != CONTROL_LEASE_MS ||
      !number_plain_utf8(snapshot.title.view()) || robot.state_generation == 0 ||
      robot.state_generation > CONTROL_MAX_SEQUENCE || snapshot.on || snapshot.brightness_known ||
      snapshot.caps.brightness || snapshot.caps.turn_on || snapshot.caps.turn_off || robot.type > 1 ||
      robot.activity > 7 || robot.available != (robot.activity != 0) || (robot.type == 0 && robot.activity == 7) ||
      (robot.type == 1 && (robot.activity == 1 || robot.activity == 3)) || robot.caps.actions > 63 ||
      (robot.type == 1 && (robot.caps.actions & ~11U)) || (robot.eligible_actions & ~robot.caps.actions) ||
      (robot.eligible_actions & ~allowed_actions(robot.type, robot.activity)) ||
      robot.caps.speed_count > robot.caps.speeds.size() || robot.caps.area_count > robot.caps.areas.size() ||
      (robot.speed_index != unknown && robot.speed_index >= robot.caps.speed_count) ||
      (robot.battery_percent != unknown && robot.battery_percent > 100) ||
      (robot.type == 1 &&
       (robot.caps.speed_count != 0 || robot.caps.area_count != 0 || robot.speed_index != unknown)) ||
      (!robot.available && (robot.speed_index != unknown || robot.battery_percent != unknown || robot.area_enabled)) ||
      (robot.area_enabled &&
       (robot.caps.area_count == 0 || (robot.activity != 2 && robot.activity != 3 && robot.activity != 4))))
    return false;
  if (robot.caps.actions == 0 && robot.caps.speed_count == 0 && robot.caps.area_count == 0)
    return false;
  for (bool area : {false, true}) {
    const auto &options = area ? robot.caps.areas : robot.caps.speeds;
    const size_t count = area ? robot.caps.area_count : robot.caps.speed_count;
    for (size_t index = 0; index < count; index++) {
      const auto text = options[index].view();
      if (text.empty() || !number_plain_utf8(text) || text == "unknown" || text == "unavailable")
        return false;
      for (size_t prior = 0; prior < index; prior++)
        if (options[index] == options[prior])
          return false;
    }
  }
  return true;
}

bool MaterialEntityControl::set_robot_enabled(bool enabled) {
  if (this->in_callback_)
    return false;
  if (!enabled && this->is_robot())
    this->revoke_(ControlReason::CONTROL_REASON_UNSUPPORTED, true);
  this->robot_enabled_ = enabled;
  return true;
}

bool MaterialEntityControl::accept_robot_state_(const ControlSnapshot &state) {
  const auto *previous_payload = this->state_.family_if<ControlSnapshot::Robot>();
  if (previous_payload == nullptr)
    return false;
  const auto &previous = *previous_payload;
  const auto *next_payload = state.family_if<ControlSnapshot::Robot>();
  if (next_payload == nullptr)
    return false;
  const auto &next = *next_payload;
  if (previous.type != next.type || !(previous.caps == next.caps)) {
    this->revoke_(ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED, true);
    return false;
  }
  if (next.state_generation < previous.state_generation)
    return false;
  if (next.state_generation == previous.state_generation) {
    // Battery is factual display telemetry, not command-admission state.
    if (next.activity != previous.activity || next.available != previous.available ||
        next.speed_index != previous.speed_index || next.eligible_actions != previous.eligible_actions ||
        next.area_enabled != previous.area_enabled) {
      this->revoke_(ControlReason::CONTROL_REASON_SELECTION_CHANGED, true);
      return false;
    }
    return true;
  }
  if (this->dragging_ && interrupt(this->pressed_robot_operation_) &&
      operation_allowed(next, this->pressed_robot_operation_)) {
    this->pressed_generation_ = next.state_generation;
    return true;
  }
  this->cancel_drag();
  return true;
}

bool MaterialEntityControl::can_robot(ControlOperation operation, uint8_t target, bool allow_same) const {
  const auto *robot_payload = this->state_.family_if<ControlSnapshot::Robot>();
  if (robot_payload == nullptr)
    return false;
  const auto &robot = *robot_payload;
  if (!this->is_robot() || !this->robot_enabled_ || !this->selected_ || !this->view_ready_ || !robot.available)
    return false;
  if (this->command_pending_)
    return interrupt(operation) && interruptible(this->pending_robot_operation_) && operation_allowed(robot, operation);
  if (operation == Op::CONTROL_OPERATION_ROBOT_SPEED)
    return robot.speed_index != ControlSnapshot::Robot::UNKNOWN && target < robot.caps.speed_count &&
           (allow_same || target != robot.speed_index);
  if (operation == Op::CONTROL_OPERATION_ROBOT_AREA)
    return robot.area_enabled && target < robot.caps.area_count;
  return operation_allowed(robot, operation);
}

bool MaterialEntityControl::begin_robot_navigation(uint32_t pointer, uint32_t now) {
  if (this->state_.family_if<ControlSnapshot::Robot>() == nullptr || !this->is_robot() || !this->can_input_(now) ||
      this->dragging_)
    return false;
  this->pointer_id_ = pointer;
  this->pressed_generation_ = this->state_.family_if<ControlSnapshot::Robot>()->state_generation;
  // Navigation is not an interrupt; unrelated activity updates retire it.
  this->pressed_robot_operation_ = Op::CONTROL_OPERATION_ROBOT_SPEED;
  this->dragging_ = true;
  return true;
}

bool MaterialEntityControl::release_robot_navigation(uint32_t pointer, uint32_t now) {
  if (!this->can_input_(now) || this->state_.family_if<ControlSnapshot::Robot>() == nullptr || !this->is_robot() ||
      !this->dragging_ || this->pointer_id_ != pointer)
    return false;
  this->dragging_ = false;
  return this->pressed_generation_ == this->state_.family_if<ControlSnapshot::Robot>()->state_generation;
}

bool MaterialEntityControl::begin_robot(uint32_t pointer, ControlOperation operation, uint8_t target, uint32_t now) {
  if (this->in_callback_)
    return false;
  this->tick(now);
  if (!this->enabled_ || this->dragging_ || !this->can_robot(operation, target))
    return false;
  this->pointer_id_ = pointer;
  this->pressed_generation_ = this->state_.family_if<ControlSnapshot::Robot>()->state_generation;
  this->pressed_robot_operation_ = operation;
  this->pressed_robot_target_ = target;
  this->dragging_ = true;
  return true;
}

bool MaterialEntityControl::release_robot(uint32_t pointer, uint32_t now) {
  if (this->in_callback_)
    return false;
  this->tick(now);
  if (!this->enabled_ || !this->is_robot() || !this->dragging_ || this->pointer_id_ != pointer)
    return false;
  this->dragging_ = false;
  if (!this->can_robot(this->pressed_robot_operation_, this->pressed_robot_target_) ||
      this->pressed_generation_ != this->state_.family_if<ControlSnapshot::Robot>()->state_generation ||
      this->command_seq_ == CONTROL_MAX_SEQUENCE)
    return false;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_COMMAND;
  message.identity = this->identity_;
  message.command_seq = ++this->command_seq_;
  message.operation = this->pressed_robot_operation_;
  message.robot_target_index = this->pressed_robot_target_;
  message.expected_state_generation = this->pressed_generation_;
  this->pending_robot_operation_ = message.operation;
  this->command_at_ = now;
  this->command_pending_ = true;
  this->last_result_ = ControlResult::CONTROL_RESULT_PENDING;
  this->last_reason_ = ControlReason::CONTROL_REASON_NONE;
  if (!this->emit_(message)) {
    this->command_pending_ = false;
    this->last_result_ = ControlResult::CONTROL_RESULT_REJECTED;
    this->last_reason_ = ControlReason::CONTROL_REASON_CONNECTION_LOST;
    this->disconnect();
    this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_RESULT);
    return false;
  }
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_SENDING);
  return true;
}
}  // namespace esphome::lvgl_material
#endif

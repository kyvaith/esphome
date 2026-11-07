#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
#include <cstdio>

namespace esphome::lvgl_material {
namespace {
using Input = ControlDatetimeInput;
using Snapshot = ControlDatetimeSnapshot;
constexpr uint8_t ACTIVE = 1, EDITOR = 2, REPLACE = 4, CHANGED = 8;
}  // namespace

bool MaterialEntityControl::datetime_digits_(uint16_t &value) const {
  value = 0;
  if ((this->datetime_.flags & EDITOR) == 0 || this->datetime_.digit_count == 0 ||
      this->datetime_.selected_field >= 6 ||
      this->datetime_.digit_count > (this->datetime_.selected_field == 0 ? 4 : 2))
    return false;
  for (uint8_t index = 0; index < this->datetime_.digit_count; index++) {
    const char digit = this->datetime_.digits[index];
    if (digit < '0' || digit > '9')
      return false;
    value = value * 10 + digit - '0';
  }
  return control_datetime_field_valid(this->datetime_.selected_field, value);
}

bool MaterialEntityControl::datetime_effective_(ControlDatetimeTuple &value) const {
  const auto *state = this->state_.family_if<Snapshot>();
  if (!this->is_datetime() || state == nullptr || (this->datetime_.flags & ACTIVE) == 0 ||
      this->datetime_.baseline_generation != state->state_generation)
    return false;
  value = this->datetime_.value;
  if ((this->datetime_.flags & EDITOR) != 0) {
    uint16_t digits;
    if (!this->datetime_digits_(digits))
      return false;
    value.values[this->datetime_.selected_field] = digits;
  }
  return control_datetime_tuple_valid(value, state->mode);
}

ControlDatetimeDraftView MaterialEntityControl::datetime_draft() const {
  ControlDatetimeDraftView view;
  const auto *state = this->state_.family_if<Snapshot>();
  if (!this->is_datetime() || state == nullptr)
    return view;
  view.active = (this->datetime_.flags & ACTIVE) != 0;
  view.value = view.active ? this->datetime_.value : state->value;
  view.digits = this->datetime_.digits;
  view.baseline_generation = this->datetime_.baseline_generation;
  view.revision = this->datetime_.revision;
  view.selected_field = this->datetime_.selected_field;
  view.editor_open = (this->datetime_.flags & EDITOR) != 0;
  view.replace_on_first_digit = (this->datetime_.flags & REPLACE) != 0;
  view.changed_elsewhere = (this->datetime_.flags & CHANGED) != 0;
  uint16_t digits;
  view.field_valid = !view.editor_open || this->datetime_digits_(digits);
  ControlDatetimeTuple effective;
  view.tuple_valid = view.active ? this->datetime_effective_(effective)
                                 : state->available() && control_datetime_tuple_valid(state->value, state->mode);
  view.dirty =
      view.active && (this->datetime_.value != state->value ||
                      (view.editor_open && (!view.field_valid || digits != state->value.values[view.selected_field])));
  view.can_save = this->can_datetime_input(Input::CONTROL_DATETIME_INPUT_SAVE);
  return view;
}

bool MaterialEntityControl::can_datetime_input(Input input, uint8_t argument) const {
  const auto *state = this->state_.family_if<Snapshot>();
  if (!this->is_datetime() || !this->view_ready_ || this->command_pending_ || (this->extra_peer_profiles_ & 64U) == 0 ||
      state == nullptr || !state->available() || !state->can_set || this->datetime_.revision == CONTROL_MAX_SEQUENCE ||
      ((this->datetime_.flags & ACTIVE) != 0 && this->datetime_.baseline_generation != state->state_generation))
    return false;
  if (input != Input::CONTROL_DATETIME_INPUT_SELECT_FIELD && input != Input::CONTROL_DATETIME_INPUT_DIGIT &&
      argument != 0)
    return false;
  const bool editor = (this->datetime_.flags & EDITOR) != 0;
  uint16_t value;
  switch (input) {
    case Input::CONTROL_DATETIME_INPUT_SELECT_FIELD:
      return argument < 6 && (control_datetime_mask(state->mode) & (1U << argument)) != 0 &&
             (!editor || argument == this->datetime_.selected_field || this->datetime_digits_(value));
    case Input::CONTROL_DATETIME_INPUT_DIGIT:
      return editor && argument <= 9 &&
             ((this->datetime_.flags & REPLACE) != 0 ||
              this->datetime_.digit_count < (this->datetime_.selected_field == 0 ? 4 : 2));
    case Input::CONTROL_DATETIME_INPUT_BACKSPACE:
      return editor;
    case Input::CONTROL_DATETIME_INPUT_ACCEPT_FIELD:
      return editor && this->datetime_digits_(value);
    case Input::CONTROL_DATETIME_INPUT_STEP_DOWN:
    case Input::CONTROL_DATETIME_INPUT_STEP_UP:
      return editor && this->datetime_digits_(value) &&
             control_datetime_field_valid(this->datetime_.selected_field,
                                          input == Input::CONTROL_DATETIME_INPUT_STEP_DOWN ? value - 1 : value + 1);
    case Input::CONTROL_DATETIME_INPUT_CANCEL:
      return (this->datetime_.flags & (ACTIVE | CHANGED)) != 0;
    case Input::CONTROL_DATETIME_INPUT_SAVE: {
      ControlDatetimeTuple effective;
      return this->command_seq_ < CONTROL_MAX_SEQUENCE && this->datetime_effective_(effective);
    }
    default:
      return false;
  }
}

bool MaterialEntityControl::begin_datetime_input(uint32_t pointer, Input input, uint8_t argument, uint32_t now) {
  if (!this->can_input_(now) || this->dragging_ || !this->can_datetime_input(input, argument))
    return false;
  this->pointer_id_ = pointer;
  this->datetime_.pressed_generation = this->state_.family_if<Snapshot>()->state_generation;
  this->datetime_.pressed_revision = this->datetime_.revision;
  this->datetime_.pressed_selection = this->admission_generation_;
  this->datetime_.input = input;
  this->datetime_.argument = argument;
  this->dragging_ = true;
  return true;
}

void MaterialEntityControl::clear_datetime_draft_(bool changed) {
  const uint32_t revision = this->datetime_.revision;
  this->datetime_ = {};
  this->datetime_.revision = revision < CONTROL_MAX_SEQUENCE ? revision + 1 : revision;
  this->datetime_.flags = changed ? CHANGED : 0;
}

bool MaterialEntityControl::advance_datetime_revision_() {
  if (this->datetime_.revision >= CONTROL_MAX_SEQUENCE - 1) {
    this->clear_datetime_draft_();
    this->dragging_ = false;
    return false;
  }
  this->datetime_.revision++;
  return true;
}

void MaterialEntityControl::open_datetime_field_(uint8_t field) {
  this->datetime_.selected_field = field;
  this->datetime_.digits.fill(0);
  this->datetime_.digit_count = snprintf(this->datetime_.digits.data(), this->datetime_.digits.size(), "%u",
                                         static_cast<unsigned>(this->datetime_.value.values[field]));
  this->datetime_.flags = ACTIVE | EDITOR | REPLACE;
}

bool MaterialEntityControl::release_datetime_input(uint32_t pointer, uint32_t now) {
  if (!this->can_input_(now) || !this->dragging_ || this->pointer_id_ != pointer)
    return false;
  this->dragging_ = false;
  const auto *state = this->state_.family_if<Snapshot>();
  const Input input = this->datetime_.input;
  const uint8_t argument = this->datetime_.argument;
  if (state == nullptr || !this->can_datetime_input(input, argument) ||
      this->datetime_.pressed_generation != state->state_generation ||
      this->datetime_.pressed_revision != this->datetime_.revision ||
      this->datetime_.pressed_selection != this->admission_generation_)
    return false;
  uint16_t digits = 0;
  if ((this->datetime_.flags & EDITOR) != 0)
    this->datetime_digits_(digits);
  if (input == Input::CONTROL_DATETIME_INPUT_SAVE) {
    ControlDatetimeTuple target;
    if (!this->datetime_effective_(target) || !this->advance_datetime_revision_())
      return false;
    ControlMessage message;
    message.kind = ControlKind::CONTROL_KIND_COMMAND;
    message.identity = this->identity_;
    message.operation = ControlOperation::CONTROL_OPERATION_DATETIME_SET;
    message.command_seq = ++this->command_seq_;
    message.expected_state_generation = state->state_generation;
    auto &payload = message.state.emplace_family<Snapshot>();
    payload.value = target;
    payload.mode = state->mode;
    this->datetime_.value = target;
    this->datetime_.flags = ACTIVE;
    this->datetime_.selected_field = 255;
    this->datetime_.digits.fill(0);
    this->datetime_.digit_count = 0;
    this->pending_panel_operation_ = message.operation;
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
  if (input == Input::CONTROL_DATETIME_INPUT_SELECT_FIELD && (this->datetime_.flags & EDITOR) != 0 &&
      argument == this->datetime_.selected_field)
    return true;
  if (input == Input::CONTROL_DATETIME_INPUT_BACKSPACE && this->datetime_.digit_count == 0 &&
      (this->datetime_.flags & REPLACE) == 0)
    return true;
  if (input == Input::CONTROL_DATETIME_INPUT_CANCEL) {
    this->clear_datetime_draft_();
  } else {
    if (!this->advance_datetime_revision_())
      return false;
    switch (input) {
      case Input::CONTROL_DATETIME_INPUT_SELECT_FIELD:
        if ((this->datetime_.flags & ACTIVE) == 0) {
          this->datetime_.value = state->value;
          this->datetime_.baseline_generation = state->state_generation;
        } else if ((this->datetime_.flags & EDITOR) != 0) {
          this->datetime_.value.values[this->datetime_.selected_field] = digits;
        }
        this->open_datetime_field_(argument);
        break;
      case Input::CONTROL_DATETIME_INPUT_DIGIT:
        if ((this->datetime_.flags & REPLACE) != 0) {
          this->datetime_.digit_count = 0;
          this->datetime_.digits.fill(0);
        }
        this->datetime_.flags &= ~REPLACE;
        this->datetime_.digits[this->datetime_.digit_count++] = '0' + argument;
        this->datetime_.digits[this->datetime_.digit_count] = 0;
        break;
      case Input::CONTROL_DATETIME_INPUT_BACKSPACE:
        this->datetime_.flags &= ~REPLACE;
        if (this->datetime_.digit_count != 0)
          this->datetime_.digits[--this->datetime_.digit_count] = 0;
        break;
      case Input::CONTROL_DATETIME_INPUT_ACCEPT_FIELD:
        this->datetime_.value.values[this->datetime_.selected_field] = digits;
        this->datetime_.flags = ACTIVE;
        this->datetime_.selected_field = 255;
        this->datetime_.digits.fill(0);
        this->datetime_.digit_count = 0;
        break;
      case Input::CONTROL_DATETIME_INPUT_STEP_DOWN:
      case Input::CONTROL_DATETIME_INPUT_STEP_UP:
        digits = input == Input::CONTROL_DATETIME_INPUT_STEP_DOWN ? digits - 1 : digits + 1;
        this->datetime_.digit_count =
            snprintf(this->datetime_.digits.data(), this->datetime_.digits.size(), "%u", static_cast<unsigned>(digits));
        this->datetime_.flags |= REPLACE;
        break;
      default:
        return false;
    }
  }
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_PREVIEW);
  return true;
}

void MaterialEntityControl::cancel_datetime_input() { this->cancel_drag(); }

bool MaterialEntityControl::accept_datetime_state_(const ControlSnapshot &snapshot) {
  const auto *before = this->state_.family_if<Snapshot>();
  const auto *after = snapshot.family_if<Snapshot>();
  if (before == nullptr || after == nullptr)
    return false;
  if (before->mode != after->mode || before->can_set != after->can_set) {
    this->revoke_(ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED, true);
    return false;
  }
  if (after->state_generation < before->state_generation)
    return false;
  if (after->state_generation == before->state_generation) {
    if (before->source != after->source || before->value != after->value) {
      this->revoke_(ControlReason::CONTROL_REASON_SELECTION_CHANGED, true);
      return false;
    }
  } else {
    const bool changed = (this->datetime_.flags & ACTIVE) != 0 && !this->command_pending_;
    this->clear_datetime_draft_(changed);
    this->cancel_drag();
  }
  return true;
}

}  // namespace esphome::lvgl_material
#endif

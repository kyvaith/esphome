#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS

namespace esphome::lvgl_material {
namespace {
using Extra = ControlExtraSnapshot;
bool class_token(std::string_view value) {
  if (value.empty())
    return true;
  if (value[0] < 'a' || value[0] > 'z')
    return false;
  for (char c : value)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
      return false;
  return true;
}
bool text_status(uint8_t status, std::string_view text, bool known) {
  return status <= 4 && number_plain_utf8(text) && (status == 0 ? !text.empty() : text.empty()) &&
         (known ? status != 4 : status == 4);
}
}  // namespace

uint8_t control_extra_eligible(const Extra &extra) {
  const auto *lock = std::get_if<Extra::Lock>(&extra.data);
  return extra.type == 4 && extra.available() && lock != nullptr && lock->capability &&
                 (lock->phase == 1 || lock->phase == 4 || lock->phase == 6)
             ? 1
             : 0;
}

bool control_valid_extra_snapshot(const ControlSnapshot &snapshot) {
  const auto *extra = snapshot.family_if<Extra>();
  if (extra == nullptr || snapshot.state_seq == 0 || snapshot.state_seq > CONTROL_MAX_SEQUENCE ||
      snapshot.ttl_ms != CONTROL_LEASE_MS || !number_plain_utf8(snapshot.title.view()) ||
      extra->state_generation == 0 || extra->state_generation > CONTROL_MAX_SEQUENCE || extra->source > 2 ||
      !number_plain_utf8(extra->updated.view()) || snapshot.on || snapshot.brightness_known ||
      snapshot.brightness_pct != 0 || snapshot.caps.brightness || snapshot.caps.turn_on || snapshot.caps.turn_off)
    return false;
  if (extra->type == 0) {
    const auto *sensor = std::get_if<Extra::Sensor>(&extra->data);
    return sensor != nullptr && class_token(sensor->device_class.view()) &&
           text_status(sensor->status, sensor->value.view(), extra->available()) &&
           number_plain_utf8(sensor->unit.view());
  }
  if (extra->type == 1 || extra->type == 3) {
    const auto *state = std::get_if<Extra::State>(&extra->data);
    if (state == nullptr || !text_status(state->status, state->text.view(), extra->available()))
      return false;
    if (extra->type == 1)
      return class_token(state->device_class.view()) && (state->phase <= 1 || state->phase == Extra::UNKNOWN_BOOL) &&
             (extra->available() ? state->phase != Extra::UNKNOWN_BOOL || state->status == 2
                                 : state->phase == Extra::UNKNOWN_BOOL);
    return state->device_class.view().empty() && state->phase <= 10 &&
           (extra->available() ? state->phase != 10 || state->status == 2 : state->phase == 10);
  }
  if (extra->type == 2) {
    const auto *calendar = std::get_if<Extra::Calendar>(&extra->data);
    if (calendar == nullptr || calendar->event > 3 || calendar->truncated > 7 ||
        !number_plain_utf8(calendar->summary.view()) || !number_plain_utf8(calendar->start.view()) ||
        !number_plain_utf8(calendar->end.view()) || !number_plain_utf8(calendar->zone.view()) ||
        !number_plain_utf8(calendar->location.view()) || !number_plain_utf8(calendar->description.view()))
      return false;
    if ((extra->available() && calendar->event == 3) || (!extra->available() && calendar->event != 3))
      return false;
    if (calendar->event == 0)
      return calendar->active <= 1 && calendar->all_day <= 1 && !calendar->start.view().empty() &&
             !calendar->end.view().empty() && !calendar->zone.view().empty();
    return calendar->active == Extra::UNKNOWN_BOOL && calendar->all_day == Extra::UNKNOWN_BOOL &&
           calendar->truncated == 0 && calendar->summary.view().empty() && calendar->start.view().empty() &&
           calendar->end.view().empty() && calendar->zone.view().empty() && calendar->location.view().empty() &&
           calendar->description.view().empty();
  }
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  if (extra->type == 7)
    return control_valid_agenda(*extra);
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  if (extra->type == 8)
    return control_valid_statistics(*extra);
#endif
  if (extra->type == 4) {
    const auto *lock = std::get_if<Extra::Lock>(&extra->data);
    return lock != nullptr && lock->phase <= 7 && (extra->available() || lock->phase == 7);
  }
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
  if (extra->type == 5 || extra->type == 6)
    return control_valid_info_context(*extra);
#endif
  return false;
}

bool control_extra_metadata_equal(const Extra &first, const Extra &second) {
  if (first.type != second.type || first.data.index() != second.data.index())
    return false;
  if (const auto *sensor = std::get_if<Extra::Sensor>(&first.data))
    return sensor->device_class == std::get_if<Extra::Sensor>(&second.data)->device_class;
  if (const auto *state = std::get_if<Extra::State>(&first.data))
    return state->device_class == std::get_if<Extra::State>(&second.data)->device_class;
  if (const auto *lock = std::get_if<Extra::Lock>(&first.data))
    return lock->capability == std::get_if<Extra::Lock>(&second.data)->capability;
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
  if (std::get_if<Extra::Person>(&first.data) != nullptr || std::get_if<Extra::Sun>(&first.data) != nullptr)
    return true;
#endif
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  if (std::get_if<Extra::Agenda>(&first.data) != nullptr)
    return true;
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  if (std::get_if<Extra::Statistics>(&first.data) != nullptr)
    return true;
#endif
  return std::get_if<Extra::Calendar>(&first.data) != nullptr;
}

bool MaterialEntityControl::set_extra_profiles(uint16_t profiles) {
  constexpr uint16_t MAX_PROFILES = 3
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
                                    | 4
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
                                    | 24
#endif
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
                                    | 256
#endif
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
                                    | 32
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                                    | 128
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
                                    | 64
#endif
      ;
  if (this->in_callback_ || (profiles & ~MAX_PROFILES) != 0 || ((profiles & 16U) != 0 && (profiles & 8U) == 0) ||
      ((profiles & 32U) != 0 && (profiles & 1U) == 0) || ((profiles & 256U) != 0 && (profiles & 24U) != 24U))
    return false;
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  if ((profiles & 128U) != 0 && this->text_storage_ == nullptr)
    return false;
#endif
  if (this->extra_profiles_ == profiles)
    return true;
  if (this->is_extra()
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
      || this->is_text()
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
      || this->is_datetime()
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
      || this->is_action()
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
      || this->is_media()
#endif
  )
    this->revoke_(ControlReason::CONTROL_REASON_UNSUPPORTED, true);
  this->extra_profiles_ = profiles;
  this->extra_peer_profiles_ = 0;
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  this->agenda_peer_ = false;
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  this->statistics_peer_ = false;
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  this->action_parameters_peer_ = false;
#endif

  return true;
}

bool MaterialEntityControl::accept_extra_features(const ControlText<32> &channel, uint16_t profiles) {
  if (this->in_callback_ || !this->enabled_ || !this->generic_ || this->selected_ || this->open_pending_ ||
      !control_valid_hex(channel) || channel != this->channel_ || profiles == 0 ||
      ((profiles & 16U) != 0 && (profiles & 8U) == 0) || ((profiles & 32U) != 0 && (profiles & 1U) == 0) ||
      ((profiles & 256U) != 0 && (profiles & 24U) != 24U) || (profiles & ~this->extra_profiles_) != 0)
    return false;
  this->extra_peer_profiles_ = profiles;
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  this->agenda_peer_ = false;
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  this->statistics_peer_ = false;
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  this->action_parameters_peer_ = false;
#endif

  return true;
}

bool MaterialEntityControl::accept_extra_state_(const ControlSnapshot &state) {
  const auto *previous = this->state_.family_if<Extra>();
  const auto *next = state.family_if<Extra>();
  if (previous == nullptr || next == nullptr)
    return false;
  if (!control_extra_metadata_equal(*previous, *next)) {
    this->revoke_(ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED, true);
    return false;
  }
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  if (next->type == 7)
    return this->accept_agenda_state_(state);
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  if (next->type == 8 && next->state_generation == previous->state_generation &&
      (next->source != previous->source || next->data != previous->data)) {
    this->revoke_(ControlReason::CONTROL_REASON_SELECTION_CHANGED, true);
    return false;
  }
#endif
  if (next->state_generation < previous->state_generation)
    return false;
  if (next->state_generation == previous->state_generation) {
    const auto *lock = std::get_if<Extra::Lock>(&next->data);
    if (next->source != previous->source ||
        (lock != nullptr && lock->phase != std::get_if<Extra::Lock>(&previous->data)->phase)) {
      this->revoke_(ControlReason::CONTROL_REASON_SELECTION_CHANGED, true);
      return false;
    }
  } else {
    this->cancel_drag();
  }
  return true;
}

bool MaterialEntityControl::can_extra_lock() const {
  const auto *extra = this->state_.family_if<Extra>();
  return this->is_extra() && this->view_ready_ && !this->command_pending_ && extra != nullptr &&
         (this->extra_peer_profiles_ & 2) != 0 && control_extra_eligible(*extra) != 0;
}

bool MaterialEntityControl::begin_extra_lock(uint32_t pointer, uint32_t now) {
  if (!this->can_input_(now) || !this->can_extra_lock() || this->dragging_)
    return false;
  this->pointer_id_ = pointer;
  this->pressed_generation_ = this->state_.family_if<Extra>()->state_generation;
  this->dragging_ = true;
  return true;
}

bool MaterialEntityControl::release_extra_lock(uint32_t pointer, uint32_t now) {
  if (!this->can_input_(now) || !this->is_extra() || !this->dragging_ || this->pointer_id_ != pointer)
    return false;
  this->dragging_ = false;
  if (!this->can_extra_lock() || this->pressed_generation_ != this->state_.family_if<Extra>()->state_generation ||
      this->command_seq_ == CONTROL_MAX_SEQUENCE)
    return false;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_COMMAND;
  message.operation = ControlOperation::CONTROL_OPERATION_EXTRA_LOCK;
  message.identity = this->identity_;
  message.command_seq = ++this->command_seq_;
  message.expected_state_generation = this->pressed_generation_;
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

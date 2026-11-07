#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
#include <algorithm>
#include <cstring>

namespace esphome::lvgl_material {
namespace {
using Input = ControlTextInput;
using Snapshot = ControlTextSnapshot;
using Reason = ControlTextDraftReason;

size_t encode_scalar(uint32_t scalar, char (&bytes)[4]) {
  if (scalar < 0x80) {
    bytes[0] = scalar;
    return 1;
  }
  if (scalar < 0x800) {
    bytes[0] = 0xc0 | (scalar >> 6);
    bytes[1] = 0x80 | (scalar & 0x3f);
    return 2;
  }
  if (scalar < 0x10000) {
    bytes[0] = 0xe0 | (scalar >> 12);
    bytes[1] = 0x80 | ((scalar >> 6) & 0x3f);
    bytes[2] = 0x80 | (scalar & 0x3f);
    return 3;
  }
  bytes[0] = 0xf0 | (scalar >> 18);
  bytes[1] = 0x80 | ((scalar >> 12) & 0x3f);
  bytes[2] = 0x80 | ((scalar >> 6) & 0x3f);
  bytes[3] = 0x80 | (scalar & 0x3f);
  return 4;
}
}  // namespace

bool MaterialEntityControl::set_text_storage(ControlTextStorage *storage, const uint32_t *repertoire, size_t count) {
  if (this->in_callback_ || this->enabled_ || this->is_bound() || storage == nullptr || repertoire == nullptr ||
      count == 0 || count > 1024)
    return false;
  for (size_t i = 0; i < count; i++)
    if (!control_text_scalar_allowed(repertoire[i], nullptr, 0) || (i != 0 && repertoire[i - 1] >= repertoire[i]))
      return false;
  this->clear_text_storage_();
  this->text_storage_ = storage;
  this->clear_text_storage_();
  storage->repertoire = repertoire;
  storage->repertoire_size = count;
  return true;
}

void MaterialEntityControl::clear_text_draft_(bool changed) {
  if (this->text_storage_ == nullptr)
    return;
  auto &text = *this->text_storage_;
  text.draft.bytes.fill(0);
  text.active = false;
  text.changed_elsewhere = changed;
  text.cursor = 0;
  text.baseline_generation = 0;
  text.pressed_generation = text.pressed_revision = text.pressed_selection = text.pressed_map = text.argument = 0;
  if (text.revision < CONTROL_MAX_SEQUENCE)
    text.revision++;
}

void MaterialEntityControl::clear_text_storage_() {
  this->clear_text_draft_();
  if (this->text_storage_ != nullptr) {
    this->text_storage_->source.bytes.fill(0);
    this->text_storage_->rx.clear();
  }
}

ControlTextDraftView MaterialEntityControl::text_draft() const {
  ControlTextDraftView view;
  const auto *state = this->state_.family_if<Snapshot>();
  if (!this->is_text() || this->text_storage_ == nullptr || state == nullptr)
    return view;
  const auto &text = *this->text_storage_;
  view.source = state->available() ? text.source.view() : std::string_view{};
  view.value = text.active ? text.draft.view() : view.source;
  view.active = text.active;
  view.cursor = text.cursor;
  view.baseline_generation = text.baseline_generation;
  view.revision = text.revision;
  view.changed_elsewhere = text.changed_elsewhere;
  view.dirty = text.active && view.value != view.source;
  view.reason =
      control_text_validate(view.value, state->minimum, state->maximum, text.repertoire, text.repertoire_size);
  view.valid = state->available() && view.reason == Reason::CONTROL_TEXT_DRAFT_REASON_NONE;
  control_text_measure(view.value, view.codepoints);
  view.can_save = this->can_text_input(Input::CONTROL_TEXT_INPUT_SAVE);
  return view;
}

bool MaterialEntityControl::can_text_input(Input input, uint32_t argument) const {
  const auto *state = this->state_.family_if<Snapshot>();
  if (!this->is_text() || this->text_storage_ == nullptr || !this->view_ready_ || this->command_pending_ ||
      (this->extra_peer_profiles_ & 128U) == 0 || state == nullptr || !state->available())
    return false;
  const auto &text = *this->text_storage_;
  if (text.revision >= CONTROL_MAX_SEQUENCE || (text.active && text.baseline_generation != state->state_generation))
    return false;
  if (input != Input::CONTROL_TEXT_INPUT_INSERT && input != Input::CONTROL_TEXT_INPUT_SELECT_CURSOR && argument != 0)
    return false;
  if (input == Input::CONTROL_TEXT_INPUT_VIEW)
    return true;
  if (!state->can_set)
    return false;
  if (input == Input::CONTROL_TEXT_INPUT_BEGIN_EDITOR)
    return !text.active;
  if (input == Input::CONTROL_TEXT_INPUT_CANCEL)
    return text.active || text.changed_elsewhere;
  if (!text.active)
    return false;
  const auto draft = text.draft.view();
  uint16_t length;
  if (!control_text_measure(draft, length) || text.cursor > length)
    return false;
  switch (input) {
    case Input::CONTROL_TEXT_INPUT_INSERT: {
      char encoded[4];
      return control_text_scalar_allowed(argument, text.repertoire, text.repertoire_size) &&
             length < CONTROL_TEXT_MAX_CODEPOINTS &&
             draft.size() + encode_scalar(argument, encoded) <= CONTROL_TEXT_MAX_BYTES;
    }
    case Input::CONTROL_TEXT_INPUT_BACKSPACE:
    case Input::CONTROL_TEXT_INPUT_MOVE_LEFT:
      return text.cursor != 0;
    case Input::CONTROL_TEXT_INPUT_MOVE_RIGHT:
      return text.cursor < length;
    case Input::CONTROL_TEXT_INPUT_SELECT_CURSOR:
      return argument <= length;
    case Input::CONTROL_TEXT_INPUT_SAVE:
      return this->command_seq_ < CONTROL_MAX_SEQUENCE && draft != text.source.view() &&
             control_text_validate(draft, state->minimum, state->maximum, text.repertoire, text.repertoire_size) ==
                 Reason::CONTROL_TEXT_DRAFT_REASON_NONE;
    default:
      return false;
  }
}

bool MaterialEntityControl::begin_text_input(uint32_t pointer, Input input, uint32_t argument, uint32_t map_generation,
                                             uint32_t now) {
  if (!this->can_input_(now) || this->dragging_ || !this->can_text_input(input, argument))
    return false;
  auto &text = *this->text_storage_;
  this->pointer_id_ = pointer;
  text.pressed_generation = this->state_.family_if<Snapshot>()->state_generation;
  text.pressed_revision = text.revision;
  text.pressed_selection = this->admission_generation_;
  text.pressed_map = map_generation;
  text.input = input;
  text.argument = argument;
  this->dragging_ = true;
  return true;
}

bool MaterialEntityControl::release_text_input(uint32_t pointer, uint32_t map_generation, uint32_t now) {
  if (!this->can_input_(now) || !this->dragging_ || this->pointer_id_ != pointer || this->text_storage_ == nullptr)
    return false;
  this->dragging_ = false;
  auto &text = *this->text_storage_;
  const auto *state = this->state_.family_if<Snapshot>();
  const auto input = text.input;
  const auto argument = text.argument;
  if (state == nullptr || !this->can_text_input(input, argument) ||
      text.pressed_generation != state->state_generation || text.pressed_revision != text.revision ||
      text.pressed_selection != this->admission_generation_ || text.pressed_map != map_generation)
    return false;
  if (input == Input::CONTROL_TEXT_INPUT_VIEW ||
      (input == Input::CONTROL_TEXT_INPUT_SELECT_CURSOR && argument == text.cursor))
    return true;
  if (text.revision >= CONTROL_MAX_SEQUENCE - 1) {
    this->clear_text_draft_();
    return false;
  }
  if (input == Input::CONTROL_TEXT_INPUT_SAVE) {
    ControlMessage message;
    message.kind = ControlKind::CONTROL_KIND_COMMAND;
    message.identity = this->identity_;
    message.operation = ControlOperation::CONTROL_OPERATION_TEXT_SET;
    message.command_seq = ++this->command_seq_;
    message.expected_state_generation = state->state_generation;
    message.state.emplace_family<Snapshot>();
    text.revision++;
    this->pending_panel_operation_ = message.operation;
    this->command_at_ = now;
    this->command_pending_ = true;
    this->last_result_ = ControlResult::CONTROL_RESULT_PENDING;
    this->last_reason_ = ControlReason::CONTROL_REASON_NONE;
    if (!this->emit_(message, {text.draft.view(), true})) {
      this->command_pending_ = false;
      this->last_result_ = ControlResult::CONTROL_RESULT_REJECTED;
      this->last_reason_ = ControlReason::CONTROL_REASON_CONNECTION_LOST;
      this->disconnect();
      this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_RESULT);
      return false;
    }
    // The sink may process external callbacks; do not dereference draft/state now.
    this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_SENDING);
    return true;
  }
  if (input == Input::CONTROL_TEXT_INPUT_CANCEL) {
    this->clear_text_draft_();
  } else {
    text.revision++;
    if (input == Input::CONTROL_TEXT_INPUT_BEGIN_EDITOR) {
      text.draft.assign(text.source.view());
      text.active = true;
      text.changed_elsewhere = false;
      text.baseline_generation = state->state_generation;
      control_text_measure(text.draft.view(), text.cursor);
    } else if (input == Input::CONTROL_TEXT_INPUT_MOVE_LEFT) {
      text.cursor--;
    } else if (input == Input::CONTROL_TEXT_INPUT_MOVE_RIGHT) {
      text.cursor++;
    } else if (input == Input::CONTROL_TEXT_INPUT_SELECT_CURSOR) {
      text.cursor = argument;
    } else {
      const auto draft = text.draft.view();
      size_t offset;
      if (!control_text_offset(draft, text.cursor, offset))
        return false;
      if (input == Input::CONTROL_TEXT_INPUT_INSERT) {
        char encoded[4];
        const size_t bytes = encode_scalar(argument, encoded);
        std::memmove(text.draft.bytes.data() + offset + bytes, text.draft.bytes.data() + offset,
                     draft.size() - offset + 1);
        std::memcpy(text.draft.bytes.data() + offset, encoded, bytes);
        text.cursor++;
      } else if (input == Input::CONTROL_TEXT_INPUT_BACKSPACE) {
        size_t previous;
        if (!control_text_offset(draft, text.cursor - 1, previous))
          return false;
        std::memmove(text.draft.bytes.data() + previous, text.draft.bytes.data() + offset, draft.size() - offset + 1);
        // Retired tail bytes must not retain previously visible text.
        for (size_t index = draft.size() - (offset - previous) + 1; index <= draft.size(); index++)
          text.draft.bytes[index] = 0;
        text.cursor--;
      }
    }
  }
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_PREVIEW);
  return true;
}

void MaterialEntityControl::cancel_text_input() { this->cancel_drag(); }

bool MaterialEntityControl::accept_text_state_(const ControlSnapshot &snapshot, ControlTextValueView value) {
  const auto *before = this->state_.family_if<Snapshot>();
  const auto *after = snapshot.family_if<Snapshot>();
  if (before == nullptr || after == nullptr || this->text_storage_ == nullptr)
    return false;
  const bool withheld_domain =
      (before->source == 3 && before->reason <= 4) || (after->source == 3 && after->reason <= 4);
  if (before->minimum != after->minimum || before->maximum != after->maximum ||
      before->bounds_known != after->bounds_known || before->can_set != after->can_set ||
      (withheld_domain && (before->source != after->source || before->reason != after->reason))) {
    this->revoke_(ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED, true);
    return false;
  }
  if (after->state_generation < before->state_generation)
    return false;
  if (after->state_generation == before->state_generation &&
      (before->source != after->source || before->reason != after->reason ||
       (after->available() && this->text_storage_->source.view() != value.value))) {
    this->revoke_(ControlReason::CONTROL_REASON_SELECTION_CHANGED, true);
    return false;
  }
  return true;
}

void MaterialEntityControl::commit_text_state_(const ControlSnapshot &snapshot, ControlTextValueView value,
                                               bool grant) {
  const auto *after = snapshot.family_if<Snapshot>();
  if (after == nullptr || this->text_storage_ == nullptr)
    return;
  auto &text = *this->text_storage_;
  const auto *before = this->state_.family_if<Snapshot>();
  if (grant || before == nullptr || before->state_generation != after->state_generation) {
    const bool changed = !grant && text.active && !this->command_pending_;
    this->clear_text_draft_(changed);
    this->cancel_drag();
  }
  if (value.present) {
    // Normal receive uses independent RX staging; memmove also makes the public
    // synchronous API safe when a caller supplies a view into retained source.
    if (!value.value.empty())
      std::memmove(text.source.bytes.data(), value.value.data(), value.value.size());
    std::fill(text.source.bytes.begin() + value.value.size(), text.source.bytes.end(), 0);
  } else
    text.source.bytes.fill(0);
}

}  // namespace esphome::lvgl_material
#endif

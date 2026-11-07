#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
#include <algorithm>
#include <cstring>
namespace esphome::lvgl_material {
namespace {
using Facet = ControlFacet;
using Op = ControlOperation;
bool decimal_text(std::string_view value, int64_t minimum, int64_t maximum, bool empty = true) {
  if (value.empty())
    return empty;
  int64_t number;
  char canonical[24];
  return value.size() <= 12 && number_parse_decimal(value, number) && number >= minimum * NUMBER_SCALE &&
         number <= maximum * NUMBER_SCALE && number_format_decimal(number, canonical, sizeof(canonical)) &&
         value == canonical;
}
bool numeric_valid(const ControlFacetNumber &number, bool nullable, std::string_view unit) {
  if (number.domain == MaterialNumberDomain{})
    return nullable && !number.known && number.index == 0;
  if (!number.domain.valid() || std::string_view(number.domain.unit.data()) != unit)
    return false;
  if (!number.known)
    return number.index == 0 && number.value.view() == "--";
  int64_t value;
  char canonical[24];
  return number.domain.value_at(number.index, value) && number_format_decimal(value, canonical, sizeof(canonical)) &&
         number.value.view() == canonical;
}
bool numeric_semantic_equal(const ControlFacetNumber &a, const ControlFacetNumber &b) {
  return a.index == b.index && a.known == b.known && a.value == b.value;
}
bool detail_metadata_equal(const ControlDetailSnapshot &a, const ControlDetailSnapshot &b) {
  if (a.climate != b.climate || a.facets != b.facets)
    return false;
  if (a.facet != b.facet)
    return true;
  if (a.data.index() != b.data.index())
    return false;
  if (auto *am = std::get_if<ControlClimateMain>(&a.data)) {
    const auto &bm = std::get<ControlClimateMain>(b.data);
    return am->unit == bm.unit && am->minimum == bm.minimum && am->maximum == bm.maximum &&
           am->number.domain == bm.number.domain;
  }
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  if (auto *range = a.range())
    return range->domain == b.range()->domain;
#endif
  if (auto *an = a.numeric())
    return an->domain == b.numeric()->domain;
  if (auto *ac = a.choice()) {
    auto *bc = b.choice();
    return ac->count == bc->count && ac->options == bc->options;
  }
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  if (auto *color = a.color())
    return color->write_mode == b.color()->write_mode;
#endif
  return true;
}
bool detail_semantic_equal(const ControlDetailSnapshot &a, const ControlDetailSnapshot &b) {
  if (a.available != b.available)
    return false;
  if (a.facet != b.facet)
    return true;
  if (a.editable != b.editable)
    return false;
  if (auto *am = std::get_if<ControlClimateMain>(&a.data)) {
    const auto &bm = std::get<ControlClimateMain>(b.data);
    return am->unit == bm.unit && am->mode == bm.mode && am->target == bm.target && am->low == bm.low &&
           am->high == bm.high && numeric_semantic_equal(am->number, bm.number);
  }
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  if (auto *range = a.range()) {
    const auto &other = *b.range();
    return range->low_index == other.low_index && range->high_index == other.high_index &&
           range->known == other.known && range->mode == other.mode && range->reason == other.reason;
  }
#endif
  if (auto *an = a.numeric())
    return numeric_semantic_equal(*an, *b.numeric());
  if (auto *ac = a.choice())
    return ac->index == b.choice()->index && ac->known == b.choice()->known;
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  if (auto *color = a.color())
    return *color == *b.color();
#endif
  return true;
}
}  // namespace
bool control_detail_facet_supported(const ControlDetailSnapshot &detail, ControlFacet facet) {
  if (facet == Facet::CONTROL_FACET_MAIN)
    return true;
  uint8_t bit = 0;
  if (!detail.climate)
    bit = facet == Facet::CONTROL_FACET_KELVIN   ? 1
          : facet == Facet::CONTROL_FACET_EFFECT ? 2
          :
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
          facet == Facet::CONTROL_FACET_COLOR ? 4
                                              :
#endif
                                              0;
  else if (facet >= Facet::CONTROL_FACET_HUMIDITY && facet <= Facet::CONTROL_FACET_SWING_HORIZONTAL_MODE)
    bit = 1U << (static_cast<uint8_t>(facet) - static_cast<uint8_t>(Facet::CONTROL_FACET_HUMIDITY) + 1);
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  else if (facet == Facet::CONTROL_FACET_RANGE)
    bit = 128;
#endif
  return bit != 0 && (detail.facets & bit) != 0;
}
bool control_valid_detail_snapshot(const ControlSnapshot &snapshot) {
  const auto *d_payload = snapshot.family_if<ControlDetailSnapshot>();
  if (d_payload == nullptr)
    return false;
  const auto &d = *d_payload;
  if (!d.present || snapshot.state_seq == 0 || snapshot.state_seq > CONTROL_MAX_SEQUENCE ||
      snapshot.ttl_ms != CONTROL_LEASE_MS || !number_plain_utf8(snapshot.title.view()) || d.state_generation == 0 ||
      d.state_generation > CONTROL_MAX_SEQUENCE || d.view_seq > CONTROL_MAX_SEQUENCE ||
      (d.facet != Facet::CONTROL_FACET_MAIN && d.view_seq == 0) ||
      d.facets > (d.climate ?
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
                            255
                            :
#else
                            127
                            :
#endif
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
                            7
#else
                            3
#endif
                  ) ||
      !control_detail_facet_supported(d, d.facet))
    return false;
  if (!d.climate) {
    if (!control_valid_light_fields(snapshot) || !d.available)
      return false;
    if (d.facet == Facet::CONTROL_FACET_MAIN)
      return std::holds_alternative<std::monostate>(d.data) && !d.editable;
  } else {
    if (snapshot.on || snapshot.brightness_known || snapshot.caps.brightness || snapshot.caps.turn_on ||
        snapshot.caps.turn_off)
      return false;
    if (d.facet == Facet::CONTROL_FACET_MAIN) {
      const auto *main = std::get_if<ControlClimateMain>(&d.data);
      if (main == nullptr || main->unit > 2 || main->mode > 7 || main->activity > 8 || d.available != (main->mode != 0))
        return false;
      for (const auto *text : {&main->target, &main->current, &main->low, &main->high, &main->minimum, &main->maximum})
        if (!decimal_text(text->view(), -100, 300))
          return false;
      if (!d.available && (!main->target.view().empty() || !main->current.view().empty() || !main->low.view().empty() ||
                           !main->high.view().empty() || main->activity != 0))
        return false;
      const char *unit = main->unit == 1 ? "°C" : main->unit == 2 ? "°F" : "";
      if (!numeric_valid(main->number, true, unit) || !main->number.current.view().empty())
        return false;
      const bool valid_domain = main->number.domain.valid();
      if (valid_domain != ((d.facets & 1) != 0) || (valid_domain && main->unit == 0))
        return false;
      if (main->number.known &&
          (!d.available || main->mode == 4 || main->mode == 5 || main->target.view() != main->number.value.view()))
        return false;
      return d.editable == (d.available && valid_domain && main->number.known && (d.facets & 1) != 0 &&
                            main->mode != 4 && main->mode != 5);
    }
  }
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  if (d.facet == Facet::CONTROL_FACET_COLOR)
    return !d.climate && d.color() != nullptr && control_valid_facet_color(*d.color(), d.editable) &&
           snapshot.on == (d.color()->reason != 1 && d.color()->reason != 5);
#endif
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  if (d.facet == Facet::CONTROL_FACET_RANGE)
    return d.climate && d.range() != nullptr && control_valid_facet_range(*d.range(), d.available, d.editable);
#endif
  if (d.facet == Facet::CONTROL_FACET_KELVIN || d.facet == Facet::CONTROL_FACET_HUMIDITY) {
    const auto *number = std::get_if<ControlFacetNumber>(&d.data);
    if (number == nullptr || !numeric_valid(*number, false, d.facet == Facet::CONTROL_FACET_KELVIN ? "K" : "%"))
      return false;
    if (d.facet == Facet::CONTROL_FACET_KELVIN &&
        (!number->current.view().empty() || number->domain.step != NUMBER_SCALE ||
         number->domain.minimum < NUMBER_SCALE))
      return false;
    if (d.facet == Facet::CONTROL_FACET_HUMIDITY &&
        (number->domain.minimum < 0 || number->domain.maximum > 100 * NUMBER_SCALE ||
         number->domain.step % NUMBER_SCALE != 0 || !decimal_text(number->current.view(), 0, 100)))
      return false;
    return d.editable == (d.available && number->known) &&
           (d.available || (!number->known && number->current.view().empty()));
  }
  const auto *choice = std::get_if<ControlFacetChoice>(&d.data);
  if (choice == nullptr || choice->count == 0 || choice->count > choice->options.size() ||
      (!choice->known && choice->index != 255) || (choice->index != 255 && choice->index >= choice->count) ||
      (choice->known && choice->index == 255 && d.facet != Facet::CONTROL_FACET_EFFECT &&
       d.facet != Facet::CONTROL_FACET_PRESET_MODE) ||
      d.editable != (d.available && choice->known) || (!d.available && choice->known))
    return false;
  for (size_t i = 0; i < choice->count; i++) {
    const auto text = choice->options[i].view();
    if (text.empty() || !number_plain_utf8(text) || text == "unknown" || text == "unavailable")
      return false;
    for (size_t j = 0; j < i; j++)
      if (choice->options[i] == choice->options[j])
        return false;
  }
  return true;
}
bool MaterialEntityControl::set_detail_profiles(uint8_t profiles) {
  if (this->in_callback_ || profiles > 3)
    return false;
  if (profiles == this->detail_profiles_)
    return true;
  if (this->is_detail())
    this->revoke_(ControlReason::CONTROL_REASON_UNSUPPORTED, true);
  this->detail_profiles_ = profiles;
  this->detail_peer_profiles_ = 0;
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  this->detail_range_peer_ = false;
#endif
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  this->detail_color_peer_ = false;
  this->clear_detail_color_draft_();
#endif
  return true;
}
bool MaterialEntityControl::accept_detail_features(const ControlText<32> &channel, uint8_t profiles) {
  if (this->in_callback_ || !this->enabled_ || !this->generic_ || this->selected_ || this->open_pending_ ||
      !control_valid_hex(channel) || channel != this->channel_ || profiles == 0 ||
      (profiles & ~this->detail_profiles_) != 0)
    return false;
  this->detail_peer_profiles_ = profiles;
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  this->detail_range_peer_ = false;
#endif
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  this->detail_color_peer_ = false;
  this->clear_detail_color_draft_();
#endif
  return true;
}
bool MaterialEntityControl::request_open_detail(const ControlContext &context, uint32_t now) {
  if (this->in_callback_ || !this->generic_ || (this->detail_peer_profiles_ & 1) == 0 ||
      context.control_kind != ControlEntityKind::CONTROL_ENTITY_LIGHT)
    return false;
  this->detail_open_requested_ = true;
  const bool result = this->request_open(context, now);
  this->detail_open_requested_ = false;
  return result;
}
bool MaterialEntityControl::validate_detail_snapshot_(const ControlMessage &message) const {
  const auto *d_payload = message.state.family_if<ControlDetailSnapshot>();
  if (d_payload == nullptr)
    return false;
  const auto &d = *d_payload;
  if (!control_valid_detail_snapshot(message.state))
    return false;
  if (d.climate) {
    if (this->identity_.context.control_kind != ControlEntityKind::CONTROL_ENTITY_PANEL ||
        (this->detail_peer_profiles_ & 2) == 0)
      return false;
  } else if (!this->detail_open_ || (this->detail_peer_profiles_ & 1) == 0)
    return false;
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  if (!d.climate && (d.facets & 4U) != 0 && !this->detail_color_bound())
    return false;
#endif
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  if (d.climate && (d.facets & 128U) != 0 && !this->detail_range_bound())
    return false;
#endif
  if (message.kind == ControlKind::CONTROL_KIND_GRANT)
    return d.facet == Facet::CONTROL_FACET_MAIN && d.view_seq == 0;
  return true;
}
bool MaterialEntityControl::accept_detail_state_(const ControlSnapshot &snapshot) {
  const auto *before_payload = this->state_.family_if<ControlDetailSnapshot>();
  if (before_payload == nullptr)
    return false;
  const auto &before = *before_payload;
  const auto *after_payload = snapshot.family_if<ControlDetailSnapshot>();
  if (after_payload == nullptr)
    return false;
  const auto &after = *after_payload;
  const bool requested = this->detail_view_pending_ && after.view_seq == this->detail_view_sequence_ &&
                         after.facet == this->detail_requested_facet_;
  const bool current = after.view_seq == before.view_seq && after.facet == before.facet;
  if (!requested && !current)
    return false;
  if (!detail_metadata_equal(before, after)) {
    this->revoke_(ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED, true);
    return false;
  }
  if (after.state_generation < before.state_generation)
    return false;
  if (after.state_generation == before.state_generation &&
      (!detail_semantic_equal(before, after) ||
       (!after.climate &&
        (snapshot.on != this->state_.on || snapshot.brightness_known != this->state_.brightness_known ||
         snapshot.brightness_pct != this->state_.brightness_pct)))) {
    this->revoke_(ControlReason::CONTROL_REASON_SELECTION_CHANGED, true);
    return false;
  }
  if (after.state_generation != before.state_generation || !current) {
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
    this->clear_detail_color_draft_();
#endif
    this->cancel_drag();
  }
  if (requested) {
    if (this->detail_view_state_seq_ == 0)
      this->detail_view_state_seq_ = snapshot.state_seq;
    this->detail_view_ready_ = false;
  }
  return true;
}
bool MaterialEntityControl::can_request_facet(ControlFacet facet) const {
  return this->state_.family_if<ControlDetailSnapshot>() != nullptr && this->enabled_ && this->selected_ &&
         this->is_detail() && !this->command_pending_ && !this->detail_view_pending_ &&
         this->detail_view_sequence_ < CONTROL_MAX_SEQUENCE &&
         control_detail_facet_supported(*this->state_.family_if<ControlDetailSnapshot>(), facet) &&
         (!this->detail_view_ready_ || facet != this->state_.family_if<ControlDetailSnapshot>()->facet);
}
bool MaterialEntityControl::request_facet(ControlFacet facet, uint32_t now) {
  if (this->in_callback_)
    return false;
  this->tick(now);
  if (!this->can_request_facet(facet))
    return false;
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  this->clear_detail_color_draft_();
#endif
  this->cancel_drag();
  this->detail_requested_facet_ = facet;
  this->detail_view_sequence_++;
  this->detail_view_state_seq_ = 0;
  this->detail_view_pending_ = true;
  this->detail_view_at_ = now;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_FACET;
  message.identity = this->identity_;
  message.facet = facet;
  message.view_seq = this->detail_view_sequence_;
  if (!this->emit_(message)) {
    this->disconnect();
    return false;
  }
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_STATE);
  return true;
}
bool MaterialEntityControl::receive_facet_result_(const ControlMessage &message) {
  if (this->state_.family_if<ControlDetailSnapshot>() == nullptr || !this->is_detail() || !this->detail_view_pending_ ||
      message.view_seq != this->detail_view_sequence_ || message.facet != this->detail_requested_facet_ ||
      message.state.state_seq == 0)
    return false;
  if (message.facet_ready) {
    if (message.reason != ControlReason::CONTROL_REASON_NONE || this->detail_view_state_seq_ == 0 ||
        message.state.state_seq < this->detail_view_state_seq_ || message.state.state_seq > this->state_.state_seq ||
        this->state_.family_if<ControlDetailSnapshot>()->view_seq != message.view_seq)
      return false;
    this->detail_view_ready_ = true;
  } else {
    if (message.reason == ControlReason::CONTROL_REASON_NONE || this->detail_view_state_seq_ != 0)
      return false;
    this->last_reason_ = message.reason;
  }
  this->detail_view_pending_ = false;
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_STATE);
  return true;
}
bool MaterialEntityControl::begin_detail_navigation(uint32_t pointer, uint32_t now) {
  if (this->in_callback_)
    return false;
  this->tick(now);
  if (this->state_.family_if<ControlDetailSnapshot>() == nullptr || !this->is_detail() || !this->selected_ ||
      !this->view_ready_ || this->dragging_ || this->command_pending_ || this->detail_view_pending_)
    return false;
  this->pointer_id_ = pointer;
  this->pressed_generation_ = this->state_.family_if<ControlDetailSnapshot>()->state_generation;
  this->detail_pressed_view_ = this->state_.family_if<ControlDetailSnapshot>()->view_seq;
  this->detail_pressed_facet_ = this->state_.family_if<ControlDetailSnapshot>()->facet;
  this->dragging_ = true;
  return true;
}
bool MaterialEntityControl::release_detail_navigation(uint32_t pointer, uint32_t now) {
  if (this->in_callback_)
    return false;
  this->tick(now);
  if (this->state_.family_if<ControlDetailSnapshot>() == nullptr || !this->is_detail() || !this->dragging_ ||
      this->pointer_id_ != pointer)
    return false;
  this->dragging_ = false;
  return this->selected_ && this->view_ready_ && !this->command_pending_ && !this->detail_view_pending_ &&
         this->pressed_generation_ == this->state_.family_if<ControlDetailSnapshot>()->state_generation &&
         this->detail_pressed_view_ == this->state_.family_if<ControlDetailSnapshot>()->view_seq &&
         this->detail_pressed_facet_ == this->state_.family_if<ControlDetailSnapshot>()->facet;
}
ControlNumericView MaterialEntityControl::active_numeric() const {
  if (this->is_detail()) {
    const auto *detail = this->state_.family_if<ControlDetailSnapshot>();
    if (detail == nullptr)
      return {};
    if (const auto *n = detail->numeric())
      return {&n->domain, n->index, n->known, this->state_.family_if<ControlDetailSnapshot>()->editable};
    return {};
  }
  if (this->is_number() && this->state_.family_if<ControlNumberSnapshot>() != nullptr)
    return {&this->state_.family_if<ControlNumberSnapshot>()->domain,
            this->state_.family_if<ControlNumberSnapshot>()->index,
            this->state_.family_if<ControlNumberSnapshot>()->available,
            this->state_.family_if<ControlNumberSnapshot>()->available};
  return {};
}
bool MaterialEntityControl::can_detail_number() const {
  const auto n = this->active_numeric();
  return this->is_detail() && this->selected_ && this->view_ready_ && !this->command_pending_ &&
         !this->detail_view_pending_ && this->detail_view_ready_ && n.domain != nullptr && n.domain->valid() &&
         n.known && n.editable;
}
bool MaterialEntityControl::can_detail_choice(uint8_t index) const {
  if (this->state_.family_if<ControlDetailSnapshot>() == nullptr)
    return false;
  const auto *choice = this->state_.family_if<ControlDetailSnapshot>()->choice();
  return this->is_detail() && this->selected_ && this->view_ready_ && !this->command_pending_ &&
         !this->detail_view_pending_ && this->detail_view_ready_ &&
         this->state_.family_if<ControlDetailSnapshot>()->editable && choice != nullptr && index < choice->count;
}
bool MaterialEntityControl::begin_detail_number(uint32_t pointer, uint32_t now) {
  if (!this->can_detail_number() || !this->begin_detail_navigation(pointer, now))
    return false;
  const auto n = this->active_numeric();
  this->pressed_index_ = n.index;
  this->detail_pressed_known_ = n.known;
  return true;
}
bool MaterialEntityControl::release_detail_number(uint32_t pointer, uint32_t target, uint32_t now) {
  if (!this->release_detail_navigation(pointer, now) || !this->can_detail_number())
    return false;
  const auto n = this->active_numeric();
  if (n.index != this->pressed_index_ || n.known != this->detail_pressed_known_ || target > n.domain->max_index ||
      target == n.index)
    return false;
  return this->send_detail_index_(Op::CONTROL_OPERATION_FACET_NUMBER, target, now);
}
bool MaterialEntityControl::begin_detail_choice(uint32_t pointer, uint8_t target, uint32_t now) {
  if (!this->can_detail_choice(target) || !this->begin_detail_navigation(pointer, now))
    return false;
  const auto *choice = this->state_.family_if<ControlDetailSnapshot>()->choice();
  this->pressed_index_ = choice->index;
  this->detail_pressed_known_ = choice->index != 255;
  this->detail_pressed_choice_ = target;
  return true;
}
bool MaterialEntityControl::release_detail_choice(uint32_t pointer, uint32_t now) {
  if (!this->release_detail_navigation(pointer, now) || !this->can_detail_choice(this->detail_pressed_choice_) ||
      this->state_.family_if<ControlDetailSnapshot>()->choice()->index != this->pressed_index_)
    return false;
  return this->send_detail_index_(Op::CONTROL_OPERATION_FACET_CHOICE, this->detail_pressed_choice_, now);
}
bool MaterialEntityControl::send_detail_index_(ControlOperation operation, uint32_t target, uint32_t now) {
  if (this->command_seq_ == CONTROL_MAX_SEQUENCE)
    return false;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_COMMAND;
  message.identity = this->identity_;
  message.command_seq = ++this->command_seq_;
  message.operation = operation;
  message.facet = this->detail_pressed_facet_;
  message.view_seq = this->detail_pressed_view_;
  message.facet_target_index = target;
  message.expected_index = this->pressed_index_;
  message.expected_index_known = this->detail_pressed_known_;
  message.expected_state_generation = this->pressed_generation_;
  this->command_at_ = now;
  this->command_pending_ = true;
  this->last_result_ = ControlResult::CONTROL_RESULT_PENDING;
  this->last_reason_ = ControlReason::CONTROL_REASON_NONE;
  if (!this->emit_(message)) {
    this->command_pending_ = false;
    this->disconnect();
    return false;
  }
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_SENDING);
  return true;
}
}  // namespace esphome::lvgl_material
#endif

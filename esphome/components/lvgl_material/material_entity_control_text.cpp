#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS

namespace esphome::lvgl_material {

bool control_text_next(std::string_view value, size_t &offset, uint32_t &scalar) {
  if (offset >= value.size())
    return false;
  size_t next = offset;
  const auto first = static_cast<uint8_t>(value[next++]);
  uint8_t remaining;
  uint32_t minimum;
  if (first < 0x80) {
    scalar = first;
    remaining = 0;
    minimum = 0;
  } else if (first >= 0xc2 && first <= 0xdf) {
    scalar = first & 0x1f;
    remaining = 1;
    minimum = 0x80;
  } else if (first >= 0xe0 && first <= 0xef) {
    scalar = first & 0x0f;
    remaining = 2;
    minimum = 0x800;
  } else if (first >= 0xf0 && first <= 0xf4) {
    scalar = first & 7;
    remaining = 3;
    minimum = 0x10000;
  } else {
    return false;
  }
  while (remaining-- != 0) {
    if (next >= value.size())
      return false;
    const auto byte = static_cast<uint8_t>(value[next++]);
    if ((byte & 0xc0) != 0x80)
      return false;
    scalar = (scalar << 6) | (byte & 0x3f);
  }
  if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
    return false;
  offset = next;
  return true;
}

bool control_text_offset(std::string_view value, uint16_t index, size_t &offset) {
  size_t next = 0;
  uint32_t scalar;
  for (uint16_t i = 0; i < index; i++)
    if (!control_text_next(value, next, scalar))
      return false;
  offset = next;
  return true;
}

bool control_text_measure(std::string_view value, uint16_t &count) {
  if (value.size() > CONTROL_TEXT_MAX_BYTES)
    return false;
  size_t offset = 0;
  uint16_t measured = 0;
  uint32_t scalar;
  while (offset < value.size()) {
    if (!control_text_next(value, offset, scalar) || ++measured > CONTROL_TEXT_MAX_CODEPOINTS)
      return false;
  }
  count = measured;
  return true;
}

bool control_text_scalar_allowed(uint32_t scalar, const uint32_t *repertoire, size_t count) {
  if (scalar < 0x20 || scalar == 0x7f || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
    return false;
  // The wire codec validates Unicode and bounds; the configured owner additionally
  // enforces its product/font repertoire. No language is hardcoded here.
  if (repertoire == nullptr)
    return count == 0;
  size_t low = 0, high = count;
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    if (repertoire[middle] < scalar)
      low = middle + 1;
    else
      high = middle;
  }
  return low < count && repertoire[low] == scalar;
}

ControlTextDraftReason control_text_validate(std::string_view value, uint8_t minimum, uint8_t maximum,
                                             const uint32_t *repertoire, size_t count) {
  using Reason = ControlTextDraftReason;
  if (value.size() > CONTROL_TEXT_MAX_BYTES)
    return Reason::CONTROL_TEXT_DRAFT_REASON_BYTE_LIMIT;
  size_t offset = 0;
  uint16_t length = 0;
  uint32_t scalar;
  bool characters = true;
  while (offset < value.size()) {
    if (!control_text_next(value, offset, scalar))
      return Reason::CONTROL_TEXT_DRAFT_REASON_ENCODING;
    if (++length > CONTROL_TEXT_MAX_CODEPOINTS)
      return Reason::CONTROL_TEXT_DRAFT_REASON_ABOVE_MAX;
    characters &= control_text_scalar_allowed(scalar, repertoire, count);
  }
  if (length < minimum)
    return Reason::CONTROL_TEXT_DRAFT_REASON_BELOW_MIN;
  if (length > maximum)
    return Reason::CONTROL_TEXT_DRAFT_REASON_ABOVE_MAX;
  if (!characters)
    return Reason::CONTROL_TEXT_DRAFT_REASON_CHARACTERS;
  if (value == "unknown" || value == "unavailable")
    return Reason::CONTROL_TEXT_DRAFT_REASON_RESERVED;
  return Reason::CONTROL_TEXT_DRAFT_REASON_NONE;
}

bool control_valid_text_metadata(const ControlSnapshot &snapshot) {
  const auto *text = snapshot.family_if<ControlTextSnapshot>();
  if (text == nullptr || snapshot.state_seq == 0 || snapshot.state_seq > CONTROL_MAX_SEQUENCE ||
      snapshot.ttl_ms != CONTROL_LEASE_MS || !number_plain_utf8(snapshot.title.view()) || text->state_generation == 0 ||
      text->state_generation > CONTROL_MAX_SEQUENCE || snapshot.on || snapshot.brightness_known ||
      snapshot.caps.brightness || snapshot.caps.turn_on || snapshot.caps.turn_off || text->source > 3 ||
      text->reason > 7)
    return false;
  if (text->bounds_known ? text->maximum == 0 || text->minimum > text->maximum
                         : text->minimum != 0 || text->maximum != 0)
    return false;
  if (text->source != 3)
    return text->reason == 0 && text->bounds_known;
  if (text->reason == 0)
    return false;
  if (text->reason <= 3)
    return !text->can_set;  // Privacy/mode/pattern can mask invalid bounds.
  if (text->reason == 4)
    return !text->bounds_known && !text->can_set;
  return text->bounds_known;
}

bool control_valid_text_snapshot(const ControlSnapshot &snapshot, ControlTextValueView value,
                                 const uint32_t *repertoire, size_t count) {
  if (!control_valid_text_metadata(snapshot))
    return false;
  const auto &text = *snapshot.family_if<ControlTextSnapshot>();
  if (!text.available())
    return !value.present && value.value.empty();
  return value.present && control_text_validate(value.value, text.minimum, text.maximum, repertoire, count) ==
                              ControlTextDraftReason::CONTROL_TEXT_DRAFT_REASON_NONE;
}

}  // namespace esphome::lvgl_material
#endif

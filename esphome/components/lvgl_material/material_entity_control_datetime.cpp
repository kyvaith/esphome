#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS

namespace esphome::lvgl_material {

uint8_t control_datetime_mask(ControlDatetimeMode mode) {
  switch (mode) {
    case ControlDatetimeMode::CONTROL_DATETIME_MODE_DATE:
      return 7;
    case ControlDatetimeMode::CONTROL_DATETIME_MODE_TIME:
      return 56;
    case ControlDatetimeMode::CONTROL_DATETIME_MODE_DATETIME:
      return 63;
    default:
      return 0;
  }
}

bool control_datetime_field_valid(uint8_t field, uint16_t value) {
  constexpr uint16_t LOW[] = {1, 1, 1, 0, 0, 0};
  constexpr uint16_t HIGH[] = {9999, 12, 31, 23, 59, 59};
  return field < 6 && value >= LOW[field] && value <= HIGH[field];
}

bool control_datetime_tuple_valid(const ControlDatetimeTuple &value, ControlDatetimeMode mode) {
  const uint8_t mask = control_datetime_mask(mode);
  if (mask == 0 || value.known_mask != mask)
    return false;
  for (uint8_t field = 0; field < 6; field++) {
    if ((mask & (1U << field)) != 0) {
      if (!control_datetime_field_valid(field, value.values[field]))
        return false;
    } else if (value.values[field] != 0) {
      return false;
    }
  }
  if ((mask & 7U) != 0) {
    constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const uint16_t year = value.values[0];
    const uint8_t month = value.values[1];
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (value.values[2] > DAYS[month - 1] + (month == 2 && leap))
      return false;
  }
  return true;
}

bool control_datetime_updated_valid(std::string_view value) {
  if (value.empty())
    return true;
  if (value.size() != 25 || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' ||
      value[16] != ':' || (value[19] != '+' && value[19] != '-') || value[22] != ':')
    return false;
  constexpr uint8_t START[] = {0, 5, 8, 11, 14, 17, 20, 23};
  ControlDatetimeTuple tuple;
  tuple.known_mask = 63;
  for (uint8_t part = 0; part < 8; part++) {
    uint16_t number = 0;
    for (uint8_t digit = 0; digit < (part == 0 ? 4 : 2); digit++) {
      const char c = value[START[part] + digit];
      if (c < '0' || c > '9')
        return false;
      number = number * 10 + c - '0';
    }
    if (part < 6)
      tuple.values[part] = number;
    else if (number >= (part == 6 ? 24 : 60))
      return false;
  }
  return control_datetime_tuple_valid(tuple, ControlDatetimeMode::CONTROL_DATETIME_MODE_DATETIME);
}

bool control_valid_datetime_snapshot(const ControlSnapshot &snapshot) {
  const auto *datetime = snapshot.family_if<ControlDatetimeSnapshot>();
  if (datetime == nullptr || !control_valid_light_fields(snapshot) || datetime->source > 2 ||
      datetime->state_generation == 0 || datetime->state_generation > CONTROL_MAX_SEQUENCE ||
      control_datetime_mask(datetime->mode) == 0 || !number_plain_utf8(datetime->zone.view()) ||
      !control_datetime_updated_valid(datetime->updated.view()) ||
      (datetime->mode != ControlDatetimeMode::CONTROL_DATETIME_MODE_DATETIME && !datetime->zone.view().empty()))
    return false;
  if (!datetime->available())
    return datetime->value == ControlDatetimeTuple{};
  return control_datetime_tuple_valid(datetime->value, datetime->mode);
}

}  // namespace esphome::lvgl_material
#endif

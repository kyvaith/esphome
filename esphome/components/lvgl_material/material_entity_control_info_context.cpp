#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT

namespace esphome::lvgl_material {
namespace {
bool digit(char value) { return value >= '0' && value <= '9'; }

// Compare the complete decimal representation, including tiny exponents, with
// an integer bound. No float conversion, rounding, underflow or locale state.
bool angle(std::string_view text, bool azimuth) {
  if (text.empty() || text.size() > 24)
    return false;
  size_t at = 0;
  const bool negative = !text.empty() && text[0] == '-';
  if (!text.empty() && (text[0] == '-' || text[0] == '+'))
    at++;
  char digits[24];
  size_t count = 0;
  while (at < text.size() && digit(text[at]))
    digits[count++] = text[at++];
  const int integer_digits = count;
  if (at < text.size() && text[at] == '.') {
    at++;
    while (at < text.size() && digit(text[at]))
      digits[count++] = text[at++];
  }
  if (count == 0)
    return false;
  int exponent = 0;
  if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
    at++;
    const bool minus = at < text.size() && text[at] == '-';
    if (at < text.size() && (text[at] == '-' || text[at] == '+'))
      at++;
    const size_t before = at;
    while (at < text.size() && digit(text[at])) {
      if (exponent < 1000)
        exponent = exponent * 10 + text[at] - '0';
      at++;
    }
    if (at == before)
      return false;
    if (minus)
      exponent = -exponent;
  }
  if (at != text.size())
    return false;
  size_t first = 0;
  while (first < count && digits[first] == '0')
    first++;
  if (first == count)
    return true;
  if (azimuth && negative)
    return false;
  const std::string_view bound = azimuth ? "360" : "90";
  const int places = integer_digits - static_cast<int>(first) + exponent;
  if (places != static_cast<int>(bound.size()))
    return places < static_cast<int>(bound.size());
  for (size_t i = 0; i < count - first || i < bound.size(); i++) {
    const char actual = i < count - first ? digits[first + i] : '0';
    const char limit = i < bound.size() ? bound[i] : '0';
    if (actual != limit)
      return actual < limit;
  }
  return true;
}

bool value_status(uint8_t status, std::string_view text, uint8_t maximum) {
  return status <= maximum && number_plain_utf8(text) && (status == 0 ? !text.empty() : text.empty());
}
bool date_status(uint8_t status, std::string_view text, bool next, std::string_view zone) {
  return value_status(status, text, next ? 4 : 3) &&
         (status != 0 || (!zone.empty() && control_info_context_datetime(text)));
}
}  // namespace

bool control_info_context_datetime(std::string_view value) {
  if (value.size() != 25 || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' ||
      value[16] != ':' || (value[19] != '+' && value[19] != '-') || value[22] != ':')
    return false;
  for (size_t i = 0; i < value.size(); i++)
    if (i != 4 && i != 7 && i != 10 && i != 13 && i != 16 && i != 19 && i != 22 && !digit(value[i]))
      return false;
  const auto pair = [&](size_t at) { return (value[at] - '0') * 10 + value[at + 1] - '0'; };
  const unsigned year = pair(0) * 100 + pair(2), month = pair(5), day = pair(8);
  if (year == 0 || month == 0 || month > 12 || day == 0 || pair(11) > 23 || pair(14) > 59 || pair(17) > 59 ||
      pair(20) > 23 || pair(23) > 59)
    return false;
  static constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  return day <= DAYS[month - 1] + (month == 2 && leap ? 1U : 0U);
}

bool control_valid_info_context(const ControlExtraSnapshot &extra) {
  if (!extra.updated.view().empty() && !control_info_context_datetime(extra.updated.view()))
    return false;
  if (extra.type == 5) {
    const auto *person = std::get_if<ControlExtraSnapshot::Person>(&extra.data);
    if (person == nullptr || person->phase > 3 || !number_plain_utf8(person->zone.view()) ||
        !value_status(person->status, person->text.view(), 4) ||
        !date_status(person->changed_status, person->changed.view(), false, person->zone.view()))
      return false;
    if (!extra.available())
      return person->phase == 3 && person->status == 4 && person->changed_status == 1 && person->zone.view().empty();
    return person->status != 4 && (person->phase != 3 || person->status == 1 || person->status == 2);
  }
  if (extra.type == 6) {
    const auto *sun = std::get_if<ControlExtraSnapshot::Sun>(&extra.data);
    if (sun == nullptr || sun->phase > 2 || !number_plain_utf8(sun->zone.view()) ||
        !date_status(sun->rising_status, sun->rising.view(), true, sun->zone.view()) ||
        !date_status(sun->setting_status, sun->setting.view(), true, sun->zone.view()) ||
        !value_status(sun->elevation_status, sun->elevation.view(), 3) ||
        !value_status(sun->azimuth_status, sun->azimuth.view(), 3) ||
        (sun->elevation_status == 0 && !angle(sun->elevation.view(), false)) ||
        (sun->azimuth_status == 0 && !angle(sun->azimuth.view(), true)))
      return false;
    return extra.available() || (sun->phase == 2 && sun->rising_status == 1 && sun->setting_status == 1 &&
                                 sun->elevation_status == 1 && sun->azimuth_status == 1 && sun->zone.view().empty());
  }
  return false;
}
}  // namespace esphome::lvgl_material
#endif

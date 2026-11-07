#pragma once

#include <cstddef>
#include <cstdio>
#include <string_view>

namespace esphome::lvgl_material::action_timestamp {

// Display-only views into the authoritative ISO value; no time conversion or rounding.
struct Parts {
  unsigned day;
  unsigned month;
  std::string_view year;
  std::string_view clock;
  std::string_view offset;
};

inline bool parse(std::string_view value, Parts &parts) {
  if (value.size() < 25 || value.size() > 32 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
      value[13] != ':' || value[16] != ':')
    return false;
  const size_t offset = value.size() - 6;
  if ((value[offset] != '+' && value[offset] != '-') || value[offset + 3] != ':')
    return false;
  if (offset != 19) {
    if (offset < 21 || offset > 26 || value[19] != '.')
      return false;
    for (size_t i = 20; i < offset; i++)
      if (value[i] < '0' || value[i] > '9')
        return false;
  }
  const auto number = [&](size_t start, size_t length) -> int {
    int result = 0;
    for (size_t i = start; i < start + length; i++) {
      if (value[i] < '0' || value[i] > '9')
        return -1;
      result = result * 10 + value[i] - '0';
    }
    return result;
  };
  const int year = number(0, 4), month = number(5, 2), day = number(8, 2);
  const int hour = number(11, 2), minute = number(14, 2), second = number(17, 2);
  const int offset_hour = number(offset + 1, 2), offset_minute = number(offset + 4, 2);
  if (year < 1 || month < 1 || month > 12 || day < 1 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59 || offset_hour < 0 || offset_hour > 23 || offset_minute < 0 || offset_minute > 59)
    return false;
  constexpr unsigned DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  if (static_cast<unsigned>(day) > DAYS[month - 1] + (month == 2 && leap ? 1U : 0U))
    return false;
  parts = {static_cast<unsigned>(day), static_cast<unsigned>(month), value.substr(0, 4), value.substr(11, offset - 11),
           value.substr(offset)};
  return true;
}

inline bool format(const Parts &parts, std::string_view heading, std::string_view month, char *output,
                   size_t capacity) {
  if (capacity == 0 || output == nullptr)
    return false;
  output[0] = '\0';
  // Provider strings have the existing 160-byte limit. Reject an oversized translation without truncating it.
  if (heading.size() > 160 || month.empty() || month.size() > 160)
    return false;
  const int length =
      std::snprintf(output, capacity, "%.*s · %u %.*s %.*s\n%.*s · UTC%.*s", static_cast<int>(heading.size()),
                    heading.data(), parts.day, static_cast<int>(month.size()), month.data(),
                    static_cast<int>(parts.year.size()), parts.year.data(), static_cast<int>(parts.clock.size()),
                    parts.clock.data(), static_cast<int>(parts.offset.size()), parts.offset.data());
  if (length < 0 || static_cast<size_t>(length) >= capacity) {
    output[0] = '\0';
    return false;
  }
  return true;
}

}  // namespace esphome::lvgl_material::action_timestamp

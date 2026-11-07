#include "esphome/components/lvgl_material/material_action_timestamp.h"
#include <array>
#include <cassert>
#include <cstring>
#include <string>

using namespace esphome::lvgl_material::action_timestamp;

int main() {
  Parts parts{};
  char text[322];
  const std::string original = "2026-11-07T14:03:12.123456+01:00";
  assert(parse(original, parts));
  assert(format(parts, "Ostatnio użyto", "listopada", text, sizeof(text)));
  assert(std::strcmp(text, "Ostatnio użyto · 7 listopada 2026\n14:03:12.123456 · UTC+01:00") == 0);
  assert(original == "2026-11-07T14:03:12.123456+01:00");
  assert(format(parts, "Last used", "November", text, sizeof(text)));
  assert(std::strcmp(text, "Last used · 7 November 2026\n14:03:12.123456 · UTC+01:00") == 0);

  // A repeated local DST hour is two distinct observations; do not convert or collapse it.
  for (const auto offset : {"+02:00", "+01:00", "+05:45", "-03:30", "+23:59", "-23:59"}) {
    const std::string value = std::string("2026-10-25T02:30:59.000001") + offset;
    assert(parse(value, parts));
    assert(format(parts, "Last used", "October", text, sizeof(text)));
    assert(std::string(text) == std::string("Last used · 25 October 2026\n02:30:59.000001 · UTC") + offset);
  }
  for (const auto fraction : {"", ".0", ".000000", ".1", ".12", ".123", ".1234", ".12345", ".123456"}) {
    const std::string value = std::string("2000-02-29T00:00:00") + fraction + "+00:00";
    assert(parse(value, parts));
    assert(format(parts, "Last used", "February", text, sizeof(text)));
    assert(std::string(text) == std::string("Last used · 29 February 2000\n00:00:00") + fraction + " · UTC+00:00");
  }
  for (const auto value : {"0001-01-01T00:00:00+00:00", "9999-12-31T23:59:59.999999+00:00", "2024-02-29T12:00:00+00:00",
                           "2400-02-29T12:00:00+00:00"})
    assert(parse(value, parts));

  const auto unchanged = parts;
  for (const auto value : {"",
                           "unknown",
                           "unavailable",
                           "2026-11-07T14:03:12Z",
                           "2026-11-07T14:03:12",
                           "2026-11-07T14:03:12.+01:00",
                           "2026-11-07T14:03:12.1234567+01:00",
                           "2026-11-07T14:03:60+01:00",
                           "2026-11-07T24:03:12+01:00",
                           "2026-11-07T14:60:12+01:00",
                           "2026-11-07T14:03:12+24:00",
                           "2026-11-07T14:03:12+01:60",
                           "2026-11-07T14:03:12+01:00tail",
                           "2026-11-07 14:03:12+01:00",
                           "2026-02-29T12:00:00+00:00",
                           "1900-02-29T12:00:00+00:00",
                           "2100-02-29T12:00:00+00:00",
                           "2026-04-31T12:00:00+00:00",
                           "2026-00-01T12:00:00+00:00",
                           "2026-13-01T12:00:00+00:00",
                           "2026-01-00T12:00:00+00:00",
                           "0000-01-01T12:00:00+00:00",
                           "2026-01-32T12:00:00+00:00",
                           "202x-01-01T12:00:00+00:00",
                           "2026-01-01T12:0x:00+00:00",
                           "2026-01-01T12:00:00.12x+00:00",
                           "2026-01-01T12:00:00+0x:00"}) {
    assert(!parse(value, parts));
    assert(parts.day == unchanged.day && parts.month == unchanged.month && parts.year == unchanged.year &&
           parts.clock == unchanged.clock && parts.offset == unchanged.offset);
  }

  assert(parse(original, parts));
  assert(format(parts, "Last used", "November", text, sizeof(text)));
  const auto required = std::strlen(text) + 1;
  std::array<char, 400> guarded;
  for (size_t capacity = 0; capacity <= required; capacity++) {
    guarded.fill('#');
    const bool complete = format(parts, "Last used", "November", guarded.data() + 1, capacity);
    assert(complete == (capacity == required));
    assert(guarded.front() == '#' && guarded[capacity + 1] == '#');
    if (capacity > 0 && !complete)
      assert(guarded[1] == '\0');
  }
  assert(!format(parts, "Last used", "November", nullptr, 10));
  assert(!format(parts, std::string(161, 'x'), "November", text, sizeof(text)) && text[0] == '\0');
  assert(!format(parts, "Last used", "", text, sizeof(text)) && text[0] == '\0');
  assert(!format(parts, std::string(160, 'x'), std::string(160, 'y'), text, sizeof(text)) && text[0] == '\0');
  return 0;
}

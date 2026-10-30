#include "material_number_domain.h"

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER

#include <cstring>

namespace esphome::lvgl_material {

bool number_parse_decimal(std::string_view text, int64_t &value) {
  if (text.empty() || text.size() > 21)
    return false;
  size_t offset = 0;
  const bool negative = text[0] == '-';
  if (negative && ++offset == text.size())
    return false;
  const size_t first = offset;
  int64_t whole = 0;
  while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') {
    if (offset > first && text[first] == '0')
      return false;
    whole = whole * 10 + text[offset++] - '0';
    if (whole > 1000000000000LL)
      return false;
  }
  if (offset == first)
    return false;
  int64_t fraction = 0;
  if (offset != text.size()) {
    if (text[offset++] != '.')
      return false;
    const size_t begin = offset;
    while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') {
      if (offset - begin == 6)
        return false;
      fraction = fraction * 10 + text[offset++] - '0';
    }
    if (offset != text.size() || offset == begin || text[offset - 1] == '0')
      return false;
    for (size_t count = offset - begin; count < 6; count++)
      fraction *= 10;
  }
  if (whole == 1000000000000LL && fraction != 0)
    return false;
  const int64_t magnitude = whole * NUMBER_SCALE + fraction;
  if (negative && magnitude == 0)
    return false;
  value = negative ? -magnitude : magnitude;
  return true;
}

bool number_format_decimal(int64_t value, char *output, size_t capacity) {
  if (output == nullptr || value < -NUMBER_MAX_MICRO || value > NUMBER_MAX_MICRO)
    return false;
  char text[24]{};
  size_t count = 0;
  if (value < 0) {
    text[count++] = '-';
    value = -value;
  }
  const int64_t fraction = value % NUMBER_SCALE;
  int64_t whole = value / NUMBER_SCALE;
  char reverse[13];
  size_t digits = 0;
  do {
    reverse[digits++] = static_cast<char>('0' + whole % 10);
    whole /= 10;
  } while (whole != 0);
  while (digits != 0)
    text[count++] = reverse[--digits];
  if (fraction != 0) {
    text[count++] = '.';
    int64_t remaining = fraction;
    for (int64_t divisor = 100000; divisor != 0; divisor /= 10) {
      text[count++] = static_cast<char>('0' + remaining / divisor);
      remaining %= divisor;
    }
    while (text[count - 1] == '0')
      count--;
  }
  if (capacity <= count)
    return false;
  std::memcpy(output, text, count);
  output[count] = 0;
  return true;
}

// Printable UTF-8 transport validation; glyph coverage belongs to product fonts.
// In addition to C0/C1, reject format and separator codepoints that cannot be
// used as a short, single-line unit. Do not allocate a Unicode database.
bool number_plain_utf8(std::string_view value) {
  for (size_t offset = 0; offset < value.size();) {
    const auto first = static_cast<uint8_t>(value[offset++]);
    uint32_t point = first;
    size_t tail = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      point &= 0x1f;
      tail = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      point &= 0x0f;
      tail = 2;
    } else if (first >= 0xf0 && first <= 0xf4) {
      point &= 7;
      tail = 3;
    } else if (first >= 0x80)
      return false;
    if (offset + tail > value.size())
      return false;
    for (size_t i = 0; i < tail; i++) {
      const auto byte = static_cast<uint8_t>(value[offset++]);
      if ((byte & 0xc0) != 0x80)
        return false;
      point = (point << 6) | (byte & 0x3f);
    }
    if ((tail == 2 && point < 0x800) || (tail == 3 && point < 0x10000) || point > 0x10ffff ||
        (point >= 0xd800 && point <= 0xdfff) || point < 0x20 || (point >= 0x7f && point <= 0x9f) || point == 0xa0 ||
        point == 0xad || point == 0x61c || point == 0x1680 || point == 0x180e || (point >= 0x2000 && point <= 0x200f) ||
        point == 0x202f || point == 0x205f || point == 0x3000 || (point >= 0x2028 && point <= 0x202e) ||
        (point >= 0x2060 && point <= 0x206f) || point == 0xfeff || (point >= 0xfff9 && point <= 0xfffb) ||
        (point >= 0xe0000 && point <= 0xe007f))
      return false;
  }
  return true;
}

bool MaterialNumberDomain::valid() const {
  const auto end = std::char_traits<char>::find(this->unit.data(), this->unit.size(), 0);
  if (end == nullptr || !number_plain_utf8({this->unit.data(), static_cast<size_t>(end - this->unit.data())}) ||
      this->minimum < -NUMBER_MAX_MICRO || this->maximum > NUMBER_MAX_MICRO || this->minimum > this->maximum ||
      this->step <= 0 || this->step > NUMBER_MAX_MICRO)
    return false;
  const auto intervals = static_cast<uint64_t>((this->maximum - this->minimum) / this->step);
  return intervals <= NUMBER_MAX_INDEX && this->max_index == intervals;
}

bool MaterialNumberDomain::value_at(uint32_t index, int64_t &value) const {
  if (!this->valid() || index > this->max_index)
    return false;
  // index <= floor(span/step), so multiplication cannot exceed the 2e18 span.
  value = this->minimum + static_cast<int64_t>(index) * this->step;
  return true;
}

}  // namespace esphome::lvgl_material

#endif

#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS

namespace esphome::lvgl_material {
namespace {
struct Decimal {
  uint64_t coefficient{0};
  int16_t exponent{0};
};

// Bounded exact decimal arithmetic keeps subnormal source observations intact.
// This validates syntax and cross-field consistency, not Python's binary64
// shortest-representation algorithm; the trusted provider owns that spelling.
bool decimal(std::string_view text, Decimal &out) {
  if (text.empty() || text.size() > 26 || text.front() < '0' || text.front() > '9')
    return false;
  size_t position = 0, digits = 0, fractional = 0;
  bool dot = false;
  while (position < text.size() && text[position] != 'e') {
    const char value = text[position++];
    if (value == '.') {
      if (dot || position == 1 || position == text.size())
        return false;
      dot = true;
      continue;
    }
    if (value < '0' || value > '9' || (position > 1 && text.front() == '0' && !dot))
      return false;
    fractional += dot;
    if (out.coefficient != 0 || value != '0')
      digits++;
    if (digits > 17)
      return false;
    out.coefficient = out.coefficient * 10 + static_cast<uint8_t>(value - '0');
  }
  if (dot && fractional == 0)
    return false;
  int exponent = 0;
  if (position < text.size()) {
    position++;
    bool negative = false;
    if (position < text.size() && (text[position] == '-' || text[position] == '+'))
      negative = text[position++] == '-';
    const size_t start = position;
    for (; position < text.size(); position++) {
      if (text[position] < '0' || text[position] > '9' || position - start >= 3)
        return false;
      exponent = exponent * 10 + text[position] - '0';
    }
    if (position == start || exponent > 350)
      return false;
    if (negative)
      exponent = -exponent;
  }
  out.exponent = exponent - static_cast<int>(fractional);
  if (out.coefficient == 0) {
    out.exponent = 0;
    return text == "0";
  }
  while (out.coefficient % 10 == 0) {
    out.coefficient /= 10;
    out.exponent++;
  }
  return true;
}

size_t digits(uint64_t value) {
  size_t result = 0;
  do {
    result++;
    value /= 10;
  } while (value);
  return result;
}

bool unit_value(const Decimal &value) {
  if (value.coefficient == 0)
    return true;
  const int order = static_cast<int>(digits(value.coefficient)) + value.exponent;
  return order < 1 || (value.coefficient == 1 && value.exponent == 0);
}

uint16_t geometry(const Decimal &value) {
  int scale = value.exponent + 4;
  uint64_t value_integer = value.coefficient;
  if (scale >= 0) {
    while (scale-- > 0)
      value_integer *= 10;
    return value_integer;
  }
  if (scale < -18)
    return 0;
  uint64_t divisor = 1;
  while (scale++ < 0)
    divisor *= 10;
  const auto quotient = value_integer / divisor, remainder = value_integer % divisor;
  // Decimal's nearest-even display geometry; values remain exact strings.
  return quotient + (remainder > divisor / 2 || (remainder == divisor / 2 && (quotient & 1U)));
}
}  // namespace

bool control_media_volume_valid(const ControlMediaSnapshot::Volume &volume) {
  if (!volume.known)
    return volume.raw.view().empty() && volume.percent.view().empty() && volume.rail == 0;
  Decimal source, percent;
  return decimal(volume.raw.view(), source) && decimal(volume.percent.view(), percent) && unit_value(source) &&
         source.coefficient == percent.coefficient &&
         (source.coefficient == 0 ? percent.exponent == 0 : source.exponent + 2 == percent.exponent) &&
         geometry(source) == volume.rail;
}

bool control_media_raw_valid(std::string_view raw) {
  Decimal value;
  return raw.size() <= 24 && decimal(raw, value) && unit_value(value);
}

uint16_t control_media_main_eligible(const ControlMediaSnapshot &media) {
  const auto *main = std::get_if<ControlMediaSnapshot::Main>(&media.data);
  if (main == nullptr || !media.available())
    return 0;
  const bool active = main->phase <= 5 && main->phase != 1;
  uint16_t result = 0;
  for (uint8_t index = 0; index < 12; index++) {
    const uint16_t bit = 1U << index;
    if ((media.operations & bit) == 0)
      continue;
    bool eligible = active;
    switch (index) {
      case 0:  // Explicit Play.
        eligible = main->phase == 0 || main->phase == 2 || main->phase == 4;
        break;
      case 1:
        eligible = main->phase == 3 || main->phase == 5;
        break;
      case 4:
        eligible = main->phase == 3 || main->phase == 4 || main->phase == 5;
        break;
      case 5:
        eligible = main->phase == 1 || main->phase == 6;
        break;
      case 6:
        eligible = active;
        break;
      case 7:
        eligible = active && main->volume.known;
        break;
      case 8:
        eligible = active && main->volume.known && main->volume.raw.view() != "1";
        break;
      case 9:
        eligible = active && main->volume.known && main->volume.raw.view() != "0";
        break;
      case 10:
        eligible = active && main->muted != ControlMediaSnapshot::UNKNOWN_BOOL;
        break;
      case 11:
        eligible = active && main->shuffle != ControlMediaSnapshot::UNKNOWN_BOOL;
        break;
      default:
        break;
    }
    if (eligible)
      result |= bit;
  }
  return result;
}

bool control_valid_media_snapshot(const ControlSnapshot &snapshot) {
  using Media = ControlMediaSnapshot;
  const auto *media = snapshot.family_if<Media>();
  if (media == nullptr || !control_valid_light_fields(snapshot) || media->state_generation == 0 ||
      media->state_generation > CONTROL_MAX_SEQUENCE || media->view_seq > CONTROL_MAX_SEQUENCE ||
      media->device_type > 3 || media->source > 2 || media->operations > Media::ALL_OPERATIONS || media->facets > 15 ||
      ((media->facets & 1U) != 0) != ((media->operations & 128U) != 0))
    return false;
  const auto facet = static_cast<uint8_t>(media->facet);
  if (facet > 4 || (facet != 0 && (media->view_seq == 0 || (media->facets & (1U << (facet - 1))) == 0)))
    return false;
  if (facet == 0) {
    const auto *main = std::get_if<Media::Main>(&media->data);
    if (main == nullptr || main->phase > 7 || media->available() != (main->phase != 7) ||
        !control_media_volume_valid(main->volume) || media->eligible != control_media_main_eligible(*media))
      return false;
    const bool active = main->phase <= 5 && main->phase != 1;
    if (main->volume.known && (!active || (media->operations & (128U | 256U | 512U)) == 0))
      return false;
    for (auto pair : {std::pair{main->muted, 1024U}, std::pair{main->shuffle, 2048U}})
      if (pair.first != Media::UNKNOWN_BOOL && (pair.first > 1 || !active || (media->operations & pair.second) == 0))
        return false;
    const auto description = [active](std::string_view text, uint8_t status) {
      if (status > 3 || !number_plain_utf8(text) || ((status == 1 || status == 2) != text.empty()) ||
          (!active && !text.empty()))
        return false;
      return status != 3 || (text.size() >= 3 && text.substr(text.size() - 3) == "\xE2\x80\xA6");
    };
    return description(main->title.view(), main->title_status) && description(main->artist.view(), main->artist_status);
  }
  if (facet == 1) {
    const auto *volume = std::get_if<Media::Volume>(&media->data);
    return volume != nullptr && control_media_volume_valid(*volume) && media->editable == volume->known &&
           (!volume->known || media->available()) && media->eligible == (media->editable ? 1 : 0);
  }
  const auto *choice = std::get_if<Media::Choice>(&media->data);
  if (choice == nullptr || choice->count == 0 || choice->count > choice->options.size() ||
      (choice->index != 255 && choice->index >= choice->count) || media->editable != (choice->index != 255) ||
      (!media->available() && choice->index != 255) || media->eligible != (media->editable ? 2 : 0))
    return false;
  for (size_t index = 0; index < choice->count; index++) {
    const auto label = choice->options[index].view();
    if (label.empty() || label == "unknown" || label == "unavailable" || !number_plain_utf8(label))
      return false;
    for (size_t before = 0; before < index; before++)
      if (label == choice->options[before].view())
        return false;
  }
  return true;
}
}  // namespace esphome::lvgl_material

#endif

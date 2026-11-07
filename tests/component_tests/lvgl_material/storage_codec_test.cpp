#include "material_entity_control.h"
#include "material_entity_control_codec.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <string>
#include <type_traits>

using namespace esphome::lvgl_material;
using Kind = ControlEntityKind;

static_assert(std::is_trivially_copyable_v<ControlSnapshot>);
static_assert(std::is_nothrow_copy_assignable_v<ControlSnapshot>);

ControlMessage make_state(unsigned family) {
  ControlMessage message;
  message.generic = true;
  message.kind = ControlKind::CONTROL_KIND_STATE;
  message.identity.channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  message.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  message.identity.open_seq = 1;
  auto &context = message.identity.context;
  context.card_id.assign("storage");
  context.slot = 2;
  context.revision = 1;
  context.control_kind = Kind::CONTROL_ENTITY_LIGHT;
  auto &state = message.state;
  state.title.assign("Typed storage");
  state.state_seq = 1;
  switch (family) {
    case 0:
      state.caps.turn_on = state.caps.turn_off = true;
      break;
    case 1: {
      context.control_kind = Kind::CONTROL_ENTITY_NUMBER;
      auto &number = state.emplace_family<ControlNumberSnapshot>();
      number.state_generation = 1;
      number.available = true;
      number.domain.maximum = 10 * NUMBER_SCALE;
      number.domain.step = NUMBER_SCALE;
      number.domain.max_index = 10;
      number.index = 1;
      number.value_text.assign("1");
      break;
    }
    case 2: {
      context.control_kind = Kind::CONTROL_ENTITY_CHOICE;
      auto &choice = state.emplace_family<ControlSnapshot::Choice>();
      choice.state_generation = 1;
      choice.available = true;
      choice.count = 1;
      choice.options[0].assign("Eco");
      break;
    }
    case 3: {
      context.control_kind = Kind::CONTROL_ENTITY_COVER;
      auto &cover = state.emplace_family<ControlCoverSnapshot>();
      cover.state_generation = 1;
      cover.available = true;
      cover.state = ControlCoverState::CONTROL_COVER_STATE_CLOSED;
      cover.caps.open = cover.caps.close = cover.caps.stop = true;
      break;
    }
    case 4: {
      context.control_kind = Kind::CONTROL_ENTITY_FAN;
      auto &fan = state.emplace_family<ControlSnapshot::Fan>();
      fan.state_generation = 1;
      fan.available = true;
      fan.power = 0;
      fan.caps.turn_on = true;
      break;
    }
    case 5: {
      context.control_kind = Kind::CONTROL_ENTITY_HUMIDIFIER;
      auto &humidity = state.emplace_family<ControlSnapshot::Humidifier>();
      humidity.state_generation = 1;
      humidity.available = true;
      humidity.power = 0;
      humidity.caps.turn_on = true;
      humidity.target_text.assign("--");
      break;
    }
    case 6: {
      context.control_kind = Kind::CONTROL_ENTITY_ROBOT;
      auto &robot = state.emplace_family<ControlSnapshot::Robot>();
      robot.state_generation = 1;
      robot.available = true;
      robot.activity = 2;
      robot.caps.actions = robot.eligible_actions = 1;
      break;
    }
    case 7: {
      context.control_kind = Kind::CONTROL_ENTITY_PANEL;
      auto &panel = state.emplace_family<ControlSnapshot::Panel>();
      panel.state_generation = 1;
      panel.available = true;
      panel.type = 2;
      panel.power = 1;
      panel.caps.turn_on = panel.caps.turn_off = true;
      panel.eligible_actions = 1;
      break;
    }
    default: {
      auto &detail = state.emplace_family<ControlDetailSnapshot>();
      detail.present = true;
      detail.state_generation = 1;
      if (family == 8) {
        state.caps.turn_on = state.caps.turn_off = true;
        detail.available = true;
      } else {
        context.control_kind = Kind::CONTROL_ENTITY_PANEL;
        detail.climate = true;
        detail.data.emplace<ControlClimateMain>().number.value.assign("--");
      }
      break;
    }
  }
  return message;
}

int main() {
  MaterialEntityControlCodec codec;
  std::array<std::string, 10> wires;
  for (unsigned family = 0; family < wires.size(); family++) {
    auto message = make_state(family);
    char buffer[2049];
    size_t length = 0;
    if (!codec.serialize_entity(message, buffer, sizeof(buffer), length)) {
      std::printf("serialize family %u failed\n", family);
      return 1;
    }
    wires[family].assign(buffer, length);
    assert(length <= 2048 && codec.high_water() <= 5120);
    if (family != 0)
      assert(!control_valid_snapshot(message.state));
  }
  unsigned transitions = 0;
  unsigned rejected = 0;
  for (unsigned from = 0; from < wires.size(); from++) {
    for (unsigned to = 0; to < wires.size(); to++) {
      ControlMessage output;
      assert(codec.parse_entity(wires[from], output));
      const auto previous = output;
      // Invalid input must preserve the whole previous alternative, including
      // arrays and the inner Detail facet. No partially decoded tag is exposed.
      auto invalid = wires[to];
      invalid.insert(invalid.size() - 1, ",\"unexpected\":0");
      assert(!codec.parse_entity(invalid, output));
      assert(output.state == previous.state && output.identity == previous.identity);
      invalid = wires[to];
      invalid.insert(1, "\"state_seq\":99,");
      assert(!codec.parse_entity(invalid, output));
      assert(output.state == previous.state && output.identity == previous.identity);
      assert(codec.parse_entity(wires[to], output));
      if (!(output.state == make_state(to).state)) {
        std::printf("state roundtrip mismatch %u -> %u\n", from, to);
        return 2;
      }
      assert(output.state.matches_family(output.identity.context.control_kind));
      transitions++;

      auto wrong = make_state(to);
      wrong.state = make_state(from).state;
      if (!wrong.state.matches_family(wrong.identity.context.control_kind)) {
        char buffer[2049] = "unchanged";
        size_t length = 77;
        assert(!codec.serialize_entity(wrong, buffer, sizeof(buffer), length));
        assert(std::string(buffer) == "unchanged");
        rejected++;
      }
    }
  }
  // Explicit Detail payloads remain incompatible with the legacy Light codec,
  // whose unchanged common Light fields are validated separately by Detail.
  auto detail = make_state(8);
  char buffer[2049];
  size_t length = 0;
  assert(control_valid_light_fields(detail.state));
  assert(!codec.serialize(detail, buffer, sizeof(buffer), length));
  std::printf("STORAGE CODEC transitions=%u wrong_tags=%u atomic=pass arena=%zu\n", transitions, rejected,
              codec.high_water());
}

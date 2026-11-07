#include "material_entity_control.h"
#include "material_entity_control_codec.h"
#include "range_fixtures.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
using namespace esphome::lvgl_material;
struct Sink : MaterialEntityControlSink {
  std::vector<ControlMessage> messages;
  std::function<void()> callback;
  bool accepted = true;
  bool emit(const ControlMessage &message) override {
    messages.push_back(message);
    if (callback)
      callback();
    return accepted;
  }
  void view_event(ControlViewEvent) override {}
  unsigned commands() const {
    unsigned n = 0;
    for (auto &m : messages)
      n += m.kind == ControlKind::CONTROL_KIND_COMMAND;
    return n;
  }
};
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
struct Fixture {
  Sink sink;
  MaterialEntityControl owner{sink};
  MaterialEntityControlCodec codec;
  ControlMessage message;
  ControlText<32> channel;
  uint32_t now = 1;
  Fixture() {
    channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    assert(owner.set_enabled(true) && owner.bind_generic(channel));
    assert(owner.set_detail_profiles(2));
    assert(owner.set_detail_range_enabled(true));
    assert(!owner.accept_detail_range(channel));
    assert(owner.accept_detail_features(channel, 2) && owner.accept_detail_range(channel));
    assert(codec.parse_entity(RANGE_FIXTURES[0], message));
    assert(owner.request_open(message.identity.context, now++));
    message.identity = sink.messages.back().identity;
    message.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    assert(owner.receive(message, now++) && owner.set_view_ready(true, now++));
    assert(owner.request_facet(ControlFacet::CONTROL_FACET_RANGE, now++));
    const auto identity = message.identity;
    assert(codec.parse_entity(RANGE_FIXTURES[1], message));
    message.identity = identity;
    assert(owner.receive(message, now++));
    auto receipt = sink.messages.back();
    receipt.kind = ControlKind::CONTROL_KIND_FACET_RESULT;
    receipt.state.state_seq = message.state.state_seq;
    receipt.facet_ready = true;
    assert(owner.receive(receipt, now++) && owner.set_view_ready(true, now++));
    assert(owner.is_detail_range() && owner.can_detail_range());
  }
  void update(bool generation = true) {
    message.state.state_seq++;
    if (generation)
      message.state.family_if<ControlDetailSnapshot>()->state_generation++;
    assert(owner.receive(message, now++));
  }
};
#endif
int main() {
  MaterialEntityControlCodec codec;
  ControlMessage parsed;
#ifndef USE_LVGL_MATERIAL_CLIMATE_RANGE
  for (auto *row : RANGE_FIXTURES)
    assert(!codec.parse_entity(row, parsed));
  for (auto *row : RANGE_HA_FIXTURES) {
    const std::string wire(row);
    if (wire.find("\"facet\":\"range\"") != std::string::npos)
      assert(!codec.parse_entity(row, parsed));
  }
  std::cout << "Range default off PASS\n";
#else
  ControlMessage decoded;
  char output[2049];
  size_t length = 0;
  static_assert(static_cast<int>(ControlFacet::CONTROL_FACET_RANGE) == 10);
  for (auto *row : RANGE_FIXTURES) {
    assert(codec.parse_entity(row, parsed));
    assert(codec.serialize_entity(parsed, output, sizeof(output), length));
    assert(codec.parse_entity({output, length}, decoded));
    assert(parsed.state == decoded.state);
    auto duplicate = std::string(row);
    duplicate.insert(1, "\"v\":1,");
    assert(!codec.parse_entity(duplicate, decoded));
  }
  for (auto *row : RANGE_BAD_FIXTURES)
    assert(!codec.parse_entity(row, parsed));
  size_t index = 0;
  for (auto *row : RANGE_HA_FIXTURES) {
    if (!codec.parse_entity(row, parsed)) {
      std::cerr << "HA row " << index << " rejected\n";
      return 2;
    }
    ++index;
    assert(codec.serialize_entity(parsed, output, sizeof(output), length));
    assert(codec.parse_entity({output, length}, decoded));
    assert(parsed.state == decoded.state && parsed.operation == decoded.operation);
    std::cout << "HA_WIRE " << std::string(output, length) << "\n";
  }
  ControlText<32> channel;
  const std::string bind =
      R"({"v":1,"type":"entity_detail_range_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","range_version":1})";
  assert(codec.parse_detail_range_bind(bind, channel));
  auto duplicate = bind;
  duplicate.insert(1, "\"range_version\":1,");
  assert(!codec.parse_detail_range_bind(duplicate, channel));
  {
    Fixture f;
    assert(f.owner.begin_detail_range(1, f.now++));
    assert(!f.owner.release_detail_range(1, 28, 34, f.now++));
    assert(f.sink.commands() == 0);
  }
  {
    Fixture f;
    assert(f.owner.begin_detail_range(1, f.now++));
    assert(!f.owner.release_detail_range(1, 35, 34, f.now++));
    assert(f.sink.commands() == 0);
  }
  {
    Fixture f;
    assert(f.owner.begin_detail_range(1, f.now++));
    assert(f.owner.release_detail_range(1, 34, 34, f.now++));
    assert(f.sink.commands() == 1 && f.owner.is_command_pending());
    assert(!f.owner.release_detail_range(1, 34, 34, f.now++));
    auto command = f.sink.messages.back();
    assert(codec.serialize_entity(command, output, sizeof(output), length));
    assert(codec.parse_entity({output, length}, parsed));
    assert(parsed.state.family_if<ControlDetailSnapshot>()->range()->low_index == 34);
    f.message.state.family_if<ControlDetailSnapshot>()->data.emplace<ControlFacetRange>() =
        *f.owner.state().family_if<ControlDetailSnapshot>()->range();
    auto &range = std::get<ControlFacetRange>(f.message.state.family_if<ControlDetailSnapshot>()->data);
    range.low_index = 34;
    f.update();
    command.kind = ControlKind::CONTROL_KIND_RESULT;
    command.state.state_seq = f.message.state.state_seq;
    command.result = ControlResult::CONTROL_RESULT_STATE_MATCHES;
    command.reason = ControlReason::CONTROL_REASON_NONE;
    assert(f.owner.receive(command, f.now++));
    assert(!f.owner.is_command_pending());
    assert(!f.owner.receive(command, f.now++));
  }
  {
    Fixture f;
    assert(f.owner.begin_detail_range(1, f.now++));
    f.owner.cancel_drag();
    assert(!f.owner.release_detail_range(1, 0, 34, f.now++));
    assert(f.sink.commands() == 0);
  }
  {
    Fixture f;
    assert(f.owner.begin_detail_range(1, f.now++));
    f.update();
    assert(!f.owner.release_detail_range(1, 0, 34, f.now++));
    assert(f.sink.commands() == 0);
  }
  {
    Fixture f;
    assert(f.owner.begin_detail_range(1, f.now++));
    f.message.state.title.assign("Metadata");
    auto &range = std::get<ControlFacetRange>(f.message.state.family_if<ControlDetailSnapshot>()->data);
    range.current.assign("22.5");
    range.activity = 3;
    f.update(false);
    assert(f.owner.is_dragging());
    assert(f.owner.release_detail_range(1, 0, 34, f.now++));
    assert(f.sink.commands() == 1);
  }
  {
    Fixture f;
    assert(f.owner.begin_detail_range(1, f.now++));
    f.message.state.state_seq++;
    std::get<ControlFacetRange>(f.message.state.family_if<ControlDetailSnapshot>()->data).low_index++;
    assert(!f.owner.receive(f.message, f.now++));
    assert(!f.owner.has_selection());
    assert(!f.owner.release_detail_range(1, 0, 34, f.now++));
  }
  {
    Fixture f;
    assert(f.owner.begin_detail_range(1, f.now++));
    f.owner.disconnect();
    assert(!f.owner.detail_range_bound());
    assert(!f.owner.release_detail_range(1, 0, 34, f.now++));
  }
  {
    Fixture f;
    assert(f.owner.begin_detail_range(1, f.now++));
    f.sink.callback = [&] {
      assert(!f.owner.begin_detail_range(9, f.now++));
      assert(!f.owner.release_detail_range(1, 0, 34, f.now++));
    };
    assert(f.owner.release_detail_range(1, 0, 34, f.now++));
    assert(f.sink.commands() == 1);
  }
  assert(codec.high_water() <= 5120);
  std::cout << "Range owner/codec PASS arena=" << codec.high_water() << " snapshot=" << sizeof(ControlSnapshot)
            << " message=" << sizeof(ControlMessage) << "\n";
#endif
}

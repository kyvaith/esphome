#include "material_entity_control.h"
#include "material_entity_control_codec.h"
#include "color_fixtures.h"
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
    unsigned count = 0;
    for (const auto &message : messages)
      count += message.kind == ControlKind::CONTROL_KIND_COMMAND;
    return count;
  }
};

#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
struct Fixture {
  Sink sink;
  MaterialEntityControl owner{sink};
  MaterialEntityControlCodec codec;
  ControlMessage message;
  ControlText<32> channel;
  uint32_t now = 1;
  Fixture(uint8_t mode = 0) {
    assert(channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    assert(owner.set_enabled(true));
    assert(owner.bind_generic(channel));
    assert(owner.set_detail_profiles(1));
    assert(owner.set_detail_color_enabled(true));
    assert(!owner.accept_detail_color(channel));
    assert(owner.accept_detail_features(channel, 1));
    assert(owner.accept_detail_color(channel));
    assert(codec.parse_entity(COLOR_FIXTURES[0], message));
    assert(owner.request_open_detail(message.identity.context, now++));
    message.identity = sink.messages.back().identity;
    message.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    message.kind = ControlKind::CONTROL_KIND_GRANT;
    assert(owner.receive(message, now++));
    assert(owner.set_view_ready(true, now++));
    assert(owner.request_facet(ControlFacet::CONTROL_FACET_COLOR, now++));
    const auto identity = message.identity;
    assert(codec.parse_entity(COLOR_FIXTURES[1], message));
    message.identity = identity;
    message.state.family_if<ControlDetailSnapshot>()->data.emplace<ControlFacetColor>() =
        ControlFacetColor{125500, 70250, {76, 255, 92}, 0, mode, 0, true};
    assert(owner.receive(message, now++));
    auto receipt = sink.messages.back();
    receipt.kind = ControlKind::CONTROL_KIND_FACET_RESULT;
    receipt.state.state_seq = message.state.state_seq;
    receipt.facet_ready = true;
    assert(owner.receive(receipt, now++));
    assert(owner.set_view_ready(true, now++));
    assert(owner.is_detail_color() && owner.can_detail_color());
  }
  void update(bool generation = true) {
    message.state.state_seq++;
    if (generation)
      message.state.family_if<ControlDetailSnapshot>()->state_generation++;
    assert(owner.receive(message, now++));
  }
  void draft(uint32_t hue = 220123, uint32_t saturation = 51001) {
    assert(owner.begin_detail_color(7, now++));
    assert(owner.update_detail_color(7, hue, saturation));
    assert(owner.release_detail_color(7, now++));
  }
};
#endif

int main() {
  MaterialEntityControlCodec codec;
  ControlMessage parsed;
#ifndef USE_LVGL_MATERIAL_LIGHT_COLOR
  for (const auto *fixture : COLOR_FIXTURES)
    assert(!codec.parse_entity(fixture, parsed));
  std::cout << "Color default-off rejects extended states/commands\n";
  return 0;
#else
  ControlMessage decoded;
  char output[2049];
  size_t length = 0;
  size_t fixture_index = 0;
  for (const auto *fixture : COLOR_FIXTURES) {
    if (!codec.parse_entity(fixture, parsed)) {
      std::cerr << "Decode fixture " << fixture_index << " failed\n";
      return 2;
    }
    fixture_index++;
    assert(codec.serialize_entity(parsed, output, sizeof(output), length));
    assert(codec.parse_entity({output, length}, decoded));
    assert(parsed.kind == decoded.kind && parsed.state == decoded.state);
    auto duplicate = std::string(fixture);
    duplicate.insert(1, "\"v\":1,");
    assert(!codec.parse_entity(duplicate, decoded));
  }
  for (const auto *fixture : COLOR_BAD_FIXTURES)
    assert(!codec.parse_entity(fixture, parsed));
  for (const auto *fixture : COLOR_HA_FIXTURES) {
    assert(codec.parse_entity(fixture, parsed));
    assert(codec.serialize_entity(parsed, output, sizeof(output), length));
    assert(codec.parse_entity({output, length}, decoded));
    assert(parsed.state == decoded.state && parsed.operation == decoded.operation);
    std::cout << "HA_WIRE " << std::string(output, length) << "\n";
  }
  assert(codec.high_water() <= 5120);
  const std::string bind =
      R"({"v":1,"type":"entity_detail_color_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","color_version":1})";
  ControlText<32> channel;
  assert(codec.parse_detail_color_bind(bind, channel));
  auto duplicate = bind;
  duplicate.insert(1, "\"color_version\":1,");
  assert(!codec.parse_detail_color_bind(duplicate, channel));
  {
    Sink sink;
    MaterialEntityControl owner(sink);
    assert(owner.set_enabled(true) && owner.bind_generic(channel));
    assert(owner.set_detail_profiles(1));
    assert(owner.accept_detail_features(channel, 1));
    assert(!owner.detail_color_enabled() && !owner.accept_detail_color(channel));
    assert(codec.parse_entity(COLOR_FIXTURES[0], parsed));
    assert(owner.request_open_detail(parsed.identity.context, 1));
    parsed.identity = sink.messages.back().identity;
    parsed.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    assert(!owner.receive(parsed, 2));
  }
  {
    Fixture f;
    assert(!f.owner.can_apply_detail_color());
    assert(!f.owner.begin_detail_color_apply(1, f.now++));
    f.draft(125500, 70250);
    assert(!f.owner.can_apply_detail_color() && f.sink.commands() == 0);
    f.draft();
    assert(f.owner.can_apply_detail_color());
    assert(f.owner.begin_detail_navigation(8, f.now++));
    assert(f.owner.cancel_detail_color(8, f.now++));
    assert(!f.owner.detail_color_draft().active && !f.owner.can_apply_detail_color() && f.sink.commands() == 0);
  }
  {
    Fixture f;
    f.draft();
    assert(f.owner.begin_detail_color_apply(8, f.now++));
    assert(f.owner.release_detail_color_apply(8, f.now++));
    assert(!f.owner.release_detail_color_apply(8, f.now++));
    assert(f.sink.commands() == 1 && f.owner.is_command_pending());
    const auto &command = f.sink.messages.back();
    assert(codec.serialize_entity(command, output, sizeof(output), length));
    const std::string wire(output, length);
    assert(wire.find("target_hue_milli\":220123") != std::string::npos);
    assert(wire.find("target_saturation_milli\":51001") != std::string::npos);
    assert(wire.find("brightness") == std::string::npos && wire.find("rgb") == std::string::npos);
    assert(!f.owner.begin_detail_color(9, f.now++));
    f.update();
    auto result = command;
    result.kind = ControlKind::CONTROL_KIND_RESULT;
    result.state.state_seq = f.message.state.state_seq;
    result.result = ControlResult::CONTROL_RESULT_STATE_MATCHES;
    result.reason = ControlReason::CONTROL_REASON_NONE;
    assert(f.owner.receive(result, f.now++));
    assert(!f.owner.is_command_pending() && !f.owner.detail_color_draft().active);
    assert(!f.owner.receive(result, f.now++));
  }
  {
    Fixture f(3);
    assert(f.owner.can_apply_detail_color());
    assert(f.owner.begin_detail_color_apply(8, f.now++));
    assert(f.owner.release_detail_color_apply(8, f.now++));
    const auto *target = f.sink.messages.back().state.family_if<ControlDetailSnapshot>()->color();
    assert(target->hue_milli == 125500 && target->saturation_milli == 70250 && f.sink.commands() == 1);
  }
  {
    Fixture f;
    f.draft();
    f.message.state.title.assign("Only title metadata");
    f.update(false);
    assert(f.owner.detail_color_draft().active && f.owner.can_apply_detail_color());
    assert(f.owner.begin_detail_color_apply(8, f.now++));
    f.update();  // Even A -> A at a higher authoritative generation retires held input.
    assert(!f.owner.detail_color_draft().active);
    assert(!f.owner.release_detail_color_apply(8, f.now++));
    assert(f.sink.commands() == 0);
    assert(f.owner.begin_detail_color(9, f.now++));
    f.update();
    assert(!f.owner.update_detail_color(9, 12000, 100000));
    assert(!f.owner.release_detail_color(9, f.now++));
  }
  {
    Fixture f;
    f.draft();
    auto &color = *f.message.state.family_if<ControlDetailSnapshot>()->color();
    // The public accessor is const; changes are made through the typed variant.
    (void) color;
    auto &mutable_color = std::get<ControlFacetColor>(f.message.state.family_if<ControlDetailSnapshot>()->data);
    mutable_color.source_mode = 3;
    f.update();
    assert(!f.owner.detail_color_draft().active && f.owner.can_apply_detail_color());
    mutable_color.reason = 4;
    f.message.state.family_if<ControlDetailSnapshot>()->editable = false;
    f.update();
    assert(!f.owner.can_detail_color() && !f.owner.can_apply_detail_color());
  }
  {
    Fixture f;
    f.draft();
    assert(f.owner.begin_detail_color_apply(9, f.now++));
    f.sink.callback = [&] {
      assert(!f.owner.begin_detail_color(10, f.now));
      assert(!f.owner.disconnect());
    };
    assert(f.owner.release_detail_color_apply(9, f.now++));
    assert(f.sink.commands() == 1);
    f.sink.callback = {};
    f.owner.tick(40000);
    assert(!f.owner.has_selection() && !f.owner.detail_color_draft().active);
  }
  {
    Fixture f;
    f.draft();
    assert(f.owner.request_facet(ControlFacet::CONTROL_FACET_MAIN, f.now++));
    assert(!f.owner.detail_color_draft().active && !f.owner.begin_detail_color_apply(9, f.now++));
    assert(f.sink.commands() == 0);
    assert(f.owner.disconnect());
    assert(!f.owner.detail_color_bound());
  }
  std::cout << "Color strict codec/arena=" << codec.high_water()
            << "/5120 and same-owner draft, CT, metadata, ABA, pending, reentrancy passed\n";
#endif
}

#include "material_entity_control.h"
#include "material_entity_control_codec.h"
#include "fixtures.h"
#include "reviews.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace esphome::lvgl_material;
struct Sink : MaterialEntityControlSink {
  ControlMessage last;
  unsigned count = 0;
  bool emit(const ControlMessage &m) override {
    last = m;
    count++;
    return true;
  }
  void view_event(ControlViewEvent) override {}
};
struct Fixture {
  Sink sink;
  MaterialEntityControl owner{sink};
  MaterialEntityControlCodec codec;
  ControlMessage message;
  uint32_t now = 1;
  ControlText<32> channel;
  Fixture(bool climate = false) {
    channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    assert(owner.set_enabled(true));
    assert(owner.bind_generic(channel));
    assert(owner.set_detail_profiles(3));
    assert(owner.accept_detail_features(channel, 3));
    assert(codec.parse_entity(FIXTURES[climate ? 3 : 0], message));
    auto context = message.identity.context;
    if (climate)
      assert(owner.request_open(context, now++));
    else
      assert(owner.request_open_detail(context, now++));
    message.kind = ControlKind::CONTROL_KIND_GRANT;
    message.identity = sink.last.identity;
    message.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    assert(owner.receive(message, now++));
    assert(owner.is_detail());
    assert(owner.is_climate_detail() == climate);
    assert(!owner.is_panel());
    assert(owner.set_view_ready(true, now++));
    if (!climate)
      assert(owner.set_scalar_ready(true));
    message.kind = ControlKind::CONTROL_KIND_STATE;
  }
  void update() {
    message.state.state_seq++;
    assert(owner.receive(message, now++));
  }
  void facet(size_t fixture) {
    ControlMessage next;
    assert(codec.parse_entity(FIXTURES[fixture], next));
    auto facet = (*next.state.family_if<ControlDetailSnapshot>()).facet;
    assert(owner.request_facet(facet, now++));
    assert(sink.last.kind == ControlKind::CONTROL_KIND_FACET);
    assert(!owner.request_facet(facet, now++));
    next.kind = ControlKind::CONTROL_KIND_STATE;
    next.identity = message.identity;
    next.state.state_seq = message.state.state_seq + 1;
    (*next.state.family_if<ControlDetailSnapshot>()).view_seq = sink.last.view_seq;
    (*next.state.family_if<ControlDetailSnapshot>()).state_generation =
        (*message.state.family_if<ControlDetailSnapshot>()).state_generation;
    ControlMessage receipt;
    receipt.kind = ControlKind::CONTROL_KIND_FACET_RESULT;
    receipt.generic = true;
    receipt.identity = next.identity;
    receipt.state.state_seq = next.state.state_seq;
    receipt.view_seq = (*next.state.family_if<ControlDetailSnapshot>()).view_seq;
    receipt.facet = facet;
    receipt.facet_ready = true;
    assert(!owner.receive(receipt, now++));
    assert(owner.receive(next, now++));
    assert(!owner.detail_view_ready());
    assert(owner.receive(receipt, now++));
    assert(owner.detail_view_ready());
    message = next;
    assert(owner.set_view_ready(true, now++));
  }
};
int main() {
  MaterialEntityControlCodec codec;
  char output[2049];
  size_t size;
  ControlMessage m, parsed;
  for (size_t i = 0; i < std::size(FIXTURES); i++) {
    if (!codec.parse_entity(FIXTURES[i], m)) {
      std::cerr << "fixture decode " << i << " highwater " << codec.high_water() << "\n";
      return 2;
    }
    assert((*m.state.family_if<ControlDetailSnapshot>()).present);
    assert(codec.serialize_entity(m, output, sizeof(output), size));
    assert(codec.parse_entity({output, size}, parsed));
    assert(parsed.state == m.state);
    auto duplicate = std::string(FIXTURES[i]);
    duplicate.insert(1, "\"v\":1,");
    assert(!codec.parse_entity(duplicate, parsed));
    auto unknown = std::string(FIXTURES[i]);
    unknown.insert(1, "\"extra\":0,");
    assert(!codec.parse_entity(unknown, parsed));
  }
  ControlText<32> channel;
  uint8_t mask = 0;
  const std::string bind =
      R"({"v":1,"type":"entity_detail_features_v1","detail_version":1,"light_detail_version":1,"climate_panel_version":1,"channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})";
  assert(codec.parse_detail_bind(bind, channel, mask) && mask == 3);
  auto duplicate = bind;
  duplicate.insert(1, "\"v\":1,");
  assert(!codec.parse_detail_bind(duplicate, channel, mask));
  {
    Fixture f;
    assert(f.sink.last.detail_open);
    assert(f.codec.serialize_entity(f.sink.last, output, sizeof(output), size));
    assert(std::string_view(output, size).find("detail_version") != std::string_view::npos);
    assert(!f.owner.accept_detail_features(f.channel, 1));
    f.facet(1);
    assert(f.owner.can_detail_number());
    assert(f.owner.begin_detail_number(1, f.now++));
    auto &n = std::get<ControlFacetNumber>((*f.message.state.family_if<ControlDetailSnapshot>()).data);
    assert(n.index == 700);
    assert(f.owner.release_detail_number(1, 701, f.now++));
    assert(f.sink.last.facet_target_index == 701 && f.sink.last.expected_index == 700);
    assert(f.sink.last.view_seq == 1);
    assert(f.codec.serialize_entity(f.sink.last, output, sizeof(output), size));
    assert(!f.owner.request_facet(ControlFacet::CONTROL_FACET_MAIN, f.now++));
  }
  {
    Fixture f(true);
    assert(f.owner.begin_detail_number(2, f.now++));
    auto &main = std::get<ControlClimateMain>((*f.message.state.family_if<ControlDetailSnapshot>()).data);
    main.current.assign("98");
    main.activity = 2;
    f.update();
    assert(f.owner.is_dragging());
    assert(f.owner.release_detail_number(2, 12, f.now++));
    assert(f.sink.last.operation == ControlOperation::CONTROL_OPERATION_FACET_NUMBER);
  }
  {
    Fixture f(true);
    assert(f.owner.begin_detail_number(2, f.now++));
    auto &main = std::get<ControlClimateMain>((*f.message.state.family_if<ControlDetailSnapshot>()).data);
    main.target.assign("22");
    main.number.value.assign("22");
    main.number.index = 12;
    (*f.message.state.family_if<ControlDetailSnapshot>()).state_generation++;
    f.update();
    main.target.assign("21.5");
    main.number.value.assign("21.5");
    main.number.index = 11;
    (*f.message.state.family_if<ControlDetailSnapshot>()).state_generation++;
    f.update();
    assert(!f.owner.release_detail_number(2, 13, f.now++));
  }
  {
    Fixture f(true);
    f.facet(6);
    assert((*f.owner.state().family_if<ControlDetailSnapshot>()).choice()->known &&
           (*f.owner.state().family_if<ControlDetailSnapshot>()).choice()->index == 255);
    assert(f.owner.begin_detail_choice(4, 0, f.now++));
    assert(f.owner.release_detail_choice(4, f.now++));
    assert(!f.sink.last.expected_index_known);
    assert(f.codec.serialize_entity(f.sink.last, output, sizeof(output), size));
    assert(std::string_view(output, size).find("\"expected_index\":null") != std::string_view::npos);
  }
  {
    Fixture f;
    assert(f.owner.request_facet(ControlFacet::CONTROL_FACET_EFFECT, f.now++));
    f.owner.tick(f.now + 10001);
    assert(!f.owner.is_detail_view_pending() && !f.owner.detail_view_ready());
    assert(f.owner.can_request_facet(ControlFacet::CONTROL_FACET_MAIN));
    assert(f.owner.bind_generic(f.channel));
    assert(f.owner.bound_detail_profiles() == 0);
  }
  {
    Fixture f(true);
    auto &main = std::get<ControlClimateMain>((*f.message.state.family_if<ControlDetailSnapshot>()).data);
    main.mode = 0;
    main.activity = 0;
    main.target.assign("");
    main.current.assign("");
    main.number.known = false;
    main.number.index = 0;
    main.number.value.assign("--");
    (*f.message.state.family_if<ControlDetailSnapshot>()).available = false;
    (*f.message.state.family_if<ControlDetailSnapshot>()).editable = false;
    (*f.message.state.family_if<ControlDetailSnapshot>()).state_generation++;
    f.update();
    assert(!f.owner.can_detail_number());
    assert(f.owner.can_request_facet(ControlFacet::CONTROL_FACET_HVAC_MODE));
  }
  {
    Fixture f(true);
    auto &main = std::get<ControlClimateMain>((*f.message.state.family_if<ControlDetailSnapshot>()).data);
    main.number.domain.minimum = 15 * NUMBER_SCALE;
    main.number.domain.max_index = 30;
    main.number.index = 13;
    (*f.message.state.family_if<ControlDetailSnapshot>()).state_generation++;
    f.message.state.state_seq++;
    assert(!f.owner.receive(f.message, f.now++));
    assert(!f.owner.has_selection());
  }
  for (const auto &sequence : REVIEWS) {
    Fixture f(true);
    ControlMessage state;
    for (size_t i = 0; i < 3 && sequence[i]; i++) {
      assert(f.codec.parse_entity(sequence[i], state));
      state.identity = f.owner.identity();
      if (i == 0) {
        assert(f.owner.invalidate_admission(ControlReason::CONTROL_REASON_USER));
        assert(f.owner.request_open(state.identity.context, f.now++));
        state.identity = f.sink.last.identity;
        state.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        state.kind = ControlKind::CONTROL_KIND_GRANT;
      } else
        state.kind = ControlKind::CONTROL_KIND_STATE;
      assert(f.owner.receive(state, f.now++));
      assert(f.owner.has_selection());
    }
    assert(f.sink.count >= 1);
  }
  std::cout << "Detail fixtures=" << std::size(FIXTURES) << " arena=" << codec.high_water()
            << "/5120 negotiation/generations/facets passed\n";
}

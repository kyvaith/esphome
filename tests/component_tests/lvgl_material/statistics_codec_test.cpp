#include "material_entity_control_codec.h"
#include "statistics_cases.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
using namespace esphome::lvgl_material;
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
using Extra = ControlExtraSnapshot;
using Statistics = Extra::Statistics;
struct Sink : MaterialEntityControlSink {
  ControlMessage sent;
  unsigned commands{0};
  bool emit(const ControlMessage &message) override {
    sent = message;
    commands += message.kind == ControlKind::CONTROL_KIND_COMMAND;
    return true;
  }
  void view_event(ControlViewEvent) override {}
};
ControlMessage initial(MaterialEntityControlCodec &codec) {
  for (const auto &wire : WIRES) {
    ControlMessage m;
    assert(codec.parse_entity(wire.raw, m));
    if (std::get<Statistics>(m.state.family_if<Extra>()->data).status == 1) {
      m.kind = ControlKind::CONTROL_KIND_GRANT;
      m.state.state_seq = 1;
      m.state.family_if<Extra>()->state_generation = 1;
      return m;
    }
  }
  assert(false);
  return {};
}
struct Harness {
  Sink sink;
  MaterialEntityControlCodec codec;
  MaterialEntityControl owner{sink};
  ControlText<32> channel;
  ControlMessage grant;
  Harness() {
    channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    assert(owner.set_enabled(true) && owner.set_extra_profiles(1) && owner.set_info_statistics(true));
    assert(owner.bind_generic(channel));
    assert(!owner.accept_info_statistics(channel));
    assert(owner.accept_extra_features(channel, 1) && owner.accept_info_statistics(channel));
    grant = initial(codec);
    assert(owner.request_open(grant.identity.context, 1));
    grant.identity = owner.identity();
    grant.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    assert(owner.receive(grant, 2) && owner.set_view_ready(true, 3));
    assert(!owner.accept_info_statistics(channel));
  }
  ControlMessage state() {
    auto s = grant;
    s.kind = ControlKind::CONTROL_KIND_STATE;
    s.state = owner.state();
    s.state.state_seq++;
    return s;
  }
};
void owner_regressions() {
  // Every plotted field belongs to the semantic generation. These changes
  // remain individually valid, so rejection must come from owner retirement.
  using Mutation = void (*)(Statistics &);
  const Mutation mutations[] = {
      [](Statistics &s) { s.current_status = 2; },
      [](Statistics &s) { s.window_end_hour++; },
      [](Statistics &s) { s.unit.assign("different unit"); },
      [](Statistics &s) { s.start.assign("changed start"); },
      [](Statistics &s) { s.end.assign("changed end"); },
      [](Statistics &s) { s.zone.assign("changed zone"); },
      [](Statistics &s) { s.axis_min.assign("-20"); },
      [](Statistics &s) { s.axis_max.assign("20"); },
      [](Statistics &s) { s.points[0][1] = s.points[0][2]; },
      [](Statistics &s) { s.valid_mask &= ~1U; s.points[0] = {}; },
      [](Statistics &s) {
        s.status = 2;
        s.reason = 1;
        s.valid_mask = 0;
        s.points = {};
        s.axis_min = {};
        s.axis_max = {};
        s.flat = false;
      },
  };
  for (auto mutate : mutations) {
    Harness h;
    auto state = h.state();
    auto *extra = state.state.family_if<Extra>();
    mutate(std::get<Statistics>(extra->data));
    assert(control_valid_statistics(*extra));
    assert(extra->data != h.owner.state().family_if<Extra>()->data);
    assert(!h.owner.receive(state, 10));
    assert(!h.owner.has_selection() && h.owner.state().family_if<Extra>() == nullptr);
    assert(h.sink.commands == 0);
  }
  {
    Harness h;
    auto next = h.state();
    auto *extra = next.state.family_if<Extra>();
    extra->state_generation++;
    auto &s = std::get<Statistics>(extra->data);
    s.status = 2; s.reason = 1; s.valid_mask = 0; s.points = {};
    s.axis_min = {}; s.axis_max = {}; s.flat = false;
    assert(h.owner.receive(next, 10));
    const auto &accepted = std::get<Statistics>(h.owner.state().family_if<Extra>()->data);
    assert(accepted.valid_mask == 0 && accepted.points == Statistics{}.points);
    assert(h.sink.commands == 0);
  }
  {
    Harness h;
    assert(h.owner.invalidate_admission(ControlReason::CONTROL_REASON_NAVIGATION));
    assert(!h.owner.receive(h.grant, 11));
    ControlText<32> replacement;
    replacement.assign("cccccccccccccccccccccccccccccccc");
    assert(h.owner.bind_generic(replacement));
    assert(!h.owner.info_statistics_bound());
    assert(!h.owner.accept_info_statistics(h.channel));
    assert(h.owner.accept_extra_features(replacement, 1));
    assert(h.owner.accept_info_statistics(replacement));
    assert(!h.owner.receive(h.grant, 12));
    assert(h.owner.state().family_if<Extra>() == nullptr && h.sink.commands == 0);
  }
  {
    Harness h;
    h.owner.tick(30003);
    assert(!h.owner.has_selection() && h.owner.state().family_if<Extra>() == nullptr);
    assert(h.sink.commands == 0);
  }
  {
    Harness h;
    assert(h.owner.set_extra_profiles(0));
    assert(!h.owner.has_selection() && !h.owner.info_statistics_bound());
    assert(h.owner.state().family_if<Extra>() == nullptr && h.sink.commands == 0);
  }
  {
    Harness h;
    assert(h.owner.set_enabled(false));
    assert(!h.owner.has_selection() && !h.owner.info_statistics_bound());
    assert(h.owner.state().family_if<Extra>() == nullptr && h.sink.commands == 0);
  }
  std::puts("PASS owner generation-field fences, empty retirement, stale grant/rebind, expiry, disable, no commands");
}
int main(int argc, char **argv) {
  owner_regressions();
  FILE *output = argc > 1 ? std::fopen(argv[1], "wb") : nullptr;
  assert(argc == 1 || output != nullptr);
  MaterialEntityControlCodec codec;
  unsigned rows = 0, negative = 0, host_abi_rejections = 0;
  size_t max_wire = 0;
  for (const auto &wire : WIRES) {
    ControlMessage m, again;
    if (sizeof(void *) == 8 && std::strcmp(wire.name, "maximum-valid-storage") == 0) {
      // Statistics1 targets P4/32-bit. The same fixed arena deliberately does
      // not promise this worst-case DOM on a host with larger pointer/slot ABI.
      // Failure must leave the caller and active owner untouched, then recover.
      Harness h;
      m = h.grant;
      std::array<unsigned char, sizeof(ControlMessage)> before;
      std::memcpy(before.data(), &m, sizeof(m));
      const auto owner_before = h.owner.state();
      assert(!codec.parse_entity(wire.raw, m));
      assert(std::memcmp(before.data(), &m, sizeof(m)) == 0);
      assert(h.owner.has_selection() && h.owner.state() == owner_before && h.sink.commands == 0);
      assert(codec.high_water() <= 5120);
      assert(codec.parse_entity(WIRES[0].raw, again));
      host_abi_rejections++;
      continue;
    }
    if (!codec.parse_entity(wire.raw, m)) {
      std::printf("FAIL parse %s arena=%zu\n", wire.name, codec.high_water());
      return 1;
    }
    char data[2049];
    size_t length;
    assert(codec.serialize_entity(m, data, sizeof(data), length));
    assert(length <= 2048 && codec.parse_entity({data, length}, again) && m.state == again.state);
    max_wire = std::max(max_wire, length);
    if (output) { std::fwrite(data, 1, length, output); std::fputc('\n', output); }
    rows++;
    ArduinoJson::JsonDocument original;
    assert(!deserializeJson(original, wire.raw));
    auto bad = [&](const char *field, ArduinoJson::JsonVariantConst value, bool state = true) {
      ArduinoJson::JsonDocument doc;
      doc.set(original);
      if (state) doc["panel_state"][field].set(value);
      else doc[field].set(value);
      serializeJson(doc, data, sizeof(data));
      assert(!codec.parse_entity(data, again));
      negative++;
    };
    ArduinoJson::JsonDocument value;
    value.set(true);
    for (const char *key : {"window_end_hour", "valid_mask"}) bad(key, value.as<ArduinoJson::JsonVariantConst>());
    value.set(nullptr);
    bad("available", value.as<ArduinoJson::JsonVariantConst>(), false);
    value.set(1); bad("eligible_actions", value.as<ArduinoJson::JsonVariantConst>(), false);
    bad("unexpected", value.as<ArduinoJson::JsonVariantConst>());
    if (std::get<Statistics>(m.state.family_if<Extra>()->data).status == 1) {
      const auto raw = original["panel_state"]["packed_q"].as<std::string>();
      for (const auto &bad_string : {raw + "0", raw.substr(1), std::string("FFFF") + raw.substr(4),
                                    std::string("03e9") + raw.substr(4), std::string(288, 'g')}) {
        value.set(bad_string); bad("packed_q", value.as<ArduinoJson::JsonVariantConst>());
      }
      for (const char *axis : {"nan", "inf", "1e999", "0x10", " 1", "1e", "-", "1\n"}) {
        value.set(axis); bad("axis_min_text", value.as<ArduinoJson::JsonVariantConst>());
      }
    }
  }
  ControlText<32> channel;
  const std::string bind = "{\"v\":1,\"type\":\"entity_info_statistics_v1\",\"channel\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"statistics_version\":1}";
  assert(codec.parse_info_statistics_bind(bind, channel));
  assert(!codec.parse_info_statistics_bind(bind + "x", channel));
  assert(!codec.parse_info_statistics_bind("{\"v\":1," + bind.substr(1), channel));
  assert(!codec.parse_info_statistics_bind("{}", channel));
  { Harness h; assert(h.owner.is_statistics()); assert(!h.owner.can_extra_lock()); assert(!h.owner.begin_extra_lock(1, 10));
    auto s = h.state(); s.state.title.assign("Metadata only");
    assert(h.owner.receive(s, 10000)); assert(h.owner.has_selection()); h.owner.tick(30005); assert(h.owner.has_selection());
    assert(h.owner.invalidate_admission(ControlReason::CONTROL_REASON_NAVIGATION));
    assert(h.owner.state().family_if<Extra>() == nullptr && h.sink.commands == 0); }
  { Harness h; auto s=h.state(); std::get<Statistics>(s.state.family_if<Extra>()->data).current_status=2;
    assert(!h.owner.receive(s, 10) && !h.owner.has_selection() && h.owner.state().family_if<Extra>() == nullptr); }
  { Harness h; auto s=h.state(); s.state.family_if<Extra>()->state_generation++;
    std::get<Statistics>(s.state.family_if<Extra>()->data).current_status=2;
    assert(h.owner.receive(s, 10));
    auto old=h.grant; old.kind=ControlKind::CONTROL_KIND_STATE;old.state.state_seq=3;
    assert(!h.owner.receive(old, 11) && h.owner.has_selection());
    assert(h.owner.disconnect() && !h.owner.info_statistics_bound() && h.owner.state().family_if<Extra>() == nullptr); }
  { Harness h; assert(h.owner.set_info_statistics(false)); assert(!h.owner.has_selection()); }
  { Sink sink; MaterialEntityControl owner(sink); ControlText<32> c;c.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    assert(owner.set_enabled(true) && owner.set_extra_profiles(1) && owner.bind_generic(c) && owner.accept_extra_features(c,1));
    assert(!owner.accept_info_statistics(c)); auto m=initial(codec); assert(owner.request_open(m.identity.context,1));
    m.identity=owner.identity();m.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");assert(!owner.receive(m,2)); }
  if (output) std::fclose(output);
  std::printf("PASS rows=%u negatives=%u host_abi_rejections=%u max_wire=%zu arena=%zu statistics=%zu snapshot=%zu message=%zu owner=%zu\n",
               rows,negative,host_abi_rejections,max_wire,codec.high_water(),sizeof(Statistics),sizeof(ControlSnapshot),sizeof(ControlMessage),sizeof(MaterialEntityControl));
}
#else
int main() {
  MaterialEntityControlCodec codec;
  for (const auto &wire : WIRES) { ControlMessage m; assert(!codec.parse_entity(wire.raw,m)); }
  std::puts("PASS default off rejects Statistics wire");
}
#endif

#include "material_entity_control_codec.h"
#include "corpus.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace esphome::lvgl_material;
// With every existing family enabled, Extra must fit the already retained
// largest family; it must not expand every live message and owner snapshot.
static_assert(sizeof(ControlExtraSnapshot) <= sizeof(ControlSnapshot::Robot));
struct Sink : MaterialEntityControlSink {
  bool emit(const ControlMessage &) override { return true; }
  void view_event(ControlViewEvent) override {}
};
int main() {
  std::setbuf(stdout, nullptr);
  MaterialEntityControlCodec codec;
  unsigned messages = 0, features = 0, failures = 0, admission = 0;
  for (const auto &r : MESSAGES) {
    ControlMessage value;
    value.kind = ControlKind::CONTROL_KIND_BIND;
    value.generic = true;
    value.identity.channel.assign("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    std::array<unsigned char, sizeof(value)> sentinel;
    std::memcpy(sentinel.data(), &value, sizeof(value));
    const bool parsed = codec.parse_entity(r.raw, value);
    if (parsed != r.structural) {
      printf("FAIL parse %s expected%u actual%u\n", r.id, r.structural, parsed);
      failures++;
      continue;
    }
    if (!parsed) {
      assert(std::memcmp(sentinel.data(), &value, sizeof(value)) == 0);
      messages++;
      continue;
    }
    char wire[2049];
    size_t length = 0;
    ControlMessage again;
    assert(codec.serialize_entity(value, wire, sizeof(wire), length));
    assert(codec.parse_entity({wire, length}, again));
    assert(value.state == again.state && value.identity == again.identity && value.kind == again.kind &&
           value.operation == again.operation);
    if (r.admission_reject) {
      Sink sink;
      MaterialEntityControl owner(sink);
      ControlText<32> channel;
      channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
      assert(owner.set_enabled(true) && owner.set_extra_profiles(3) && owner.bind_generic(channel));
      if (r.profiles)
        assert(owner.accept_extra_features(channel, r.profiles));
      const bool opened = owner.request_open(value.identity.context, 1);
      if (opened) {
        value.identity = owner.identity();
        value.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        if (r.server)
          assert(!owner.receive(value, 2));
      }
      if (!r.server)
        assert(!owner.begin_extra_lock(1, 2));
      assert(!owner.has_selection() && !owner.can_extra_lock());
      admission++;
    }
    messages++;
  }
  for (const auto &r : FEATURES) {
    Sink sink;
    MaterialEntityControl owner(sink);
    ControlText<32> current;
    current.assign(r.channel);
    assert(owner.set_enabled(true) && owner.set_extra_profiles(r.configured) && owner.bind_generic(current));
    if (r.previous)
      assert(owner.accept_extra_features(current, r.previous));
    if (r.active) {
      ControlContext c;
      c.slot = 2;
      c.revision = 1;
      c.card_id.assign("card-2");
      c.control_kind = ControlEntityKind::CONTROL_ENTITY_PANEL;
      assert(owner.request_open(c, 1));
      ControlMessage grant;
      assert(codec.parse_entity(MESSAGES[0].raw, grant));
      grant.identity = owner.identity();
      grant.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
      assert(owner.receive(grant, 2));
      assert(owner.has_selection());
    }
    ControlText<32> parsed;
    parsed.assign("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    uint16_t mask = 7;
    const bool ok = codec.parse_extra_features(r.raw, parsed, mask, r.configured, r.bind);
    const bool accepted = r.bind && ok && owner.accept_extra_features(parsed, mask);
    if (r.bind) {
      if (accepted != r.accepted) {
        printf("FAIL bind %s expected%u actual%u\n", r.id, r.accepted, accepted);
        failures++;
      }
      assert(owner.bound_extra_profiles() == (accepted ? mask : r.previous));
    } else {
      // Query is read-only: do not call the binder or change its state.
      const unsigned response = ok && parsed == current ? mask : 0;
      if (ok != r.structure || response != r.expected) {
        printf("FAIL query %s expected%u/%u actual%u/%u\n", r.id, r.structure, r.expected, ok, response);
        failures++;
      }
      assert(owner.bound_extra_profiles() == r.previous);
    }
    if (!ok)
      assert(mask == 7 && parsed.view() == "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    features++;
  }
  printf("RESULT messages=%u admissions=%u features=%u failures=%u arena=%zu\n", messages, admission, features,
         failures, codec.high_water());
  return failures ? 1 : 0;
}

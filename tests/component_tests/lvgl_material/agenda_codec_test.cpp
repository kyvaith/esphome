#include "material_entity_control_codec.h"
#include "agenda_cases.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
using namespace esphome::lvgl_material;
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
using Extra = ControlExtraSnapshot;
using Agenda = Extra::Agenda;
struct Sink : MaterialEntityControlSink {
  ControlMessage sent;
  unsigned pages{0}, commands{0};
  std::function<void()> reenter;
  bool emit(const ControlMessage &message) override {
    sent = message;
    pages += message.kind == ControlKind::CONTROL_KIND_PAGE;
    commands += message.kind == ControlKind::CONTROL_KIND_COMMAND;
    if (reenter)
      reenter();
    return true;
  }
  void view_event(ControlViewEvent) override {}
};
ControlMessage initial(MaterialEntityControlCodec &codec) {
  for (const auto &r : WIRES) {
    ControlMessage m;
    assert(codec.parse_entity(r.raw, m));
    const auto *e = m.state.family_if<Extra>();
    if (e && std::get<Agenda>(e->data).count > 1) {
      m.kind = ControlKind::CONTROL_KIND_GRANT;
      m.state.state_seq = 1;
      e = nullptr;
      auto &state = *m.state.family_if<Extra>();
      state.state_generation = 1;
      auto &a = std::get<Agenda>(state.data);
      a.index = 0;
      a.request_seq = 0;
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
    assert(owner.set_enabled(true) && owner.set_extra_profiles(1) && owner.set_info_agenda(true));
    assert(owner.bind_generic(channel));
    assert(!owner.accept_info_agenda(channel));
    assert(owner.accept_extra_features(channel, 1) && owner.accept_info_agenda(channel));
    grant = initial(codec);
    assert(owner.request_open(grant.identity.context, 1));
    grant.identity = owner.identity();
    grant.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    assert(owner.receive(grant, 2) && owner.set_view_ready(true, 3));
    assert(!owner.accept_info_agenda(channel));
  }
  void page(uint32_t now = 10) {
    assert(owner.begin_agenda_page(1, 1, now));
    sink.reenter = [&] { assert(!owner.begin_agenda_page(2, 1, now) && !owner.disconnect()); };
    assert(owner.release_agenda_page(1, now + 1));
    sink.reenter = {};
    assert(owner.agenda_page_pending() && sink.commands == 0);
    assert(!owner.release_agenda_page(1, now + 2));
  }
  ControlMessage state() {
    ControlMessage m = grant;
    m.kind = ControlKind::CONTROL_KIND_STATE;
    m.state = owner.state();
    m.state.state_seq++;
    auto &a = std::get<Agenda>(m.state.family_if<Extra>()->data);
    a.index = sink.sent.value;
    a.request_seq = sink.sent.command_seq;
    a.summary.assign("Second event");
    return m;
  }
  ControlMessage receipt(const ControlMessage &state) {
    ControlMessage m;
    m.generic = true;
    m.kind = ControlKind::CONTROL_KIND_PAGE_RESULT;
    m.identity = owner.identity();
    m.command_seq = sink.sent.command_seq;
    m.value = sink.sent.value;
    m.state.state_seq = state.state.state_seq;
    m.enabled = true;
    m.reason = ControlReason::CONTROL_REASON_NONE;
    return m;
  }
};
int main(int argc, char **argv) {
  std::setbuf(stdout, nullptr);
  FILE *serialized = argc > 1 ? std::fopen(argv[1], "wb") : nullptr;
  if (argc > 1)
    assert(serialized != nullptr);
  MaterialEntityControlCodec codec;
  unsigned rows = 0, negative = 0;
  for (const auto &r : WIRES) {
    ControlMessage m, again;
    if (!codec.parse_entity(r.raw, m)) {
      printf("parse failed %s\n", r.name);
      return 1;
    }
    char out[2049];
    size_t length = 0;
    assert(codec.serialize_entity(m, out, sizeof(out), length) && length <= 2048);
    if (serialized) {
      std::fwrite(out, 1, length, serialized);
      std::fputc('\n', serialized);
    }
    assert(codec.parse_entity({out, length}, again));
    assert(m.state == again.state && m.kind == again.kind && m.identity == again.identity &&
           m.command_seq == again.command_seq && m.value == again.value && m.enabled == again.enabled);
    rows++;
    ArduinoJson::JsonDocument doc;
    assert(!deserializeJson(doc, r.raw));
    doc["unexpected"] = 1;
    serializeJson(doc, out, sizeof(out));
    assert(!codec.parse_entity(out, again));
    negative++;
    doc.remove("unexpected");
    if (doc["panel_state"].is<ArduinoJson::JsonObject>()) {
      for (const char *key : {"request_seq", "count", "index", "truncated_fields"}) {
        ArduinoJson::JsonDocument copy;
        copy.set(doc);
        copy["panel_state"][key] = true;
        serializeJson(copy, out, sizeof(out));
        assert(!codec.parse_entity(out, again));
        negative++;
      }
      for (const char *key : {"available", "eligible_actions"}) {
        ArduinoJson::JsonDocument copy;
        copy.set(doc);
        copy[key] = nullptr;
        serializeJson(copy, out, sizeof(out));
        assert(!codec.parse_entity(out, again));
        negative++;
      }
    }
  }
  ControlText<32> channel;
  const char *bind = "{\"v\":1,\"type\":\"entity_info_agenda_v1\",\"channel\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
                     "\"agenda_version\":1}";
  assert(codec.parse_info_agenda_bind(bind, channel));
  for (const char *bad : {"{}",
                          "{\"v\":true,\"type\":\"entity_info_agenda_v1\",\"channel\":"
                          "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"agenda_version\":1}",
                          "{\"v\":1,\"v\":1,\"type\":\"entity_info_agenda_v1\",\"channel\":"
                          "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"agenda_version\":1}"}) {
    assert(!codec.parse_info_agenda_bind(bad, channel));
    negative++;
  }
  assert(!codec.parse_info_agenda_bind(std::string(bind) + "garbage", channel));
  {
    Harness h;
    assert(!h.owner.begin_agenda_page(1, -1, 10));
    h.page();
    auto s = h.state(), r = h.receipt(s);
    assert(h.owner.receive(r, 12) && h.owner.agenda_page_pending());
    assert(h.owner.receive(s, 13) && !h.owner.agenda_page_pending());
    assert(!h.owner.receive(r, 14));
    assert(h.sink.pages == 1 && h.sink.commands == 0);
  }
  {
    Harness h;
    h.page();
    auto s = h.state(), r = h.receipt(s);
    assert(h.owner.receive(s, 12) && h.owner.agenda_page_pending());
    assert(h.owner.receive(r, 13) && !h.owner.agenda_page_pending());
  }
  {
    Harness h;
    h.page();
    auto s = h.state(), r = h.receipt(s);
    s.state.family_if<Extra>()->state_generation++;
    auto &a = std::get<Agenda>(s.state.family_if<Extra>()->data);
    a.index = 0;
    a.request_seq = 0;
    assert(h.owner.receive(s, 12) && !h.owner.agenda_page_pending());
    assert(!h.owner.receive(r, 13));
  }
  {
    Harness h;
    assert(h.owner.begin_agenda_page(1, 1, 10));
    auto s = h.grant;
    s.kind = ControlKind::CONTROL_KIND_STATE;
    s.state.state_seq = 2;
    s.state.family_if<Extra>()->state_generation++;
    assert(h.owner.receive(s, 11));
    assert(!h.owner.release_agenda_page(1, 12) && h.sink.pages == 0);
  }
  {
    Harness h;
    assert(h.owner.begin_agenda_page(1, 1, 10));
    auto s = h.grant;
    s.kind = ControlKind::CONTROL_KIND_STATE;
    s.state.state_seq = 2;
    assert(h.owner.receive(s, 11));
    assert(h.owner.release_agenda_page(1, 12) && h.sink.pages == 1);
  }
  {
    Harness h;
    auto s = h.grant;
    s.kind = ControlKind::CONTROL_KIND_STATE;
    s.state.state_seq = 2;
    std::get<Agenda>(s.state.family_if<Extra>()->data).summary.assign("Unannounced semantic change");
    assert(!h.owner.receive(s, 11) && !h.owner.has_selection());
  }
  {
    Harness h;
    h.page();
    auto s = h.state(), r = h.receipt(s);
    h.owner.tick(13012);
    assert(!h.owner.agenda_page_pending() && h.owner.agenda_page_failed() && h.owner.has_selection());
    assert(!h.owner.receive(r, 13013) && !h.owner.receive(s, 13014));
  }
  {
    Harness h;
    h.page();
    assert(h.owner.disconnect());
    assert(!h.owner.info_agenda_bound() && !h.owner.has_selection() && h.owner.state().family_if<Extra>() == nullptr);
  }
  {
    Harness h;
    assert(h.owner.begin_agenda_page(1, 1, 10));
    assert(h.owner.cancel_drag());
    assert(!h.owner.release_agenda_page(1, 11) && h.sink.pages == 0);
  }
  {
    Harness h;
    auto s = h.grant;
    s.kind = ControlKind::CONTROL_KIND_STATE;
    s.state.state_seq = 2;
    auto &a = std::get<Agenda>(s.state.family_if<Extra>()->data);
    a.index = 1;
    a.request_seq = 1;
    assert(!h.owner.receive(s, 10));
    assert(h.owner.has_selection());
  }
  {
    Harness h;
    h.page();
    auto s = h.state(), r = h.receipt(s);
    r.command_seq++;
    assert(!h.owner.receive(r, 12));
    r.command_seq--;
    r.value = 15;
    assert(!h.owner.receive(r, 13));
    r.value = h.sink.sent.value;
    r.enabled = false;
    r.reason = ControlReason::CONTROL_REASON_BUSY;
    assert(h.owner.receive(r, 14));
    assert(!h.owner.agenda_page_pending() && h.owner.agenda_page_failed());
    assert(h.owner.can_agenda_page(1));
    assert(!h.owner.receive(r, 15));
  }
  {
    Harness h;
    h.page();
    auto s = h.state(), r = h.receipt(s);
    r.kind = ControlKind::CONTROL_KIND_RESULT;
    r.result = ControlResult::CONTROL_RESULT_STATE_MATCHES;
    assert(!h.owner.receive(r, 12));
    assert(h.owner.agenda_page_pending() && !h.owner.is_command_pending());
  }
  {
    Sink sink;
    MaterialEntityControl owner(sink);
    ControlText<32> c;
    c.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    assert(owner.set_enabled(true) && owner.set_extra_profiles(1) && owner.bind_generic(c) &&
           owner.accept_extra_features(c, 1));
    assert(!owner.accept_info_agenda(c));
    auto g = initial(codec);
    assert(owner.request_open(g.identity.context, 1));
    g.identity = owner.identity();
    g.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    assert(!owner.receive(g, 2));
  }
  if (serialized)
    std::fclose(serialized);
  printf("PASS rows=%u negatives=%u owner=13 arena=%zu snapshot=%zu message=%zu owner_size=%zu agenda=%zu\n", rows,
         negative, codec.high_water(), sizeof(ControlSnapshot), sizeof(ControlMessage), sizeof(MaterialEntityControl),
         sizeof(Agenda));
}
#else
int main() {
  MaterialEntityControlCodec codec;
  for (const auto &row : WIRES) {
    ControlMessage message;
    assert(!codec.parse_entity(row.raw, message));
  }
  std::puts("PASS Agenda macro disabled; all Agenda wire rejected");
}
#endif

#include "material_entity_control_codec.h"
#include "media_owner_fixtures.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace esphome::lvgl_material;
using Media = ControlMediaSnapshot;
using Facet = ControlMediaFacet;
using Op = ControlOperation;
struct Sink : MaterialEntityControlSink {
  ControlMessage last;
  unsigned commands{0};
  bool emit(const ControlMessage &message) override {
    last = message;
    commands += message.kind == ControlKind::CONTROL_KIND_COMMAND;
    return true;
  }
  void view_event(ControlViewEvent) override {}
};
struct Harness {
  Sink sink;
  MaterialEntityControl owner{sink};
  MaterialEntityControlCodec codec;
  ControlText<32> channel;
  Harness(uint8_t profiles = 4) {
    channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    assert(owner.set_enabled(true) && owner.set_extra_profiles(7) && owner.bind_generic(channel));
    if (profiles)
      assert(owner.accept_extra_features(channel, profiles));
  }
  ControlMessage parse(const char *wire) {
    ControlMessage message;
    assert(codec.parse_entity(wire, message));
    return message;
  }
  void open(const char *wire = MUSIC_MAIN) {
    auto message = parse(wire);
    assert(owner.request_open(message.identity.context, 1));
    message.kind = ControlKind::CONTROL_KIND_GRANT;
    message.identity = owner.identity();
    message.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    assert(owner.receive(message, 2));
    assert(owner.set_view_ready(true, 3));
    assert(owner.is_media() && !owner.is_extra() && !owner.is_panel() && owner.media_view_ready());
    assert(!owner.accept_extra_features(channel, 4));
  }
  ControlMessage state(const char *wire, uint32_t seq = 2) {
    auto message = parse(wire);
    message.identity = owner.identity();
    message.state.state_seq = seq;
    return message;
  }
  void facet(Facet target, const char *wire) {
    assert(owner.request_media_facet(target, 4));
    assert(sink.last.kind == ControlKind::CONTROL_KIND_FACET && sink.commands == 0);
    const auto view = sink.last.state.family_if<Media>()->view_seq;
    ControlMessage result;
    result.generic = true;
    result.kind = ControlKind::CONTROL_KIND_FACET_RESULT;
    result.identity = owner.identity();
    result.state.state_seq = 2;
    auto &payload = result.state.emplace_family<Media>();
    payload.facet = target;
    payload.view_seq = view;
    payload.editable = true;
    assert(!owner.receive(result, 5));
    auto observed = state(wire);
    observed.state.family_if<Media>()->view_seq = view;
    assert(owner.receive(observed, 6));
    assert(owner.is_media_view_pending() && !owner.media_view_ready());
    assert(owner.receive(result, 7));
    assert(!owner.is_media_view_pending() && owner.media_view_ready());
    if (target == Facet::VOLUME) {
      assert(!owner.can_media(Op::CONTROL_OPERATION_MEDIA_VOLUME, 50));
      assert(owner.set_scalar_ready(true));
    }
  }
  void wire_roundtrip() {
    char bytes[2049];
    size_t length = 0;
    assert(codec.serialize_entity(sink.last, bytes, sizeof(bytes), length));
    ControlMessage parsed;
    assert(codec.parse_entity({bytes, length}, parsed));
    assert(parsed.operation == sink.last.operation && parsed.identity == sink.last.identity &&
           parsed.expected_state_generation == sink.last.expected_state_generation);
  }
};
int main() {
  std::setbuf(stdout, nullptr);
  unsigned cases = 0;
  {
    Harness h;
    ControlText<32> parsed;
    uint16_t mask = 0;
    const char *offer =
        R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"media","version":1}]})";
    assert(h.codec.parse_extra_features(offer, parsed, mask, 7, true) && mask == 4 && parsed == h.channel);
    assert(!h.codec.parse_extra_features(offer, parsed, mask, 3, true));
    assert(!h.owner.set_extra_profiles(8));
    cases++;
  }
  {
    Harness h(0);
    auto message = h.parse(MUSIC_MAIN);
    assert(!h.owner.request_open(message.identity.context, 1));
    cases++;
  }
  {
    Harness h(1);
    auto message = h.parse(MUSIC_MAIN);
    assert(h.owner.request_open(message.identity.context, 1));
    message.kind = ControlKind::CONTROL_KIND_GRANT;
    message.identity = h.owner.identity();
    message.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    assert(!h.owner.receive(message, 2) && !h.owner.has_selection());
    cases++;
  }
  {
    Harness h;
    h.open();
    assert(h.owner.can_media(Op::CONTROL_OPERATION_MEDIA_PAUSE));
    assert(!h.owner.can_media(Op::CONTROL_OPERATION_MEDIA_PLAY));
    assert(h.owner.begin_media(5, Op::CONTROL_OPERATION_MEDIA_PAUSE, 0, 4));
    auto message = h.state(MUSIC_MAIN);
    message.state.family_if<Media>()->data.emplace<Media::Main>() =
        std::get<Media::Main>(h.owner.state().family_if<Media>()->data);
    std::get<Media::Main>(message.state.family_if<Media>()->data).title.assign("Metadata renewal");
    assert(h.owner.receive(message, 5));
    assert(h.owner.release_media(5, 0, 6) && h.sink.commands == 1);
    assert(h.sink.last.expected_state_generation == 1);
    h.wire_roundtrip();
    cases++;
  }
  {
    Harness h;
    h.open();
    assert(h.owner.begin_media(5, Op::CONTROL_OPERATION_MEDIA_PAUSE, 0, 4));
    auto message = h.state(MUSIC_MAIN);
    message.state.family_if<Media>()->state_generation++;
    assert(h.owner.receive(message, 5));
    assert(!h.owner.release_media(5, 0, 6) && h.sink.commands == 0);
    cases++;
  }
  {
    Harness h;
    h.open();
    auto message = h.state(MUSIC_MAIN);
    auto &media = *message.state.family_if<Media>();
    std::get<Media::Main>(media.data).phase = 4;
    media.eligible = control_media_main_eligible(media);
    assert(!h.owner.receive(message, 5) && !h.owner.has_selection());
    cases++;
  }
  {
    Harness h;
    h.open();
    auto message = h.state(MUSIC_MAIN);
    message.state.family_if<Media>()->device_type = 2;
    assert(!h.owner.receive(message, 5) && !h.owner.has_selection());
    cases++;
  }
  {
    Harness h;
    h.open(MAIN_SUBNORMAL);
    h.facet(Facet::VOLUME, VOLUME_SUBNORMAL);
    assert(h.owner.begin_media(2, Op::CONTROL_OPERATION_MEDIA_VOLUME, 0, 8));
    assert(h.owner.release_media(2, 50, 9));
    assert(h.sink.last.state.family_if<Media>()->data.index() == 2);
    assert(std::get<Media::Volume>(h.sink.last.state.family_if<Media>()->data).raw.view() == "5e-324");
    assert(h.sink.last.value == 50);
    h.wire_roundtrip();
    cases++;
  }
  {
    Harness h;
    h.open();
    h.facet(Facet::VOLUME, MUSIC_VOLUME);
    assert(h.owner.begin_media(2, Op::CONTROL_OPERATION_MEDIA_VOLUME, 100, 8));
    assert(!h.owner.release_media(2, 100, 9) && h.sink.commands == 0);
    assert(h.owner.begin_media(3, Op::CONTROL_OPERATION_MEDIA_VOLUME, 100, 10));
    assert(h.owner.release_media(3, 99, 11));
    cases++;
  }
  {
    Harness h;
    h.open(MAIN_FRACTION);
    h.facet(Facet::VOLUME, VOLUME_FRACTION);
    assert(h.owner.begin_media(2, Op::CONTROL_OPERATION_MEDIA_VOLUME, 33, 8));
    assert(h.owner.release_media(2, 33, 9));  // Exact source fraction is not the UI's33%.
    h.wire_roundtrip();
    cases++;
  }
  {
    Harness h;
    h.open(TV_MAIN);
    assert(!h.owner.can_request_media_facet(Facet::VOLUME));
    assert(!h.owner.can_media(Op::CONTROL_OPERATION_MEDIA_VOLUME_UP));
    h.facet(Facet::SOURCE, TV_SOURCE);
    const auto *choice = std::get_if<Media::Choice>(&h.owner.state().family_if<Media>()->data);
    assert(choice != nullptr && choice->count >= 2);
    const auto target = (choice->index + 1) % choice->count;
    assert(h.owner.begin_media(2, Op::CONTROL_OPERATION_MEDIA_CHOICE, target, 8));
    assert(h.owner.release_media(2, target, 9));
    assert(h.sink.last.expected_index == choice->index);
    h.wire_roundtrip();
    cases++;
  }
  {
    Harness h;
    h.open();
    auto message = h.state(MUSIC_VOLUME);
    assert(!h.owner.receive(message, 5));
    assert(h.owner.state().family_if<Media>()->facet == Facet::MAIN);
    cases++;
  }
  {
    Harness h;
    h.open();
    assert(h.owner.begin_media(2, Op::CONTROL_OPERATION_MEDIA_PAUSE, 0, 8));
    assert(!h.owner.release_media(2, 0, 30002) && !h.owner.has_selection() && h.sink.commands == 0);
    cases++;
  }
  for (bool service_only : {false, true}) {
    Harness h;
    h.open();
    const auto operation = service_only ? Op::CONTROL_OPERATION_MEDIA_NEXT : Op::CONTROL_OPERATION_MEDIA_PAUSE;
    assert(h.owner.begin_media(2, operation, 0, 8) && h.owner.release_media(2, 0, 9));
    ControlMessage result;
    result.generic = true;
    result.kind = ControlKind::CONTROL_KIND_RESULT;
    result.identity = h.owner.identity();
    result.command_seq = 1;
    result.state.state_seq = 1;
    result.result =
        service_only ? ControlResult::CONTROL_RESULT_STATE_MATCHES : ControlResult::CONTROL_RESULT_SERVICE_COMPLETED;
    assert(!h.owner.receive(result, 10));
    result.result =
        service_only ? ControlResult::CONTROL_RESULT_SERVICE_COMPLETED : ControlResult::CONTROL_RESULT_STATE_MATCHES;
    assert(h.owner.receive(result, 11));
    cases++;
  }
  {
    Harness h;
    h.open();
    assert(h.owner.request_media_facet(Facet::VOLUME, 4));
    h.owner.tick(10004);
    assert(!h.owner.media_view_ready() && !h.owner.is_media_view_pending());
    assert(h.owner.begin_media_navigation(2, 10005) && h.owner.release_media_navigation(2, 10006));
    assert(h.owner.can_request_media_facet(Facet::MAIN));
    cases++;
  }
  {
    Harness h;
    h.open(TV_MAIN);
    h.facet(Facet::SOURCE, TV_SOURCE);
    auto message = h.state(TV_SOURCE, 3);
    auto &media = *message.state.family_if<Media>();
    media.view_seq = h.owner.state().family_if<Media>()->view_seq;
    std::get<Media::Choice>(media.data).options[0].assign("Changed catalog");
    assert(!h.owner.receive(message, 8) && !h.owner.has_selection());
    cases++;
  }
  {
    Harness h;
    h.open();
    h.facet(Facet::VOLUME, MUSIC_VOLUME);
    assert(h.owner.begin_media(2, Op::CONTROL_OPERATION_MEDIA_VOLUME, 100, 8));
    auto message = h.state(MUSIC_VOLUME, 3);
    auto &media = *message.state.family_if<Media>();
    media.view_seq = h.owner.state().family_if<Media>()->view_seq;
    media.state_generation++;
    auto &volume = std::get<Media::Volume>(media.data);
    volume.raw.assign("0.5");
    volume.percent.assign("50");
    volume.rail = 5000;
    assert(h.owner.receive(message, 9));
    assert(!h.owner.release_media(2, 75, 10) && h.sink.commands == 0);
    cases++;
  }
  printf("PASS media owner scenarios=%u owner_bytes=%zu snapshot_bytes=%zu message_bytes=%zu\n", cases,
         sizeof(MaterialEntityControl), sizeof(ControlSnapshot), sizeof(ControlMessage));
}

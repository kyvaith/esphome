#include "material_entity_control_codec.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace esphome::lvgl_material;

struct Sink : MaterialEntityControlSink {
  std::array<ControlMessage, 32> messages;
  size_t count{0};
  bool emit(const ControlMessage &message) override {
    assert(this->count < this->messages.size());
    this->messages[this->count++] = message;
    return true;
  }
  void view_event(ControlViewEvent) override {}
};

struct Fixture {
  Sink sink;
  MaterialEntityControl owner{sink};
  ControlMessage grant;
  Fixture() {
    ControlText<32> channel;
    assert(channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    assert(this->owner.set_enabled(true));
    assert(this->owner.bind_generic(channel));
    ControlContext context;
    context.card_id.assign("card-2");
    context.slot = 2;
    context.revision = 4;
    context.control_kind = ControlEntityKind::CONTROL_ENTITY_NUMBER;
    assert(this->owner.request_open(context, 100));
    this->grant.generic = true;
    this->grant.kind = ControlKind::CONTROL_KIND_GRANT;
    this->grant.identity = this->owner.identity();
    this->grant.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    this->grant.state.state_seq = 1;
    this->grant.state.title.assign("Number");
    auto &number = this->grant.state.number;
    number.domain.minimum = -1000000;
    number.domain.maximum = 1000000;
    number.domain.step = 250000;
    number.domain.max_index = 8;
    number.value_text.assign("0");
    number.available = true;
    number.index = 4;
    number.state_generation = 1;
    assert(this->owner.receive(this->grant, 101));
  }
  void ready() { assert(this->owner.set_view_ready(true, 102)); }
  ControlMessage state(uint32_t sequence, uint32_t generation, uint32_t index) {
    auto message = this->grant;
    message.kind = ControlKind::CONTROL_KIND_STATE;
    message.state.state_seq = sequence;
    message.state.number.index = index;
    message.state.number.state_generation = generation;
    int64_t value;
    char text[24];
    assert(message.state.number.domain.value_at(index, value));
    assert(number_format_decimal(value, text, sizeof(text)));
    message.state.number.value_text.assign(text);
    return message;
  }
  ControlMessage result(uint32_t state_seq, ControlResult status = ControlResult::CONTROL_RESULT_STATE_MATCHES) {
    ControlMessage message;
    message.generic = true;
    message.kind = ControlKind::CONTROL_KIND_RESULT;
    message.identity = this->owner.identity();
    message.command_seq = this->owner.command_sequence();
    message.state.state_seq = state_seq;
    message.result = status;
    return message;
  }
};

static void run(int scenario) {
  if (scenario == 0) {
    for (const auto text :
         {"0", "-0.000001", "0.000001", "-1.25", "1000000000000", "-1000000000000", "999999999999.999999"}) {
      int64_t value;
      char output[24];
      assert(number_parse_decimal(text, value));
      assert(number_format_decimal(value, output, sizeof(output)));
      assert(std::string_view(output) == text);
    }
    for (const auto text : {"", "-", "+1", "01", "-0", "-0.0", "1.0", "1.", ".1", "1e2", "0.0000001",
                            "1000000000000.000001", "99999999999999999999"}) {
      int64_t unchanged = 123;
      assert(!number_parse_decimal(text, unchanged));
      assert(unchanged == 123);
    }
    char unchanged[2] = {'x', 0};
    assert(!number_format_decimal(1000000, unchanged, 1));
    assert(unchanged[0] == 'x');
    return;
  }
  if (scenario == 1) {
    MaterialNumberDomain domain;
    domain.minimum = -NUMBER_MAX_MICRO;
    domain.maximum = NUMBER_MAX_MICRO;
    domain.step = NUMBER_MAX_MICRO;
    domain.max_index = 2;
    int64_t value;
    assert(domain.valid() && domain.value_at(2, value) && value == NUMBER_MAX_MICRO);
    domain.minimum = 0;
    domain.maximum = 1000001;
    domain.step = 3;
    domain.max_index = 333333;
    assert(domain.valid() && domain.value_at(domain.max_index, value) && value == 999999);
    assert(!domain.value_at(domain.max_index + 1, value));
    domain.maximum = 1000000;
    domain.step = 1;
    domain.max_index = 1000000;
    assert(domain.valid());
    domain.maximum++;
    assert(!domain.valid());
    return;
  }
  Fixture fixture;
  auto &owner = fixture.owner;
  assert(owner.is_number() && !owner.is_scalar_ready());
  if (scenario == 2) {
    assert(!owner.begin_step(7, 1, 102));
    fixture.ready();
    assert(!owner.begin_drag(7, 103));
    assert(!owner.command(ControlOperation::CONTROL_OPERATION_TURN_OFF, 103));
    assert(owner.begin_step(7, 1, 103));
    assert(owner.release_step(7, 104));
    assert(fixture.sink.count == 2);
    const auto &command = fixture.sink.messages[1];
    assert(command.generic && command.operation == ControlOperation::CONTROL_OPERATION_STEP && command.direction == 1);
    assert(command.expected_index == 4 && command.expected_state_generation == 1);
    assert(!owner.release_step(7, 105) && !owner.begin_step(7, 1, 105));
    return;
  }
  fixture.ready();
  if (scenario == 3) {
    assert(owner.begin_step(7, -1, 103));
    auto state = fixture.state(2, 1, 4);
    state.state.title.assign("Renamed during unchanged lease renewal");
    assert(owner.receive(state, 104) && owner.is_dragging());
    assert(owner.release_step(7, 105));
    assert(fixture.sink.messages[1].expected_state_generation == 1);
    return;
  }
  if (scenario == 4 || scenario == 5) {
    assert(owner.begin_step(7, 1, 103));
    auto state = fixture.state(2, scenario == 4 ? 2 : 3, scenario == 4 ? 5 : 4);
    assert(owner.receive(state, 104));
    assert(!owner.is_dragging() && !owner.release_step(7, 105) && fixture.sink.count == 1);
    return;
  }
  if (scenario == 6) {
    assert(owner.begin_step(7, 1, 103));
    auto state = fixture.state(2, 1, 4);
    state.state.number.domain.maximum = 2000000;
    state.state.number.domain.max_index = 12;
    assert(!owner.receive(state, 104) && !owner.has_selection());
    assert(fixture.sink.messages[1].reason == ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED);
    assert(!owner.release_step(7, 105));
    return;
  }
  if (scenario == 7) {
    assert(owner.begin_step(7, 1, 103));
    assert(!owner.receive(fixture.state(2, 1, 5), 104));
    assert(!owner.has_selection() && !owner.release_step(7, 105));
    return;
  }
  if (scenario == 8) {
    assert(owner.receive(fixture.state(2, 2, 0), 103));
    assert(!owner.can_step(-1) && owner.can_step(1));
    assert(owner.receive(fixture.state(3, 3, 8), 104));
    assert(owner.can_step(-1) && !owner.can_step(1));
    auto unknown = fixture.state(4, 4, 0);
    unknown.state.number.available = false;
    unknown.state.number.value_text.assign("--");
    assert(owner.receive(unknown, 105));
    assert(!owner.begin_step(7, -1, 106) && !owner.begin_step(7, 1, 106));
    return;
  }
  if (scenario == 9) {
    assert(owner.begin_step(7, 1, 103));
    assert(owner.release_step(7, 104));
    assert(!owner.receive(fixture.result(2), 105));
    assert(owner.is_command_pending() && !owner.begin_step(7, 1, 106));
    assert(owner.receive(fixture.state(2, 2, 5), 107));
    assert(owner.is_command_pending());
    assert(owner.receive(fixture.result(2), 108));
    assert(!owner.is_command_pending() && owner.state().number.index == 5 && owner.begin_step(7, 1, 109));
    return;
  }
  if (scenario == 10) {
    assert(owner.begin_step(7, 1, 103) && owner.release_step(7, 104));
    owner.tick(13104);
    assert(!owner.has_selection() && !owner.is_view_ready() && !owner.begin_step(7, 1, 13105));
    assert(owner.last_result() == ControlResult::CONTROL_RESULT_NOT_CONFIRMED && fixture.sink.count == 3);
    return;
  }
  if (scenario == 11) {
    assert(owner.begin_step(7, 1, 103));
    assert(owner.invalidate_admission(ControlReason::CONTROL_REASON_NAVIGATION));
    assert(!owner.release_step(7, 104));
    assert(!owner.receive(fixture.grant, 105));
    return;
  }
  if (scenario == 12) {
    auto wrong = fixture.state(2, 2, 5);
    wrong.identity.context.control_kind = ControlEntityKind::CONTROL_ENTITY_LIGHT;
    assert(!owner.receive(wrong, 103));
    wrong = fixture.state(2, 2, 5);
    wrong.generic = false;
    assert(!owner.receive(wrong, 104));
    assert(owner.state().state_seq == 1);
    ControlText<32> channel;
    channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    assert(owner.bind(channel) && !owner.is_generic() && !owner.has_selection());
    assert(!owner.request_open(fixture.grant.identity.context, 105));
    return;
  }
  if (scenario == 13) {
    assert(owner.begin_step(7, 1, 103));
    assert(!owner.release_step(8, 104) && owner.is_dragging());
    assert(owner.cancel_drag() && !owner.release_step(7, 105));
    assert(fixture.sink.count == 1);
    return;
  }
  if (scenario == 14) {
    assert(owner.begin_step(7, 1, 103) && owner.release_step(7, 104));
    auto rejected = fixture.result(1, ControlResult::CONTROL_RESULT_REJECTED);
    rejected.reason = ControlReason::CONTROL_REASON_STALE_STATE;
    assert(owner.receive(rejected, 105));
    assert(!owner.is_command_pending() && owner.last_reason() == ControlReason::CONTROL_REASON_STALE_STATE);
    assert(!control_valid_result(rejected.result, rejected.reason));
    return;
  }
  if (scenario == 15) {
    assert(!owner.set_scalar_ready(true));
    assert(owner.begin_step(7, 1, 103));
    assert(!owner.update_candidate(7, 0, 104));
    assert(!owner.release(7, 105));
    assert(owner.release_step(7, 106));
    assert(fixture.sink.count == 2 && fixture.sink.messages[1].operation == ControlOperation::CONTROL_OPERATION_STEP);
    return;
  }
  if (scenario == 16) {
    auto state = fixture.state(2, 3, 4);
    assert(owner.receive(state, 103));
    assert(!owner.receive(fixture.state(3, 2, 5), 104));
    assert(owner.state().state_seq == 2 && owner.state().number.index == 4);
    assert(owner.begin_step(7, 1, 105));
    assert(!owner.receive(fixture.state(2, 4, 5), 106));
    assert(owner.is_dragging() && owner.release_step(7, 107));
    return;
  }
  if (scenario == 17) {
    assert(owner.begin_step(7, 1, 103) && owner.release_step(7, 104));
    assert(owner.receive(fixture.state(2, 2, 5), 105));
    auto old_token = fixture.result(2);
    old_token.identity.token.assign("cccccccccccccccccccccccccccccccc");
    assert(!owner.receive(old_token, 106) && owner.is_command_pending());
    auto wrong_command = fixture.result(2);
    wrong_command.command_seq++;
    assert(!owner.receive(wrong_command, 107) && owner.is_command_pending());
    assert(owner.receive(fixture.result(2), 108));
    return;
  }
  assert(false);
}

int main(int argc, char **argv) {
  if (argc == 3 && std::string_view(argv[1]) == "codec") {
    std::ifstream stream(argv[2], std::ios::binary);
    std::string payload{std::istreambuf_iterator<char>(stream), {}};
    MaterialEntityControlCodec codec;
    ControlMessage message;
    message.command_seq = 99;
    const auto unchanged = message;
    if (!codec.parse_entity(payload, message)) {
      assert(message.command_seq == unchanged.command_seq && message.identity == unchanged.identity);
      std::cerr << "refused arena_high_water=" << codec.high_water() << '\n';
      return 1;
    }
    char output[2049];
    size_t length = 0;
    assert(codec.serialize_entity(message, output, sizeof(output), length));
    ControlMessage roundtrip;
    assert(codec.parse_entity({output, length}, roundtrip));
    assert(roundtrip.kind == message.kind && roundtrip.identity == message.identity &&
           roundtrip.state == message.state);
    std::cout.write(output, length);
    std::cerr << "arena_high_water=" << codec.high_water() << '\n';
    return 0;
  }
  assert(argc == 2);
  int scenario = std::stoi(argv[1]);
  run(scenario);
  std::cout << "number case " << scenario << " PASS\n";
}

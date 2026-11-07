"""Fan session generations, absolute intent and bounded production wire codec."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command


class FanOwnerHostTests(unittest.TestCase):
    def test_generation_and_strict_wire(self):
        sources = [
            "material_entity_control.cpp",
            "material_entity_control_protocol.cpp",
            "material_entity_control_codec.cpp",
            "material_number_domain.cpp",
            "material_entity_control_generic.cpp",
            "material_entity_control_generic_protocol.cpp",
            "material_entity_control_fan.cpp",
        ]
        for siblings in (False, True):
            with (
                self.subTest(siblings=siblings),
                tempfile.TemporaryDirectory(prefix="fan-owner-") as temporary,
            ):
                root = Path(temporary)
                defines = root / "esphome/core/defines.h"
                defines.parent.mkdir(parents=True)
                defines.write_text("")
                harness = root / "fan.cpp"
                harness.write_text(HARNESS, encoding="utf8")
                executable = root / "fan.exe"
                optional = (
                    [
                        "-DUSE_LVGL_MATERIAL_CHOICE_SELECTOR",
                        "-DUSE_LVGL_MATERIAL_COVER_CONTROLS",
                        "-DUSE_LVGL_MATERIAL_THERMOSTAT",
                        str(SOURCE / "material_entity_control_choice.cpp"),
                        str(SOURCE / "material_entity_control_cover.cpp"),
                    ]
                    if siblings
                    else []
                )
                command = [
                    *compiler_command(),
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DARDUINOJSON_POOL_CAPACITY=128",
                    "-DUSE_LVGL_MATERIAL_NUMBER_STEPPER",
                    "-DUSE_LVGL_MATERIAL_FAN_CONTROLS",
                    *optional,
                    "-I",
                    str(root),
                    "-I",
                    str(SOURCE),
                    "-I",
                    str(arduino_json()),
                    *[str(SOURCE / name) for name in sources],
                    str(harness),
                    "-o",
                    str(executable),
                ]
                result = subprocess.run(
                    command, capture_output=True, text=True, timeout=120, check=False
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run(
                    [str(executable)],
                    capture_output=True,
                    text=True,
                    timeout=20,
                    check=False,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


HARNESS = r"""
#include "material_entity_control.h"
#include "material_entity_control_codec.h"
#include <cassert>
#include <string>
using namespace esphome::lvgl_material;
struct Sink : MaterialEntityControlSink {
  ControlMessage last; unsigned count{0};
  bool emit(const ControlMessage &message) override { last=message;count++;return true; }
  void view_event(ControlViewEvent) override {}
};
void preset_to_manual() {
  constexpr auto unknown = ControlSnapshot::Fan::UNKNOWN;
  constexpr auto speed = ControlOperation::CONTROL_OPERATION_FAN_SPEED;
  for (uint8_t power = 0; power <= 1; power++) {
    for (uint8_t target = 0; target < 4; target++) {
      Sink sink;
      MaterialEntityControl owner(sink);
      ControlText<32> channel;
      channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
      assert(owner.set_enabled(true));
      assert(owner.set_fan_enabled(true));
      assert(owner.bind_generic(channel));
      ControlContext context;
      context.slot = 31;
      context.revision = 348;
      context.card_id.assign("fan-31");
      context.control_kind = ControlEntityKind::CONTROL_ENTITY_FAN;
      assert(owner.request_open(context, 1));
      ControlMessage state;
      state.generic = true;
      state.kind = ControlKind::CONTROL_KIND_GRANT;
      state.identity = sink.last.identity;
      state.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
      state.state.title.assign("Fan");
      state.state.state_seq = 1;
      auto &fan = state.state.emplace_family<ControlSnapshot::Fan>();
      fan.caps.turn_on = fan.caps.turn_off = true;
      fan.caps.speeds = {0, 33, 66, 100};
      fan.caps.speed_count = 4;
      fan.caps.presets[0].assign("Eco");
      fan.caps.preset_count = 1;
      fan.available = fan.preset_known = true;
      fan.power = power;
      fan.percentage = unknown;
      fan.preset_index = 0;
      fan.state_generation = 1;
      assert(control_valid_fan_snapshot(state.state));
      assert(owner.receive(state, 2));
      assert(!owner.can_fan(speed, target));  // No input before view admission.
      assert(owner.set_view_ready(true, 3));
      assert(owner.can_fan(speed, target));
      assert(!owner.can_fan(speed, fan.caps.speed_count));
      auto before = sink.count;
      assert(owner.begin_fan(11, speed, target, 4));
      assert(sink.count == before);
      assert(!owner.release_fan(12, 5));  // A different pointer cannot release it.
      assert(owner.release_fan(11, 6));
      assert(!owner.release_fan(11, 7));
      assert(sink.count == before + 1);
      assert(sink.last.operation == speed && sink.last.fan_target_index == target);
      assert(sink.last.expected_state_generation == 1);
      assert(owner.state().family_if<ControlSnapshot::Fan>()->percentage == unknown);
      assert(!owner.can_fan(speed, target));  // Command pending, not optimistic state.
      ControlMessage result;
      result.generic = true;
      result.kind = ControlKind::CONTROL_KIND_RESULT;
      result.identity = state.identity;
      result.command_seq = owner.command_sequence();
      result.state.state_seq = state.state.state_seq;
      result.result = ControlResult::CONTROL_RESULT_STATE_MATCHES;
      assert(owner.receive(result, 8));
      state.kind = ControlKind::CONTROL_KIND_STATE;
      // Explicit Manual/null is known-empty; missing/unrecognized is unknown.
      state.state.state_seq++;
      fan.state_generation++;
      fan.preset_index = unknown;
      assert(owner.receive(state, 9));
      assert(!owner.can_fan(speed, target));
      state.state.state_seq++;
      fan.state_generation++;
      fan.preset_known = false;
      assert(owner.receive(state, 10));
      assert(!owner.can_fan(speed, target));
      // An unavailable observation cannot enable manual control.
      state.state.state_seq++;
      fan.state_generation++;
      fan.available = false;
      fan.power = unknown;
      assert(owner.receive(state, 11));
      assert(!owner.can_fan(speed, target));
      state.state.state_seq++;
      fan.state_generation++;
      fan.available = fan.preset_known = true;
      fan.power = power;
      fan.preset_index = 0;
      assert(owner.receive(state, 12));
      assert(owner.begin_fan(13, speed, target, 13));
      before = sink.count;
      // A -> B -> A still cancels the held release by authoritative generation.
      state.state.state_seq++;
      fan.state_generation++;
      fan.preset_index = unknown;
      assert(owner.receive(state, 14));
      state.state.state_seq++;
      fan.state_generation++;
      fan.preset_index = 0;
      assert(owner.receive(state, 15));
      assert(!owner.release_fan(13, 16));
      assert(sink.count == before);
      // Malformed current preset indices never enter the owner.
      fan.preset_index = fan.caps.preset_count;
      assert(!control_valid_fan_snapshot(state.state));
      fan.preset_index = 0;
      fan.preset_known = false;
      assert(!control_valid_fan_snapshot(state.state));
      fan.preset_known = true;
      assert(owner.begin_fan(14, speed, target, 17));
      state.state.state_seq++;
      fan.caps.speed_count = 0;
      fan.caps.speeds = {};
      assert(!owner.receive(state, 18));
      assert(!owner.has_selection());
      assert(owner.last_reason() == ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED);
      before = sink.count;
      assert(!owner.can_fan(speed, target));
      assert(!owner.release_fan(14, 19));
      assert(sink.count == before);
    }
  }
}
int main() {
  preset_to_manual();
  Sink sink; MaterialEntityControl owner(sink); MaterialEntityControlCodec codec;
  ControlText<32> channel; channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  assert(owner.set_enabled(true));assert(owner.bind_generic(channel));
  ControlContext context;context.slot=31;context.revision=348;context.card_id.assign("fan-31");
  context.control_kind=ControlEntityKind::CONTROL_ENTITY_FAN;
  assert(!owner.request_open(context,1));assert(owner.set_fan_enabled(true));assert(owner.request_open(context,2));
  ControlMessage state;state.generic=true;state.kind=ControlKind::CONTROL_KIND_GRANT;state.identity=sink.last.identity;
  state.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");state.state.title.assign("Fan");state.state.state_seq=1;
  state.state.emplace_family<ControlSnapshot::Fan>();
  auto &fan=(*state.state.family_if<ControlSnapshot::Fan>());fan.caps.turn_on=fan.caps.turn_off=fan.caps.oscillate=fan.caps.direction=true;
  fan.caps.speeds={0,33,66,100};fan.caps.speed_count=4;fan.caps.preset_count=2;
  fan.caps.presets[0].assign("Eco");fan.caps.presets[1].assign("Sleep");
  fan.available=true;fan.power=1;fan.percentage=33;fan.oscillating=0;fan.direction=0;fan.preset_known=true;
  fan.state_generation=1;
  assert(control_valid_fan_snapshot(state.state));assert(owner.receive(state,3));assert(owner.set_view_ready(true,4));
  assert(owner.is_fan() && owner.is_discrete());
  assert(!owner.can_fan(ControlOperation::CONTROL_OPERATION_FAN_POWER,1));
  assert(owner.can_fan(ControlOperation::CONTROL_OPERATION_FAN_POWER,0));
  assert(!owner.can_fan(ControlOperation::CONTROL_OPERATION_FAN_SPEED,1));
  assert(owner.can_fan(ControlOperation::CONTROL_OPERATION_FAN_PRESET,0));
  assert(!owner.command(ControlOperation::CONTROL_OPERATION_TURN_OFF,5));
  char buffer[2049];size_t length;ControlMessage parsed;
  assert(codec.serialize_entity(state,buffer,sizeof(buffer),length));
  std::string wire(buffer,length);assert(codec.parse_entity(wire,parsed));assert(parsed.state==state.state);
  auto bad=wire;bad.insert(bad.find("\"turn_on\":true"),"\"turn_on\":true,");assert(!codec.parse_entity(bad,parsed));
  bad=wire;bad.replace(bad.find("\"percentage\":33"),15,"\"percentage\":true");assert(!codec.parse_entity(bad,parsed));
  bad=wire;bad.replace(bad.find("\"preset_known\":true"),19,"\"preset_known\":null");assert(!codec.parse_entity(bad,parsed));
  assert(owner.begin_fan(1,ControlOperation::CONTROL_OPERATION_FAN_SPEED,2,6));
  state.kind=ControlKind::CONTROL_KIND_STATE;state.state.state_seq++;state.state.title.assign("Renewed");
  assert(owner.receive(state,7));assert(owner.is_dragging());assert(owner.release_fan(1,8));assert(!owner.release_fan(1,9));
  assert(sink.last.fan_target_index==2 && sink.last.expected_state_generation==1);
  assert(codec.serialize_entity(sink.last,buffer,sizeof(buffer),length));wire.assign(buffer,length);
  assert(wire.find("\"op\":\"speed\"")!=std::string::npos);assert(codec.parse_entity(wire,parsed));
  bad=wire;bad.insert(bad.size()-1,",\"value\":66");assert(!codec.parse_entity(bad,parsed));
  ControlMessage result;result.generic=true;result.kind=ControlKind::CONTROL_KIND_RESULT;result.identity=state.identity;
  result.command_seq=owner.command_sequence();result.state.state_seq=state.state.state_seq+1;
  result.result=ControlResult::CONTROL_RESULT_STATE_MATCHES;assert(!owner.receive(result,10));
  result.state.state_seq=state.state.state_seq;assert(owner.receive(result,10));
  assert(owner.begin_fan(2,ControlOperation::CONTROL_OPERATION_FAN_OSCILLATION,1,11));
  state.state.state_seq++;fan.state_generation+=2;assert(owner.receive(state,12));
  auto before=sink.count;assert(!owner.release_fan(2,13));assert(sink.count==before);
  // A noncanonical observation remains exact; selecting a catalog speed is still valid.
  state.state.state_seq++;fan.state_generation++;fan.percentage=67;assert(owner.receive(state,14));
  assert(owner.can_fan(ControlOperation::CONTROL_OPERATION_FAN_SPEED,1));
  assert(owner.begin_fan(4,ControlOperation::CONTROL_OPERATION_FAN_SPEED,1,14));
  assert(owner.release_fan(4,14));assert(sink.last.fan_target_index==1);
  result.command_seq=owner.command_sequence();result.state.state_seq=state.state.state_seq;assert(owner.receive(result,14));
  state.state.state_seq++;fan.state_generation++;fan.percentage=ControlSnapshot::Fan::UNKNOWN;
  assert(owner.receive(state,14));assert(!owner.can_fan(ControlOperation::CONTROL_OPERATION_FAN_SPEED,1));
  assert(owner.can_fan(ControlOperation::CONTROL_OPERATION_FAN_POWER,0));
  // Manual preset is known-empty; unknown preset is not manual.
  state.state.state_seq++;fan.state_generation++;fan.preset_known=false;assert(owner.receive(state,15));
  assert(!owner.can_fan(ControlOperation::CONTROL_OPERATION_FAN_PRESET,0));
  // Local picker navigation owns a generation, so a stale release cannot open it.
  assert(owner.begin_fan_navigation(3,16));state.state.state_seq++;fan.state_generation++;
  assert(owner.receive(state,17));assert(!owner.release_fan_navigation(3,18));
  // Unsupported fields remain empty and unknown; capability changes retire ownership.
  state.state.state_seq++;fan.caps.direction=false;fan.direction=ControlSnapshot::Fan::UNKNOWN;
  assert(!owner.receive(state,19));assert(!owner.has_selection());
  assert(owner.last_reason()==ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED);
  // Full eight-option catalog fits the bounded fan arena and unchanged wire budget.
  fan.caps.preset_count=8;fan.preset_known=true;fan.caps.speed_count=8;fan.caps.speeds={0,14,28,42,57,71,85,100};
  state.state.title.assign(std::string(160,'T'));
  for(unsigned i=0;i<8;i++) fan.caps.presets[i].assign(std::string(32,'a'+i));
  assert(codec.serialize_entity(state,buffer,sizeof(buffer),length));
  assert(codec.parse_entity({buffer,length},parsed));
  assert(length<=2048 && codec.high_water()<=5120);
  fan.available=false;fan.power=ControlSnapshot::Fan::UNKNOWN;
  fan.percentage=fan.preset_index=fan.oscillating=fan.direction=ControlSnapshot::Fan::UNKNOWN;fan.preset_known=false;
  assert(control_valid_fan_snapshot(state.state));
  assert(codec.serialize_entity(state,buffer,sizeof(buffer),length));assert(codec.parse_entity({buffer,length},parsed));
  fan.percentage=50;assert(!control_valid_fan_snapshot(state.state));
  ControlMessage cap;cap.generic=true;cap.kind=ControlKind::CONTROL_KIND_CAPABILITY;cap.enabled=true;cap.fan_enabled=true;
  assert(codec.serialize_entity(cap,buffer,sizeof(buffer),length));assert(codec.parse_entity({buffer,length},parsed));
  assert(parsed.fan_enabled);cap.fan_enabled=false;
  assert(codec.serialize_entity(cap,buffer,sizeof(buffer),length));assert(codec.parse_entity({buffer,length},parsed));
  assert(!parsed.fan_enabled);
}
"""

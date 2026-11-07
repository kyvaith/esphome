"""Cover owner, strict wire and opt-in compatibility using production sources."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command


class CoverOwnerHostTests(unittest.TestCase):
    def test_motion_generation_and_exact_wire(self):
        sources = [
            "material_entity_control.cpp",
            "material_entity_control_protocol.cpp",
            "material_entity_control_codec.cpp",
            "material_number_domain.cpp",
            "material_entity_control_generic.cpp",
            "material_entity_control_generic_protocol.cpp",
            "material_entity_control_cover.cpp",
        ]
        for choice in (False, True):
            with (
                self.subTest(choice=choice),
                tempfile.TemporaryDirectory(prefix="cover-owner-") as temporary,
            ):
                root = Path(temporary)
                defines = root / "esphome/core/defines.h"
                defines.parent.mkdir(parents=True)
                defines.write_text("")
                harness = root / "cover.cpp"
                harness.write_text(HARNESS)
                executable = root / "cover.exe"
                optional = (
                    [
                        "-DUSE_LVGL_MATERIAL_CHOICE_SELECTOR",
                        str(SOURCE / "material_entity_control_choice.cpp"),
                    ]
                    if choice
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
                    "-DUSE_LVGL_MATERIAL_COVER_CONTROLS",
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
  ControlMessage last;
  unsigned count{0};
  bool emit(const ControlMessage &message) override { last=message;count++;return true; }
  void view_event(ControlViewEvent) override {}
};
int main() {
  Sink sink; MaterialEntityControl owner(sink); MaterialEntityControlCodec codec;
  ControlText<32> channel; channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  assert(owner.set_enabled(true));assert(owner.bind_generic(channel));
  ControlContext context; context.slot=15;context.revision=85;
  context.card_id.assign("cover-15");context.control_kind=ControlEntityKind::CONTROL_ENTITY_COVER;
  assert(!owner.request_open(context,1));assert(owner.set_cover_enabled(true));assert(owner.request_open(context,2));
  ControlMessage state; state.generic=true;state.kind=ControlKind::CONTROL_KIND_GRANT;
  state.identity=sink.last.identity;state.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  state.state.title.assign("Cover");state.state.state_seq=1;
  state.state.emplace_family<ControlCoverSnapshot>();
  auto &cover=(*state.state.family_if<ControlCoverSnapshot>());cover.caps={true,true,true};cover.available=true;
  cover.state=ControlCoverState::CONTROL_COVER_STATE_OPEN;cover.state_generation=1;
  cover.position_known=true;cover.position=40;
  assert(control_valid_cover_snapshot(state.state));assert(owner.receive(state,3));assert(owner.set_view_ready(true,4));
  assert(owner.is_discrete() && owner.is_cover());
  assert(owner.can_cover(ControlOperation::CONTROL_OPERATION_COVER_OPEN));
  assert(owner.can_cover(ControlOperation::CONTROL_OPERATION_COVER_CLOSE));
  assert(!owner.can_cover(ControlOperation::CONTROL_OPERATION_COVER_STOP));
  assert(!owner.command(ControlOperation::CONTROL_OPERATION_TURN_ON,5));
  char buffer[2049];size_t length; ControlMessage parsed;
  assert(codec.serialize_entity(state,buffer,sizeof(buffer),length));
  std::string wire(buffer,length);assert(codec.parse_entity(wire,parsed));assert(parsed.state==state.state);
  auto bad=wire;bad.insert(bad.find("\"open\":true"),"\"open\":true,");assert(!codec.parse_entity(bad,parsed));
  bad=wire;bad.replace(bad.find("\"position\":40"),13,"\"position\":true");assert(!codec.parse_entity(bad,parsed));
  assert(owner.begin_cover(1,ControlOperation::CONTROL_OPERATION_COVER_OPEN,6));
  // Title/lease renewal must preserve a valid held contact.
  state.kind=ControlKind::CONTROL_KIND_STATE;state.state.state_seq++;
  state.state.title.assign("Renewed");assert(owner.receive(state,7));assert(owner.is_dragging());
  assert(owner.release_cover(1,8));assert(!owner.release_cover(1,9));
  assert(sink.last.operation==ControlOperation::CONTROL_OPERATION_COVER_OPEN);
  assert(sink.last.expected_state_generation==1);
  assert(codec.serialize_entity(sink.last,buffer,sizeof(buffer),length));
  wire.assign(buffer,length);assert(wire.find("\"op\":\"open\"")!=std::string::npos);
  assert(codec.parse_entity(wire,parsed));
  bad=wire;bad.insert(bad.size()-1,",\"value\":1");assert(!codec.parse_entity(bad,parsed));
  ControlMessage result;result.generic=true;result.kind=ControlKind::CONTROL_KIND_RESULT;
  result.identity=state.identity;result.command_seq=owner.command_sequence();result.state.state_seq=state.state.state_seq;
  result.result=ControlResult::CONTROL_RESULT_STATE_MATCHES;assert(owner.receive(result,10));
  // Coalesced semantic ABA retires a held contact even with identical final state.
  assert(owner.begin_cover(2,ControlOperation::CONTROL_OPERATION_COVER_CLOSE,11));
  state.state.state_seq++;cover.state_generation+=2;assert(owner.receive(state,12));
  auto before=sink.count;assert(!owner.release_cover(2,13));assert(sink.count==before);
  // Opening: Stop and reverse Close available, duplicate Open suppressed.
  state.state.state_seq++;cover.state_generation++;cover.state=ControlCoverState::CONTROL_COVER_STATE_OPENING;
  assert(owner.receive(state,14));assert(!owner.can_cover(ControlOperation::CONTROL_OPERATION_COVER_OPEN));
  assert(owner.can_cover(ControlOperation::CONTROL_OPERATION_COVER_STOP));assert(owner.can_cover(ControlOperation::CONTROL_OPERATION_COVER_CLOSE));
  // Fully open endpoint suppresses Open, unknown disables all operations.
  state.state.state_seq++;cover.state_generation++;cover.state=ControlCoverState::CONTROL_COVER_STATE_OPEN;cover.position=100;
  assert(owner.receive(state,15));assert(!owner.can_cover(ControlOperation::CONTROL_OPERATION_COVER_OPEN));
  state.state.state_seq++;cover.state_generation++;cover.position_known=false;cover.position=0;
  assert(owner.receive(state,16));assert(owner.can_cover(ControlOperation::CONTROL_OPERATION_COVER_OPEN));
  state.state.state_seq++;cover.state_generation++;cover.available=false;cover.state=ControlCoverState::CONTROL_COVER_STATE_UNKNOWN;
  assert(owner.receive(state,17));assert(!owner.can_cover(ControlOperation::CONTROL_OPERATION_COVER_CLOSE));
  // Metadata changes revoke the selection rather than silently changing semantics.
  state.state.state_seq++;cover.caps.stop=false;
  assert(!owner.receive(state,18));assert(!owner.has_selection());
  assert(owner.last_reason()==ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED);
  ControlMessage cap;cap.generic=true;cap.kind=ControlKind::CONTROL_KIND_CAPABILITY;cap.enabled=true;cap.cover_enabled=true;
  assert(codec.serialize_entity(cap,buffer,sizeof(buffer),length));assert(codec.parse_entity({buffer,length},parsed));assert(parsed.cover_enabled);
}
"""

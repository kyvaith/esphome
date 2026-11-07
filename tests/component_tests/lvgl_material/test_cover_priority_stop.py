"""Negotiated Stop owner behavior with unchanged v1 wire and bounded deadlines."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command


class CoverPriorityOwnerTests(unittest.TestCase):
    def test_exact_channel_pending_stop_late_result_and_deadline(self):
        sources = [
            "material_entity_control.cpp",
            "material_entity_control_protocol.cpp",
            "material_entity_control_codec.cpp",
            "material_number_domain.cpp",
            "material_entity_control_generic.cpp",
            "material_entity_control_generic_protocol.cpp",
            "material_entity_control_cover.cpp",
        ]
        with tempfile.TemporaryDirectory(prefix="cover-priority-owner-") as temporary:
            root = Path(temporary)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("")
            harness = root / "priority.cpp"
            harness.write_text(HARNESS, encoding="utf-8")
            executable = root / "priority.exe"
            command = [
                *compiler_command(),
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DARDUINOJSON_POOL_CAPACITY=128",
                "-DUSE_LVGL_MATERIAL_NUMBER_STEPPER",
                "-DUSE_LVGL_MATERIAL_COVER_CONTROLS",
                "-DUSE_LVGL_MATERIAL_COVER_PRIORITY_STOP",
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
            compiled = subprocess.run(
                command, capture_output=True, text=True, timeout=120, check=False
            )
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            ran = subprocess.run(
                [str(executable)],
                capture_output=True,
                text=True,
                timeout=20,
                check=False,
            )
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


HARNESS = r"""
#include "material_entity_control.h"
#include <cassert>
using namespace esphome::lvgl_material;
struct Sink : MaterialEntityControlSink {
  ControlMessage last;size_t count{0};
  bool emit(const ControlMessage &message) override {last=message;count++;return true;}
  void view_event(ControlViewEvent) override {}
};
ControlMessage grant(Sink &sink) {
  ControlMessage value;value.generic=true;value.kind=ControlKind::CONTROL_KIND_GRANT;
  value.identity=sink.last.identity;value.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  value.state.emplace_family<ControlCoverSnapshot>();
  value.state.state_seq=1;(*value.state.family_if<ControlCoverSnapshot>()).state_generation=1;
  (*value.state.family_if<ControlCoverSnapshot>()).state=ControlCoverState::CONTROL_COVER_STATE_CLOSED;
  (*value.state.family_if<ControlCoverSnapshot>()).available=true;(*value.state.family_if<ControlCoverSnapshot>()).position_known=true;(*value.state.family_if<ControlCoverSnapshot>()).caps={true,true,true};return value;
}
int main() {
  Sink sink;MaterialEntityControl owner(sink);ControlText<32> a,c;
  a.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");c.assign("cccccccccccccccccccccccccccccccc");
  owner.set_enabled(true);owner.set_cover_enabled(true);owner.bind_generic(a);
  assert(!owner.cover_priority_stop_supported());assert(!owner.accept_cover_features(a,1));
  assert(owner.set_cover_priority_stop_enabled(true));assert(owner.cover_priority_stop_supported());
  assert(!owner.accept_cover_features(c,1));assert(!owner.accept_cover_features(a,2));
  ControlContext context;context.slot=15;context.revision=91;context.card_id.assign("cover");context.control_kind=ControlEntityKind::CONTROL_ENTITY_COVER;
  assert(owner.request_open(context,1));auto value=grant(sink);assert(owner.receive(value,2));assert(owner.set_view_ready(true,3));
  assert(owner.begin_cover(1,ControlOperation::CONTROL_OPERATION_COVER_OPEN,4));assert(owner.release_cover(1,5));
  assert(!owner.can_cover(ControlOperation::CONTROL_OPERATION_COVER_STOP));
  assert(!owner.accept_cover_features(a,1)); // No change underneath an active view.
  assert(owner.bind_generic(c));assert(owner.accept_cover_features(c,1));assert(owner.cover_priority_stop_active());
  assert(owner.request_open(context,10));value=grant(sink);assert(owner.receive(value,11));assert(owner.set_view_ready(true,12));
  assert(owner.begin_cover(2,ControlOperation::CONTROL_OPERATION_COVER_OPEN,13));assert(owner.release_cover(2,14));
  assert(owner.command_sequence()==1 && owner.can_cover(ControlOperation::CONTROL_OPERATION_COVER_STOP));
  assert(!owner.can_cover(ControlOperation::CONTROL_OPERATION_COVER_OPEN));
  assert(owner.begin_cover(3,ControlOperation::CONTROL_OPERATION_COVER_STOP,15));
  value.kind=ControlKind::CONTROL_KIND_STATE;value.state.state_seq=2;(*value.state.family_if<ControlCoverSnapshot>()).state_generation=2;
  (*value.state.family_if<ControlCoverSnapshot>()).state=ControlCoverState::CONTROL_COVER_STATE_OPENING;(*value.state.family_if<ControlCoverSnapshot>()).position=30;
  assert(owner.receive(value,16));assert(owner.is_dragging());
  value.state.state_seq++;(*value.state.family_if<ControlCoverSnapshot>()).state_generation++;(*value.state.family_if<ControlCoverSnapshot>()).position=40;
  assert(owner.receive(value,17));assert(owner.is_dragging());
  assert(owner.release_cover(3,18));assert(sink.last.operation==ControlOperation::CONTROL_OPERATION_COVER_STOP);
  assert(sink.last.command_seq==2 && sink.last.expected_state_generation==3);
  assert(!owner.can_cover(ControlOperation::CONTROL_OPERATION_COVER_STOP));
  ControlMessage old;old.generic=true;old.kind=ControlKind::CONTROL_KIND_RESULT;old.identity=value.identity;
  old.command_seq=1;old.state.state_seq=3;old.result=ControlResult::CONTROL_RESULT_STATE_MATCHES;
  assert(!owner.receive(old,19));assert(owner.is_command_pending());
  owner.tick(14020);assert(owner.is_command_pending() && owner.has_selection());
  owner.tick(28020);assert(!owner.has_selection());assert(owner.last_result()==ControlResult::CONTROL_RESULT_NOT_CONFIRMED);
  owner.disconnect();assert(!owner.cover_priority_stop_active());owner.bind_generic(a);assert(!owner.cover_priority_stop_active());
  // Ordinary movement keeps its original thirteen-second deadline.
  assert(owner.accept_cover_features(a,1));assert(owner.request_open(context,30000));value=grant(sink);
  assert(owner.receive(value,30001));assert(owner.set_view_ready(true,30002));
  assert(owner.begin_cover(4,ControlOperation::CONTROL_OPERATION_COVER_OPEN,30003));assert(owner.release_cover(4,30004));
  owner.tick(43005);assert(!owner.has_selection());
}
"""

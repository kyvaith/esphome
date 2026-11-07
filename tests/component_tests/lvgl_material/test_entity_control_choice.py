"""Opt-in choice owner and strict codec using the production C++ sources."""

from __future__ import annotations

import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command


class ChoiceOwnerHostTests(unittest.TestCase):
    def test_candidate_apply_stale_state_and_exact_wire(self) -> None:
        sources = (
            "material_entity_control.cpp",
            "material_entity_control_protocol.cpp",
            "material_entity_control_codec.cpp",
            "material_number_domain.cpp",
            "material_entity_control_generic.cpp",
            "material_entity_control_generic_protocol.cpp",
            "material_entity_control_choice.cpp",
        )
        paths = [
            *(SOURCE / name for name in sources),
            *SOURCE.glob("material_entity_control*.h"),
        ]
        pins = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
        with tempfile.TemporaryDirectory(
            prefix="entity-choice-host-", ignore_cleanup_errors=True
        ) as temporary:
            root = Path(temporary)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("", encoding="utf-8")
            harness = root / "choice.cpp"
            harness.write_text(HARNESS, encoding="utf-8")
            executable = root / "choice.exe"
            command = [
                *compiler_command(),
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DARDUINOJSON_POOL_CAPACITY=128",
                "-DUSE_LVGL_MATERIAL_NUMBER_STEPPER",
                "-DUSE_LVGL_MATERIAL_CHOICE_SELECTOR",
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
        self.assertEqual(
            pins,
            {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths},
        )


HARNESS = r"""
#include "material_entity_control.h"
#include "material_entity_control_codec.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace esphome::lvgl_material;
struct Sink : MaterialEntityControlSink {
  ControlMessage last;
  size_t count{0};
  bool emit(const ControlMessage &message) override { last=message;count++;return true; }
  void view_event(ControlViewEvent) override {}
};
ControlMessage grant(const Sink &sink) {
  ControlMessage value;
  value.generic=true;
  value.kind=ControlKind::CONTROL_KIND_GRANT;
  value.identity=sink.last.identity;
  assert(value.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
  value.state.state_seq=1;
  value.state.emplace_family<ControlSnapshot::Choice>();
  (*value.state.family_if<ControlSnapshot::Choice>()).state_generation=1;
  (*value.state.family_if<ControlSnapshot::Choice>()).count=3;
  (*value.state.family_if<ControlSnapshot::Choice>()).available=true;
  assert(value.state.title.assign("Choose"));
  assert((*value.state.family_if<ControlSnapshot::Choice>()).options[0].assign("Eco"));
  assert((*value.state.family_if<ControlSnapshot::Choice>()).options[1].assign("Komfort"));
  assert((*value.state.family_if<ControlSnapshot::Choice>()).options[2].assign(" Boost "));
  return value;
}
int main() {
  Sink sink;
  MaterialEntityControl owner(sink);
  ControlText<32> channel;
  assert(channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  assert(owner.set_enabled(true));assert(owner.bind_generic(channel));
  ControlContext context;
  context.slot=2;context.revision=4;context.control_kind=ControlEntityKind::CONTROL_ENTITY_CHOICE;
  assert(context.card_id.assign("card-2"));
  assert(!owner.request_open(context,1));
  assert(owner.set_choice_enabled(true));assert(owner.request_open(context,2));
  auto value=grant(sink);
  assert(control_valid_choice_snapshot(value.state));
  auto reserved=value;
  (*reserved.state.family_if<ControlSnapshot::Choice>()).options[1].assign("unknown");assert(!control_valid_choice_snapshot(reserved.state));
  (*reserved.state.family_if<ControlSnapshot::Choice>()).options[1].assign("unavailable");assert(!control_valid_choice_snapshot(reserved.state));
  assert(owner.receive(value,3));assert(owner.set_view_ready(true,4));
  assert(owner.is_choice() && owner.is_discrete() && !owner.is_number());
  assert(owner.choice_candidate_known() && owner.choice_candidate()==0);
  assert(!owner.can_apply_choice());
  const auto opened=sink.count;
  assert(owner.begin_choice(7,1,5));assert(owner.release_choice(7,6));
  assert(owner.choice_candidate()==1 && owner.can_apply_choice());
  assert(sink.count==opened);
  value.kind=ControlKind::CONTROL_KIND_STATE;value.state.state_seq=2;
  assert(value.state.title.assign("Renewed"));
  assert(owner.receive(value,7));assert(owner.choice_candidate()==1);
  assert(owner.begin_choice_apply(8,8));
  value.state.state_seq=3;(*value.state.family_if<ControlSnapshot::Choice>()).state_generation=3;
  assert(owner.receive(value,9));assert(!owner.release_choice_apply(8,10));
  assert(owner.choice_candidate()==0 && sink.count==opened);
  assert(owner.begin_choice(9,2,11));assert(owner.release_choice(9,12));
  assert(owner.begin_choice_apply(10,13));assert(owner.release_choice_apply(10,14));
  assert(sink.count==opened+1 && sink.last.operation==ControlOperation::CONTROL_OPERATION_CHOOSE);
  assert(sink.last.target_index==2 && sink.last.expected_index==0 && sink.last.expected_state_generation==3);
  assert(!owner.can_choose(0) && !owner.can_apply_choice());
  MaterialEntityControlCodec codec;
  char output[CONTROL_MAX_PAYLOAD_BYTES+1];size_t length=0;
  assert(codec.serialize_entity(sink.last,output,sizeof(output),length));
  ControlMessage decoded;
  assert(codec.parse_entity({output,length},decoded));
  assert(decoded.target_index==2 && decoded.expected_state_generation==3);
  ControlMessage result;
  result.generic=true;result.kind=ControlKind::CONTROL_KIND_RESULT;result.identity=value.identity;
  result.command_seq=1;result.state.state_seq=4;result.result=ControlResult::CONTROL_RESULT_STATE_MATCHES;
  assert(!owner.receive(result,15));
  value.state.state_seq=4;(*value.state.family_if<ControlSnapshot::Choice>()).state_generation=4;(*value.state.family_if<ControlSnapshot::Choice>()).index=2;
  assert(owner.receive(value,16));assert(owner.receive(result,17));
  assert(owner.choice_candidate()==2 && !owner.can_apply_choice());
  value.state.state_seq=5;(*value.state.family_if<ControlSnapshot::Choice>()).state_generation=5;(*value.state.family_if<ControlSnapshot::Choice>()).available=false;(*value.state.family_if<ControlSnapshot::Choice>()).index=0;
  assert(owner.receive(value,18));assert(!owner.choice_candidate_known() && !owner.can_choose(0));
  value.state.state_seq=6;(*value.state.family_if<ControlSnapshot::Choice>()).state_generation=6;(*value.state.family_if<ControlSnapshot::Choice>()).available=true;
  assert(owner.receive(value,19));assert(owner.choice_candidate()==0);
  assert(owner.begin_choice(11,1,20));owner.cancel_drag();assert(!owner.release_choice(11,21));
  value.state.state_seq=7;assert((*value.state.family_if<ControlSnapshot::Choice>()).options[1].assign("Other"));
  assert(!owner.receive(value,22) && !owner.has_selection());
  assert(owner.request_open(context,23));value=grant(sink);assert(owner.receive(value,24));
  assert(owner.set_view_ready(true,25));assert(owner.choice_candidate()==0);
  assert(owner.begin_choice(12,1,26));assert(owner.release_choice(12,27));
  assert(owner.begin_choice_apply(13,28));assert(owner.release_choice_apply(13,29));
  assert(owner.tick(13030));assert(!owner.has_selection());
  ControlMessage capability;
  capability.generic=true;capability.kind=ControlKind::CONTROL_KIND_CAPABILITY;capability.enabled=true;
  assert(codec.serialize_entity(capability,output,sizeof(output),length));
  assert(std::strstr(output,"choice")==nullptr);
  capability.choice_enabled=true;
  assert(codec.serialize_entity(capability,output,sizeof(output),length));
  assert(codec.parse_entity({output,length},decoded) && decoded.choice_enabled);
  auto maximum=grant(sink);(*maximum.state.family_if<ControlSnapshot::Choice>()).count=8;(*maximum.state.family_if<ControlSnapshot::Choice>()).index=7;
  std::array<char,33> label;label.fill('"');label[32]=0;
  for(size_t i=0;i<8;i++){label[31]=static_cast<char>('0'+i);assert((*maximum.state.family_if<ControlSnapshot::Choice>()).options[i].assign(label.data()));}
  std::array<char,161> title;title.fill('"');title[160]=0;assert(maximum.state.title.assign(title.data()));
  assert(codec.serialize_entity(maximum,output,sizeof(output),length) && length<=2048);
  assert(codec.parse_entity({output,length},decoded) && (*decoded.state.family_if<ControlSnapshot::Choice>())==(*maximum.state.family_if<ControlSnapshot::Choice>()));
  {
    Sink empty_sink;
    MaterialEntityControl empty_owner(empty_sink);
    assert(empty_owner.set_enabled(true));
    assert(empty_owner.set_choice_enabled(true));
    assert(empty_owner.bind_generic(channel));
    assert(!empty_owner.choice_empty_active());
    assert(!empty_owner.accept_choice_features(channel,1));
    assert(empty_owner.set_choice_empty_enabled(true));
    ControlText<32> other;
    assert(other.assign("cccccccccccccccccccccccccccccccc"));
    assert(!empty_owner.accept_choice_features(other,1));
    assert(!empty_owner.accept_choice_features(channel,0));
    assert(empty_owner.accept_choice_features(channel,1));
    assert(empty_owner.choice_empty_active());
    assert(empty_owner.request_open(context,100));
    assert(!empty_owner.accept_choice_features(channel,1));
    auto manual=grant(empty_sink);
    (*manual.state.family_if<ControlSnapshot::Choice>()).index=ControlSnapshot::Choice::EMPTY_INDEX;
    assert(!control_valid_choice_snapshot(manual.state));
    assert(control_valid_choice_snapshot(manual.state,true));
    assert(!codec.serialize_entity(manual,output,sizeof(output),length));
    assert(codec.serialize_entity(manual,output,sizeof(output),length,true));
    const std::string manual_wire(output,length);
    assert(manual_wire.find("\"index\":null")!=std::string::npos);
    assert(!codec.parse_entity(manual_wire,decoded));
    assert(codec.parse_entity(manual_wire,decoded,true));
    assert((*decoded.state.family_if<ControlSnapshot::Choice>()).index==ControlSnapshot::Choice::EMPTY_INDEX && (*decoded.state.family_if<ControlSnapshot::Choice>()).available);
    assert(!codec.parse_entity(manual_wire,decoded));
    assert(empty_owner.receive(manual,101));assert(empty_owner.set_view_ready(true,102));
    assert(!empty_owner.choice_candidate_known() && !empty_owner.can_apply_choice());
    assert(empty_owner.can_choose(0));
    assert(empty_owner.begin_choice(21,0,103));assert(empty_owner.release_choice(21,104));
    assert(empty_owner.can_apply_choice());
    assert(empty_owner.begin_choice_apply(22,105));assert(empty_owner.release_choice_apply(22,106));
    assert(empty_sink.last.expected_index==ControlSnapshot::Choice::EMPTY_INDEX && empty_sink.last.target_index==0);
    assert(!codec.serialize_entity(empty_sink.last,output,sizeof(output),length));
    assert(codec.serialize_entity(empty_sink.last,output,sizeof(output),length,true));
    assert(std::string(output,length).find("\"expected_index\":null")!=std::string::npos);
    assert(codec.parse_entity({output,length},decoded,true));
    assert(decoded.expected_index==ControlSnapshot::Choice::EMPTY_INDEX);
    empty_owner.disconnect();assert(!empty_owner.choice_empty_active());
    assert(empty_owner.bind_generic(other));assert(!empty_owner.choice_empty_active());
    assert(!empty_owner.accept_choice_features(channel,1));
    assert(empty_owner.accept_choice_features(other,1));
    assert(empty_owner.request_open(context,200));manual=grant(empty_sink);
    (*manual.state.family_if<ControlSnapshot::Choice>()).index=ControlSnapshot::Choice::EMPTY_INDEX;
    assert(empty_owner.receive(manual,201));assert(empty_owner.set_view_ready(true,202));
    assert(empty_owner.begin_choice(23,1,203));assert(empty_owner.release_choice(23,204));
    assert(empty_owner.begin_choice_apply(24,205));
    auto aba=manual;aba.kind=ControlKind::CONTROL_KIND_STATE;
    aba.state.state_seq=2;(*aba.state.family_if<ControlSnapshot::Choice>()).state_generation=3;
    assert(empty_owner.receive(aba,206));
    const auto count=empty_sink.count;
    assert(!empty_owner.release_choice_apply(24,207));assert(empty_sink.count==count);
    assert(!empty_owner.choice_candidate_known());
    aba.state.state_seq=3;(*aba.state.family_if<ControlSnapshot::Choice>()).state_generation=4;
    (*aba.state.family_if<ControlSnapshot::Choice>()).available=false;(*aba.state.family_if<ControlSnapshot::Choice>()).index=0;
    assert(empty_owner.receive(aba,208));assert(!empty_owner.can_choose(0));
    assert(empty_owner.bind_generic(channel));assert(!empty_owner.choice_empty_active());
  }
  std::cout<<"choice owner and codec passed, maximum wire="<<length<<" snapshot="<<sizeof(ControlSnapshot)<<"\n";
}
"""

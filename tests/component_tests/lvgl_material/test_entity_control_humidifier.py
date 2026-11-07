"""Fan session generations, absolute intent and bounded production wire codec."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command


class HumidifierOwnerHostTests(unittest.TestCase):
    def test_generation_and_strict_wire(self):
        sources = [
            "material_entity_control.cpp",
            "material_entity_control_protocol.cpp",
            "material_entity_control_codec.cpp",
            "material_number_domain.cpp",
            "material_entity_control_generic.cpp",
            "material_entity_control_generic_protocol.cpp",
            "material_entity_control_humidifier.cpp",
        ]
        for siblings in (False, True):
            with (
                self.subTest(siblings=siblings),
                tempfile.TemporaryDirectory(prefix="humidity-owner-") as temporary,
            ):
                root = Path(temporary)
                defines = root / "esphome/core/defines.h"
                defines.parent.mkdir(parents=True)
                defines.write_text("")
                harness = root / "humidity.cpp"
                harness.write_text(HARNESS, encoding="utf8")
                executable = root / "humidity.exe"
                optional = (
                    [
                        "-DUSE_LVGL_MATERIAL_CHOICE_SELECTOR",
                        "-DUSE_LVGL_MATERIAL_COVER_CONTROLS",
                        "-DUSE_LVGL_MATERIAL_THERMOSTAT",
                        "-DUSE_LVGL_MATERIAL_FAN_CONTROLS",
                        str(SOURCE / "material_entity_control_fan.cpp"),
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
                    "-DUSE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS",
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
 ControlMessage last;unsigned count{0};
 bool emit(const ControlMessage &message) override {last=message;count++;return true;}
 void view_event(ControlViewEvent) override {}
};
int main() {
 Sink sink;MaterialEntityControl owner(sink);MaterialEntityControlCodec codec;
 ControlText<32> channel;channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
 assert(owner.set_enabled(true));assert(owner.bind_generic(channel));
 ControlContext context;context.slot=31;context.revision=348;context.card_id.assign("humidity-31");
 context.control_kind=ControlEntityKind::CONTROL_ENTITY_HUMIDIFIER;
 assert(!owner.request_open(context,1));assert(owner.set_humidifier_enabled(true));assert(owner.request_open(context,2));
 ControlMessage state;state.generic=true;state.kind=ControlKind::CONTROL_KIND_GRANT;state.identity=sink.last.identity;
 state.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");state.state.title.assign("Humidifier");state.state.state_seq=1;
 state.state.emplace_family<ControlSnapshot::Humidifier>();
 auto &humidity=(*state.state.family_if<ControlSnapshot::Humidifier>());humidity.caps.turn_on=humidity.caps.turn_off=true;
 humidity.caps.mode_count=2;humidity.caps.modes[0].assign("Eco");humidity.caps.modes[1].assign("Normal");
 humidity.available=true;humidity.power=1;humidity.mode_index=0;humidity.device_class=0;humidity.state_generation=1;
 humidity.domain.minimum=30*NUMBER_SCALE;humidity.domain.maximum=80*NUMBER_SCALE;humidity.domain.step=NUMBER_SCALE;
 humidity.domain.max_index=50;humidity.domain.unit[0]='%';humidity.index=15;humidity.target_text.assign("45");humidity.current.assign("39.5");
 assert(control_valid_humidifier_snapshot(state.state));assert(owner.receive(state,3));assert(owner.set_view_ready(true,4));
 assert(owner.is_humidifier()&&owner.is_discrete());
 char buffer[2049];size_t length;ControlMessage parsed;
 assert(codec.serialize_entity(state,buffer,sizeof(buffer),length));std::string wire(buffer,length);
 assert(codec.parse_entity(wire,parsed));assert(parsed.state==state.state);
 auto bad=wire;bad.insert(bad.find("\"turn_on\":true"),"\"turn_on\":true,");assert(!codec.parse_entity(bad,parsed));
 assert(owner.begin_humidifier(1,ControlOperation::CONTROL_OPERATION_HUMIDIFIER_TARGET,15,5));
 assert(owner.update_humidifier_target(1,20,6));
 state.kind=ControlKind::CONTROL_KIND_STATE;state.state.state_seq++;humidity.current.assign("40.25");
 assert(owner.receive(state,7));assert(owner.is_dragging());assert(owner.release_humidifier(1,8));
 assert(sink.last.humidifier_target_index==20&&sink.last.expected_state_generation==1);
 assert(codec.serialize_entity(sink.last,buffer,sizeof(buffer),length));assert(codec.parse_entity({buffer,length},parsed));
 assert(parsed.operation==ControlOperation::CONTROL_OPERATION_HUMIDIFIER_TARGET);
 ControlMessage result;result.generic=true;result.kind=ControlKind::CONTROL_KIND_RESULT;result.identity=state.identity;
 result.command_seq=owner.command_sequence();result.state.state_seq=state.state.state_seq;result.result=ControlResult::CONTROL_RESULT_STATE_MATCHES;
 assert(owner.receive(result,9));assert(!owner.release_humidifier(1,10));
 assert(owner.begin_humidifier(2,ControlOperation::CONTROL_OPERATION_HUMIDIFIER_TARGET,15,11));
 state.state.state_seq++;humidity.state_generation+=2;assert(owner.receive(state,12));
 auto count=sink.count;assert(!owner.release_humidifier(2,13));assert(count==sink.count);
 assert(owner.begin_humidifier(3,ControlOperation::CONTROL_OPERATION_HUMIDIFIER_POWER,0,14));assert(owner.release_humidifier(3,15));
 assert(codec.serialize_entity(sink.last,buffer,sizeof(buffer),length));assert(codec.parse_entity({buffer,length},parsed));
 assert(parsed.operation==ControlOperation::CONTROL_OPERATION_HUMIDIFIER_POWER); // distinct from Fan power
 result.command_seq=owner.command_sequence();result.state.state_seq=state.state.state_seq;assert(owner.receive(result,16));
 state.state.state_seq++;humidity.state_generation++;humidity.index=255;humidity.target_text.assign("--");
 assert(owner.receive(state,17));assert(!owner.can_humidifier(ControlOperation::CONTROL_OPERATION_HUMIDIFIER_TARGET,20));
 assert(owner.can_humidifier(ControlOperation::CONTROL_OPERATION_HUMIDIFIER_POWER,0));
 humidity.caps.mode_count=8;state.state.title.assign(std::string(160,'T'));
 for(unsigned i=0;i<8;i++)humidity.caps.modes[i].assign(std::string(32,'a'+i));
 assert(codec.serialize_entity(state,buffer,sizeof(buffer),length));assert(codec.parse_entity({buffer,length},parsed));
 assert(length<=2048&&codec.high_water()<=5120);
 humidity.available=false;humidity.power=humidity.mode_index=255;humidity.current.assign("");
 assert(control_valid_humidifier_snapshot(state.state));
 assert(codec.serialize_entity(state,buffer,sizeof(buffer),length));assert(codec.parse_entity({buffer,length},parsed));
 humidity.current.assign("0");assert(!control_valid_humidifier_snapshot(state.state));
}
"""

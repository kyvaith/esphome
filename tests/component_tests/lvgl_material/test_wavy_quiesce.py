"""Execute actual additive quiesce/status bodies with bounded producer/retirement fakes.

Not a full renderer/ESP-IDF build or real RTOS/DSI scheduling test.
"""

from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, compiler_command


def extract(text, marker):
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 1
    position = opening + 1
    while depth:
        depth += (text[position] == "{") - (text[position] == "}")
        position += 1
    return text[start:position]


class WaveQuiesceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="wave-quiesce-host-")
        cls.root = Path(cls.temp.name)
        support = cls.root / "esphome/core/component.h"
        support.parent.mkdir(parents=True)
        support.write_text("""#pragma once
namespace esphome {
namespace setup_priority {constexpr float PROCESSOR=600;}
class Component {public:virtual ~Component()=default;virtual void setup(){};virtual void on_shutdown(){};
virtual void dump_config(){};virtual float get_setup_priority()const{return 0;}};
}
""")
        (cls.root / "lvgl.h").write_text(
            "#pragma once\n#include <cstdint>\nstruct lv_obj_t{};struct lv_image_dsc_t{};\n"
        )
        source = (SOURCE / "material_wavy_progress.cpp").read_text(encoding="utf-8")
        methods = "\n".join(
            extract(source, marker)
            for marker in (
                "bool MaterialWavyProgress::quiesce_direct_present(",
                "bool MaterialWavyProgress::is_direct_present_enabled() const",
            )
        )
        cpp = cls.root / "quiesce.cpp"
        cpp.write_text(
            r"""
#include "material_wavy_progress.h"
#include <atomic>
#include <cassert>
#include <functional>
#include <iostream>
inline uint32_t now_ms=0;
inline uint32_t millis(){return now_ms;}
inline std::function<void()> on_delay;
inline int delay_calls=0,release_calls=0,retire_calls=0,service_calls=0;
inline bool lock_ok=true,release_ok=true,gate_violation=false;
inline bool mutex_held=false;
inline uint32_t expected_timeout=0;
inline constexpr int pdTRUE=1;
inline int xSemaphoreTake(void*,int timeout){assert(timeout==0);if(!lock_ok)return 0;assert(!mutex_held);mutex_held=true;return 1;}
inline void delay(uint32_t ms){now_ms+=ms;delay_calls++;if(on_delay)on_delay();}
inline bool lvgl_esphome_direct_blit_rgb888_try_release(int,int,int,int);
namespace esphome::lvgl_material {
namespace wavy_progress_internal {
[[maybe_unused]] constexpr int WAVE_SIZE=244;
struct State {
 std::atomic<bool>direct_present_enabled{true};
 void*render_mutex{reinterpret_cast<void*>(1)};
 uint32_t render_generation{5};bool frame_ready{true};
 std::atomic<int>queued_phase_delta{7};
 int direct_x{12},direct_y{34},inflight{0},completed{0};
} media_wavy_arc;
inline int present_in_flight_count(const State*s){return s->inflight;}
inline void unlock_render_state(State*s){if(s->render_mutex){assert(mutex_held);mutex_held=false;}}
inline bool media_wavy_arc_present_ready(bool){
 service_calls++;auto&s=media_wavy_arc;
 gate_violation|=s.direct_present_enabled.load();
 if(s.render_mutex&&!lock_ok)return false;
 if(s.completed){assert(s.inflight>=s.completed);s.inflight-=s.completed;retire_calls+=s.completed;s.completed=0;}
 return false;
}
}
void MaterialWavyProgress::setup(){}
void MaterialWavyProgress::on_shutdown(){}
void MaterialWavyProgress::dump_config(){}
"""
            + methods
            + r"""
}
inline bool lvgl_esphome_direct_blit_rgb888_try_release(int x,int y,int width,int height){
 auto&s=esphome::lvgl_material::wavy_progress_internal::media_wavy_arc;
 assert(!s.direct_present_enabled.load()&&s.inflight==0);
 assert(s.render_mutex==nullptr||mutex_held);
 assert(x==12&&y==34&&width==244&&height==244);release_calls++;return release_ok;
}
int main(int argc,char**argv){
 assert(argc==2);const int scenario=std::stoi(argv[1]);
 using namespace esphome::lvgl_material;
 auto&s=wavy_progress_internal::media_wavy_arc;(void)s;MaterialWavyProgress wave;
#ifdef ESP_PLATFORM
 if(scenario==0){assert(wave.quiesce_direct_present(5));assert(release_calls==1&&!s.frame_ready&&s.render_generation==6);}
 else if(scenario==1){lock_ok=false;s.inflight=1;assert(!wave.quiesce_direct_present(3));assert(release_calls==0&&s.inflight==1&&s.frame_ready);}
 else if(scenario==2){s.inflight=1;assert(!wave.quiesce_direct_present(3));assert(release_calls==0&&s.inflight==1);}
 else if(scenario==3){s.inflight=1;on_delay=[&](){if(delay_calls==2)s.completed=1;};assert(wave.quiesce_direct_present(5));assert(retire_calls==1&&release_calls==1);}
 else if(scenario==4){release_ok=false;assert(!wave.quiesce_direct_present(3));assert(release_calls>=1&&s.inflight==0);}
 else if(scenario==5){release_ok=false;on_delay=[](){if(delay_calls==2)release_ok=true;};assert(wave.quiesce_direct_present(5));assert(release_calls==3);}
 else if(scenario==6){s.direct_present_enabled=false;s.inflight=1;on_delay=[&](){s.completed=1;};assert(wave.quiesce_direct_present(5));assert(retire_calls==1);}
 else if(scenario==7){assert(wave.quiesce_direct_present(5));assert(!wave.is_direct_present_enabled());s.direct_present_enabled=true;assert(wave.is_direct_present_enabled());return 0;}
 else if(scenario==8){s.render_mutex=nullptr;assert(wave.quiesce_direct_present(0));assert(release_calls==1);}
 else if(scenario==9){assert(wave.quiesce_direct_present(0));assert(delay_calls==0);}
 else if(scenario==10){lock_ok=false;assert(!wave.quiesce_direct_present(0));assert(delay_calls==0&&release_calls==0);}
 else if(scenario==11){s.inflight=1;lock_ok=false;on_delay=[&](){if(delay_calls==1)lock_ok=true;if(delay_calls==2)s.completed=1;};assert(wave.quiesce_direct_present(5));assert(retire_calls==1&&release_calls==1);}
 else return 2;
 assert(!wave.is_direct_present_enabled()&&!gate_violation&&!mutex_held);
#else
 assert(scenario==0&&wave.quiesce_direct_present(0));assert(!wave.is_direct_present_enabled());
 assert(release_calls==0&&service_calls==0);
#endif
 std::cout<<"quiesce case "<<scenario<<" PASS\n";
}
"""
        )
        cls.exes = {}
        for enabled in (False, True):
            executable = cls.root / f"quiesce-{enabled}.exe"
            command = [
                *compiler_command(),
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(cls.root),
                "-I",
                str(SOURCE),
                str(cpp),
                "-o",
                str(executable),
            ]
            if enabled:
                command.append("-DESP_PLATFORM")
            run = subprocess.run(
                command, capture_output=True, text=True, timeout=120, check=False
            )
            if run.returncode:
                raise AssertionError(run.stdout + run.stderr)
            cls.exes[enabled] = executable

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_actual_quiesce_source_and_retirement_boundaries(self):
        for case in range(12):
            with self.subTest(case=case):
                run = subprocess.run(
                    [str(self.exes[True]), str(case)],
                    capture_output=True,
                    text=True,
                    timeout=10,
                    check=False,
                )
                self.assertEqual(0, run.returncode, run.stdout + run.stderr)

    def test_non_esp_path_has_no_direct_ownership(self):
        run = subprocess.run(
            [str(self.exes[False]), "0"],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        self.assertEqual(0, run.returncode, run.stdout + run.stderr)

    def test_disabled_producer_still_services_completed_sources(self):
        source = (SOURCE / "material_wavy_progress.cpp").read_text(encoding="utf-8")
        service = extract(
            source, "inline bool media_wavy_arc_present_ready(bool worker_context) {"
        )
        self.assertLess(
            service.index("completed->in_flight.store(false"),
            service.index("if (!state.direct_present_enabled.load"),
        )


if __name__ == "__main__":
    unittest.main()

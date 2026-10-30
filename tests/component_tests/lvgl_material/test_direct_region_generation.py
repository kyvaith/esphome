"""Compile exact new enqueue/filter/commit/retirement blocks with bounded fakes.

This is not a build of the full ESP32/PPA translation unit or a DSI simulator.
"""

from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import LVGL_SOURCE, compiler_command

SOURCE = LVGL_SOURCE


def between(text, first, last):
    start = text.index(first)
    return text[start : text.index(last, start)]


def harness():
    header = (SOURCE / "lvgl_esphome.h").read_text(encoding="utf-8")
    source = (SOURCE / "lvgl_esphome.cpp").read_text(encoding="utf-8")
    request = between(
        header,
        "  struct DirectRegionRequest {",
        "  static constexpr size_t DIRECT_REGION_SLOT_COUNT",
    )
    request = request[: request.index("\n  };") + len("\n  };")]
    enqueue = between(
        source,
        "uint8_t LvglComponent::direct_blit_rgb888_generation_async(",
        "\n#endif\n\nuint8_t LvglComponent::direct_blit_xrgb8888_async(",
    )
    enqueue = enqueue.replace("LvglComponent::", "TestComponent::", 1)
    prototype = (
        enqueue[: enqueue.index("{")]
        .replace("TestComponent::", "")
        .replace("bool require_primary_slot)", "bool require_primary_slot = false)")
        + ";"
    )
    exclusive = between(
        source,
        "bool LvglComponent::direct_region_primary_slot_compatible_(",
        "\n#endif\n\nvoid LvglComponent::direct_region_task_()",
    )
    exclusive = exclusive.replace("LvglComponent::", "TestComponent::", 1)
    exclusive_prototype = (
        exclusive[: exclusive.index("{")].replace("TestComponent::", "") + ";"
    )
    defer = between(
        source, "    auto defer_request_completion =", "    auto add_latest_request ="
    )
    filtered = between(
        source, "      size_t retained = 0;", "\n#endif\n      const bool cache_only"
    )
    commit = between(
        source, "        bool generation_current =", "        auto invalidate_target ="
    )
    confirmed = between(
        source,
        "                  generation_carry_accepted[i] =",
        "\n#endif\n                }",
    )
    begin = source.index(
        "    for (size_t i = 0; i < deferred_completion_count; i++) {",
        source.index("void LvglComponent::direct_region_task_()"),
    )
    retirement = between(
        source[begin:],
        "    for (size_t i = 0; i < request_count; i++) {",
        "    // A priority barrier",
    )
    return (
        r"""
#include "direct_region_generation.h"
#include <array>
#include <atomic>
#include <cassert>
#include <functional>
#include <vector>
#include <iostream>
using namespace esphome::lvgl;
using LvglDirectBlitReadyCallback=void(*)(void*);
namespace display {enum Rotation{DISPLAY_ROTATION_0_DEGREES};}
namespace mipi_dsi {class MipiDsi{};}
inline mipi_dsi::MipiDsi display_instance;
inline constexpr uint8_t LVGL_DIRECT_BLIT_REJECTED=0,LVGL_DIRECT_BLIT_BUSY=1,LVGL_DIRECT_BLIT_SUBMITTED=2;
inline constexpr int pdTRUE=1;
inline bool s_snapshot_direct_active=false,s_snapshot_swipe_active=false;
inline void *s_region_srm_client=reinterpret_cast<void*>(1),*s_region_srm_mutex=reinterpret_cast<void*>(1);
inline bool test_send=true;
inline std::function<void(const void*)> test_enqueue;
inline int xQueueSend(void*,const void *request,int){if(test_send&&test_enqueue)test_enqueue(request);return test_send?pdTRUE:0;}
struct TestComponent {
"""
        + request
        + r"""
  bool direct_mode_active_{true},direct_image_animation_active_{false};
  display::Rotation rotation{display::DISPLAY_ROTATION_0_DEGREES};
  std::vector<mipi_dsi::MipiDsi*>displays_{&display_instance};
  std::atomic<bool>direct_region_paused_{false};
  std::atomic<uint32_t>direct_region_copy_submitted_{0},direct_region_submit_busy_{0},direct_region_base_generation_{7};
  void*direct_region_queue_{reinterpret_cast<void*>(1)};
  int width_{800},height_{800};
  uint32_t get_direct_region_base_generation() const{return direct_region_base_generation_.load();}
  std::vector<std::function<void()>>deferred;
  static constexpr size_t DIRECT_REGION_SLOT_COUNT=4;
  struct DirectRegionSlot{bool valid{};int x{},y{},width{},height{};};
  std::array<DirectRegionSlot,4>direct_region_slots_{};
"""
        + exclusive_prototype
        + r"""
"""
        + prototype
        + r"""
  void filter(DirectRegionRequest *requests,size_t &request_count){
    auto defer_completion=[&](LvglDirectBlitReadyCallback callback,void*arg,bool=false){deferred.emplace_back([=](){if(callback)callback(arg);});};
"""
        + defer
        + filtered
        + r"""
  }
  bool commit(DirectRegionRequest *requests,size_t request_count,uint32_t generation,bool target_composed=true){
"""
        + commit
        + r"""
    return generation_current;
  }
  struct Slot{bool persistent_carry{},persistent_copy_valid{};};
  bool carry(const DirectRegionRequest &request,Slot *slot){
    size_t i=0;bool generation_carry_accepted[1]{};
"""
        + confirmed
        + r"""
    return generation_carry_accepted[0];
  }
  void complete(DirectRegionRequest *requests,size_t request_count,const bool *generation_carry_accepted){
"""
        + retirement
        + r"""
  }
};
"""
        + enqueue
        + exclusive
        + r"""
#undef USE_LVGL_DIRECT_REGION_EXPECTED_GENERATION
struct Callback{int calls{0};DirectRegionGenerationResult*result{};DirectRegionGenerationState observed{DirectRegionGenerationState::PENDING};};
static void done(void *arg){auto*c=static_cast<Callback*>(arg);c->calls++;if(c->result)c->observed=c->result->state.load();}
int main(int argc,char**argv){
  assert(argc==2);const int scenario=std::stoi(argv[1]);TestComponent component;
  DirectRegionGenerationResult result;Callback callback{0,&result};std::array<uint8_t,12>pixels{};
  if(scenario==0){
    assert(component.direct_blit_rgb888_generation_async(pixels.data(),6,0,0,2,2,0,&result,done,&callback,false)==LVGL_DIRECT_BLIT_REJECTED);
    assert(component.direct_blit_rgb888_generation_async(pixels.data(),6,0,0,2,2,6,&result,done,&callback,false)==LVGL_DIRECT_BLIT_REJECTED);
    assert(callback.calls==0);
  }else if(scenario==1){
    TestComponent::DirectRegionRequest queued;
    test_enqueue=[&](const void*p){queued=*static_cast<const TestComponent::DirectRegionRequest*>(p);};
    assert(component.direct_blit_rgb888_generation_async(pixels.data(),6,0,0,2,2,7,&result,done,&callback,false)==LVGL_DIRECT_BLIT_SUBMITTED);
    assert(queued.source==pixels.data()&&queued.expected_generation==7&&queued.generation_result==&result&&queued.persistent_carry);
    assert(callback.calls==0&&result.state.load()==DirectRegionGenerationState::PENDING);
  }else if(scenario==2){
    test_send=false;
    assert(component.direct_blit_rgb888_generation_async(pixels.data(),6,0,0,2,2,7,&result,done,&callback,false)==LVGL_DIRECT_BLIT_BUSY);
    assert(callback.calls==0);
  }else if(scenario==3||scenario==4||scenario==5){
    std::array<TestComponent::DirectRegionRequest,4>r{};
    r[0].expected_generation=scenario==4?6:0;r[0].generation_result=&result;r[0].ready_callback=done;r[0].ready_arg=&callback;
    r[1].expected_generation=6;
    r[2].expected_generation=7;
    r[3].operation=TestComponent::DirectRegionRequest::Operation::BARRIER;
    if(scenario==4)r[2].expected_generation=6;
    if(scenario==5)component.direct_region_paused_.store(true);
    size_t count=r.size();component.filter(r.data(),count);
    assert(count==(scenario==3?3U:scenario==4?1U:2U));
    assert(r[count-1].operation==TestComponent::DirectRegionRequest::Operation::BARRIER);
    assert(callback.calls==0);
    for(auto &retire:component.deferred)retire();
    assert(callback.calls==(scenario==4?1:0));
    if(scenario==4)assert(callback.observed==DirectRegionGenerationState::RETIRED_UNCONFIRMED);
  }else if(scenario==6){
    TestComponent::DirectRegionRequest r;r.expected_generation=7;
    assert(component.commit(&r,1,7));component.direct_region_paused_.store(true);
    assert(!component.commit(&r,1,7));r.expected_generation=0;assert(component.commit(&r,1,7));
    component.direct_region_base_generation_.store(8);assert(!component.commit(&r,1,7));
  }else if(scenario==7){
    TestComponent::DirectRegionRequest r;r.expected_generation=7;TestComponent::Slot slot{true,false};
    assert(!component.carry(r,&slot));slot.persistent_copy_valid=true;assert(component.carry(r,&slot));
    slot.persistent_carry=false;assert(!component.carry(r,&slot));slot.persistent_carry=true;
    component.direct_region_base_generation_.store(8);assert(!component.carry(r,&slot));
  }else if(scenario==8||scenario==9){
    TestComponent::DirectRegionRequest r;r.expected_generation=7;r.generation_result=&result;r.ready_callback=done;r.ready_arg=&callback;
    bool carry=scenario==8;component.complete(&r,1,&carry);
    assert(callback.calls==1&&result.accepted(7)==carry);
    assert(callback.observed==(carry?DirectRegionGenerationState::QUEUED_WITH_CARRY:DirectRegionGenerationState::RETIRED_UNCONFIRMED));
  }else if(scenario==10){
    std::array<TestComponent::DirectRegionRequest,2>r{};r[0].expected_generation=7;r[0].require_primary_slot=true;
    r[0].generation_result=&result;r[0].ready_callback=done;r[0].ready_arg=&callback;
    r[1].width=10; // An unrelated legacy request must survive scalar refusal.
    size_t count=2;component.filter(r.data(),count);
    assert(count==1&&r[0].expected_generation==0&&r[0].width==10&&callback.calls==0);
    for(auto &retire:component.deferred)retire();assert(callback.calls==1);
  }else if(scenario==11){
    TestComponent::DirectRegionRequest r;r.require_primary_slot=true;r.expected_generation=7;
    r.width=564;r.height=112;
    assert(component.direct_region_primary_slot_compatible_(&r,1,0));
    component.direct_region_slots_[0]={true,0,0,564,112};assert(component.direct_region_primary_slot_compatible_(&r,1,0));
    component.direct_region_slots_[1]={true,0,0,564,112};assert(!component.direct_region_primary_slot_compatible_(&r,1,0));
    component.direct_region_slots_[1].valid=false;component.direct_region_slots_[0].width=100;
    assert(!component.direct_region_primary_slot_compatible_(&r,1,0));
  }else return 2;
  std::cout<<"generation case "<<scenario<<" PASS\n";
}
"""
    )


class RegionGenerationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="region-generation-host-")
        root = Path(cls.temp.name)
        cpp = root / "generation.cpp"
        cpp.write_text(harness())
        cls.executable = root / "generation.exe"
        result = subprocess.run(
            [
                *compiler_command(),
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DLV_COLOR_DEPTH=32",
                "-DUSE_ESP32",
                "-DUSE_MIPI_DSI",
                "-DUSE_LVGL_PPA",
                "-DUSE_LVGL_DIRECT_REGION_EXPECTED_GENERATION",
                "-I",
                str(SOURCE),
                str(cpp),
                "-o",
                str(cls.executable),
            ],
            capture_output=True,
            text=True,
            timeout=180,
            check=False,
        )
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_exact_generation_enqueue_filter_commit_and_retirement_blocks(self):
        for case in range(12):
            with self.subTest(case=case):
                result = subprocess.run(
                    [str(self.executable), str(case)],
                    capture_output=True,
                    text=True,
                    timeout=10,
                    check=False,
                )
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()

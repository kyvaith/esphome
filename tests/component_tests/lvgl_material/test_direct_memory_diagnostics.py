"""Compile exact protected metadata audit/log blocks; no pixel or renderer ownership."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import LVGL_SOURCE, compiler_command

SOURCE = LVGL_SOURCE / "lvgl_esphome.cpp"


class DirectCapacityAuditTests(unittest.TestCase):
    def test_all_slots_retained_capacity_and_unavailable_are_distinct(self):
        source = SOURCE.read_text(encoding="utf-8")
        start = source.index(
            "#if defined(USE_LVGL_PPA)\n  struct RegionSlotAudit",
            source.index("void LvglComponent::log_direct_buffer_state"),
        )
        end = source.index("\n#endif\n\n  constexpr int SAMPLE_ROWS", start) + len(
            "\n#endif"
        )
        audit = source[start:end]
        start = source.index(
            "#if defined(USE_LVGL_PPA) && defined(USE_LVGL_DIRECT_REGION_EXPECTED_GENERATION)",
            end,
        )
        logs = source[
            start : source.index("  mipi_display->log_dma_ring_state(label);", start)
        ]
        harness = (
            r"""
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
inline void*s_region_srm_mutex=reinterpret_cast<void*>(1);
inline bool test_locked=false,test_lock_ok=true;
inline int takes=0,gives=0;
inline constexpr int pdTRUE=1;
inline std::vector<std::string>test_logs;
inline int xSemaphoreTake(void*,int ticks){assert(ticks==0);takes++;if(!test_lock_ok)return 0;assert(!test_locked);test_locked=true;return 1;}
inline void xSemaphoreGive(void*){assert(test_locked);test_locked=false;gives++;}
template<typename...Args>void log_line(const char*,const char*format,Args...args){char line[512];std::snprintf(line,sizeof(line),format,args...);test_logs.emplace_back(line);}
#define ESP_LOGW(...) log_line(__VA_ARGS__)
struct Audit {
 static constexpr size_t DIRECT_REGION_SLOT_COUNT=4;
 struct Slot {
  int x{},y{},width{},height{};bool valid{},blend{},stable_carry{},persistent_carry{};
  bool animation_overlay_valid{},persistent_copy_valid{},scaled_background_valid{};
  uint8_t*animation_overlay{};uint8_t*persistent_copy{};uint8_t*transition_background{};uint8_t*scaled_background{};
  const uint8_t*latest_background{};
  size_t animation_overlay_size{},persistent_copy_size{},transition_background_size{},scaled_background_size{};
 };
 std::array<Slot,4>direct_region_slots_{};
 std::array<uint32_t,3>direct_region_buffer_generation_{};
 std::atomic<uint32_t>direct_region_base_generation_{7};
 bool available{};
 void capture(){
  const char*label="phase";
  char region_count[16]="unavailable",region_generations[40]="unavailable";
  uint32_t region_base_generation=0;
"""
            + audit
            + r"""
  (void)region_slots;(void)region_base_generation;(void)label;
  this->available=region_audit_available;
"""
            + logs
            + r"""
 }
};
int main(){
 Audit audit;
 for(size_t i=0;i<4;i++){
  auto&s=audit.direct_region_slots_[i];
  // Deliberately unreadable addresses prove the path copies metadata only.
  s.persistent_copy=reinterpret_cast<uint8_t*>(1);s.persistent_copy_size=1000*(i+1);
  s.animation_overlay=reinterpret_cast<uint8_t*>(2);s.animation_overlay_size=2000*(i+1);
  s.transition_background=reinterpret_cast<uint8_t*>(3);s.transition_background_size=3000*(i+1);
  s.scaled_background=reinterpret_cast<uint8_t*>(4);s.scaled_background_size=4000*(i+1);
 }
 audit.capture();assert(audit.available&&takes==1&&gives==1&&!test_locked);
#ifdef USE_LVGL_DIRECT_REGION_EXPECTED_GENERATION
 assert(test_logs.size()==4);
 for(size_t i=0;i<4;i++){
  const auto&line=test_logs[i];
  assert(line.find("slot="+std::to_string(i)+" valid=0 copy_ready=0")!=std::string::npos);
  assert(line.find("persistent_b="+std::to_string(1000*(i+1)))!=std::string::npos);
  assert(line.find("scaled_b="+std::to_string(4000*(i+1)))!=std::string::npos);
 }
 test_logs.clear();audit.direct_region_slots_[0].valid=true;
 audit.direct_region_slots_[0].persistent_copy_valid=true;
 audit.direct_region_slots_[1].persistent_copy=nullptr; // stale capacity cannot claim allocation
 audit.capture();assert(test_logs.size()==4);
 assert(test_logs[0].find("valid=1 copy_ready=1 persistent_b=1000")!=std::string::npos);
 assert(test_logs[1].find("persistent_b=0")!=std::string::npos);
 test_logs.clear();test_lock_ok=false;audit.capture();assert(!audit.available&&!test_locked);
 assert(test_logs.size()==1&&test_logs[0]=="phase capacity_audit=unavailable");
#else
 assert(test_logs.empty());
#endif
}
"""
        )
        with tempfile.TemporaryDirectory(prefix="region-capacity-host-") as directory:
            root = Path(directory)
            cpp = root / "audit.cpp"
            cpp.write_text(harness)
            for enabled in (False, True):
                with self.subTest(feature_enabled=enabled):
                    executable = root / f"audit-{enabled}.exe"
                    command = [
                        *compiler_command(),
                        "-std=c++20",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-DUSE_LVGL_PPA",
                        str(cpp),
                        "-o",
                        str(executable),
                    ]
                    if enabled:
                        command.append("-DUSE_LVGL_DIRECT_REGION_EXPECTED_GENERATION")
                    result = subprocess.run(
                        command,
                        capture_output=True,
                        text=True,
                        timeout=120,
                        check=False,
                    )
                    self.assertEqual(
                        0, result.returncode, result.stdout + result.stderr
                    )
                    result = subprocess.run(
                        [str(executable)],
                        capture_output=True,
                        text=True,
                        timeout=10,
                        check=False,
                    )
                    self.assertEqual(
                        0, result.returncode, result.stdout + result.stderr
                    )


if __name__ == "__main__":
    unittest.main()

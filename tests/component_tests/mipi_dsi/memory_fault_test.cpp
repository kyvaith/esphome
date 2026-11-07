#include "memory_fault_test_support.h"
#include "esphome/components/mipi_dsi/memory_fault_diagnostics.cpp"
#include <sstream>
#include <vector>

using namespace esphome::mipi_dsi;

static std::vector<uint32_t> values(const std::string &line) {
  std::istringstream stream(line);
  std::string prefix;
  stream >> prefix;
  assert(prefix == "DSIMEM1");
  std::vector<uint32_t> result;
  uint32_t word;
  while (stream >> std::hex >> word)
    result.push_back(word);
  assert(result.size() == 25);
  assert(line.size() == 234);
  return result;
}

int main() {
  emit_memory_fault_diagnostics();
  assert(test_output.empty() && test_reads == 0);
  note_memory_fault_from_isr(100);
  assert(test_output.empty() && test_reads == 21);
  test_register_bias = 0x1000;
  note_memory_fault_from_isr(101);
  assert(test_reads == 21);  // First event remains intact, second is coalesced.
  test_during_output = []() { note_memory_fault_from_isr(102); };
  emit_memory_fault_diagnostics();
  auto first = values(test_output);
  assert(first[0] == 1 && first[1] == 100 && first[2] == 1 && first[3] == 1);
  for (uint32_t i = 4; i < 25; ++i)
    assert(first[i] == i - 3);
  assert(test_reads == 42 && test_lock_depth == 0);
  test_output.clear();
  emit_memory_fault_diagnostics();
  auto second = values(test_output);
  assert(second[0] == 3 && second[1] == 102 && second[2] == 1 && second[3] == 0);
  for (uint32_t i = 4; i < 25; ++i)
    assert(second[i] == 0x1000 + i - 3);
  test_output.clear();
  emit_memory_fault_diagnostics();
  assert(test_output.empty() && test_lock_depth == 0);
}

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_NO_MEM = 257;
constexpr uint32_t MALLOC_CAP_SPIRAM = 1;
constexpr uint32_t MALLOC_CAP_8BIT = 2;
constexpr uint32_t MALLOC_CAP_SPIRAM_NO_ENC = 4;
constexpr uint32_t OUTPUT_CAPS = TEST_NO_ENC ? MALLOC_CAP_SPIRAM_NO_ENC : MALLOC_CAP_SPIRAM;
static constexpr const char *TAG = "test";

std::array<uint8_t, 1048448> scratch_storage;
std::array<uint8_t, 1048448> moved_storage;
std::array<uint8_t, 200000> copy_storage;
std::array<uint8_t, 32> previous_storage;
bool scratch_live = true, moved_live = false, copy_live = false, previous_live = true;
unsigned scratch_frees = 0, moved_frees = 0, copy_frees = 0, previous_frees = 0;
unsigned allocation_calls = 0, realloc_calls = 0, recovered_logs = 0, terminal_logs = 0;
bool first_copy_success = false, second_copy_success = false, shrink_success = true, shrink_moves = false;
size_t realloc_size = 0;
uint32_t realloc_caps = 0;

void heap_caps_free(void *ptr) {
  if (ptr == nullptr)
    return;
  if (ptr == scratch_storage.data()) {
    assert(scratch_live);
    scratch_live = false;
    scratch_frees++;
  } else if (ptr == moved_storage.data()) {
    assert(moved_live);
    moved_live = false;
    moved_frees++;
  } else if (ptr == copy_storage.data()) {
    assert(copy_live);
    copy_live = false;
    copy_frees++;
  } else {
    assert(ptr == previous_storage.data() && previous_live);
    previous_live = false;
    previous_frees++;
  }
}

void *heap_caps_malloc(size_t size, uint32_t caps) {
  allocation_calls++;
  assert(caps == (allocation_calls == 1 ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT : MALLOC_CAP_8BIT));
  if ((allocation_calls == 1 && first_copy_success) || (allocation_calls == 2 && second_copy_success)) {
    assert(size <= copy_storage.size());
    copy_live = true;
    return copy_storage.data();
  }
  return nullptr;
}

void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps) {
  assert(ptr == scratch_storage.data() && scratch_live);
  realloc_calls++;
  realloc_size = size;
  realloc_caps = caps;
  if (!shrink_success)
    return nullptr;  // The original allocation remains live on failure.
  if (!shrink_moves)
    return ptr;
  moved_live = true;
  std::memcpy(moved_storage.data(), scratch_storage.data(), size);
  heap_caps_free(ptr);
  return moved_storage.data();
}

class JpegBuffer {
 public:
  ~JpegBuffer() { heap_caps_free(data); }
  void reset(uint8_t *pointer, size_t length, size_t capacity) {
    heap_caps_free(this->data);
    this->data = pointer;
    this->length = length;
    this->capacity = capacity;
  }
  uint8_t *data{previous_storage.data()};
  size_t length{previous_storage.size()};
  size_t capacity{previous_storage.size()};
};

void log_no_mem(const char *stage, size_t, size_t, size_t, size_t, size_t, size_t) {
  assert(std::strcmp(stage, "owned_copy") == 0);
  terminal_logs++;
}
template<typename... Args> void recovered_log(const char *, const char *, Args...) { recovered_logs++; }
#define ESP_LOGW(...) recovered_log(__VA_ARGS__)

esp_err_t publish_completed_output(uint8_t *output_data, size_t output_capacity, uint32_t encoded_size,
                                   bool owns_output, JpegBuffer *output) {
  constexpr size_t input_capacity = 1920000;
  // This include is extracted from the complete, actual production publication
  // branch by pytest. Only allocator/logging/JpegBuffer ownership are faked.
#include "encoder_publication_under_test.inc"
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const unsigned test = std::strtoul(argv[1], nullptr, 10);
  assert(test < 10);
  for (size_t i = 0; i < scratch_storage.size(); i++)
    scratch_storage[i] = static_cast<uint8_t>((i * 13 + 7) % 251);
  const bool owned = test != 5 && test != 9;
  const uint32_t encoded = test == 7 ? 1 : test == 8 ? scratch_storage.size() : test == 6 ? 153606 : 148995;
  first_copy_success = test == 0 || test == 9;
  second_copy_success = test == 1;
  shrink_moves = test == 3;
  shrink_success = test != 4;
  {
    JpegBuffer output;
    const auto result =
        publish_completed_output(scratch_storage.data(), scratch_storage.size(), encoded, owned, &output);
    const bool success = test != 4 && test != 5;
    assert(result == (success ? ESP_OK : ESP_ERR_NO_MEM));
    if (success) {
      assert(output.length == encoded && output.capacity == encoded);
      for (size_t i = 0; i < encoded; i++)
        assert(output.data[i] == static_cast<uint8_t>((i * 13 + 7) % 251));
      assert(previous_frees == 1);
      if (test == 0 || test == 1 || test == 9) {
        assert(output.data == copy_storage.data());
        assert(realloc_calls == 0 && recovered_logs == 0);
        assert(scratch_frees == static_cast<unsigned>(owned));
      } else {
        assert(output.data == (shrink_moves ? moved_storage.data() : scratch_storage.data()));
        assert(realloc_calls == 1 && realloc_size == encoded);
        assert(realloc_caps == (OUTPUT_CAPS | MALLOC_CAP_8BIT));
        assert(recovered_logs == 1 && terminal_logs == 0);
        assert(scratch_frees == static_cast<unsigned>(shrink_moves));
      }
    } else {
      assert(output.data == previous_storage.data() && output.length == previous_storage.size());
      assert(previous_frees == 0 && terminal_logs == 1 && recovered_logs == 0);
      assert(realloc_calls == (test == 4 ? 1U : 0U));
      assert(scratch_frees == static_cast<unsigned>(owned));
    }
  }
  assert(previous_frees == 1);  // The previous output is retired exactly once.
  if (owned)
    assert(scratch_frees == 1);  // No free after transfer until output teardown.
  else {
    assert(scratch_frees == 0 && scratch_live);  // Borrowed/global owner untouched.
    heap_caps_free(scratch_storage.data());
  }
  assert(!scratch_live && !moved_live && !copy_live && !previous_live);
  std::printf("case %u PASS\n", test);
}

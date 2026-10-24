#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define USE_LVGL_PPA
struct lv_color_t { uint8_t blue, green, red; };
struct LottieContext {
  uint32_t render_width{}, render_height{};
  int32_t start_frame{0}, end_frame{360};
  lv_color_t fast_radial_fill_color{0xff, 0xe2, 0xae};
  lv_color_t fast_radial_outline_color{0x0d, 0x0d, 0x0d};
  uint8_t *fast_radial_buffer_ptr[2]{};
  int32_t fast_radial_last_frame[2]{-1, -1};
  int16_t fast_radial_dirty_x1{}, fast_radial_dirty_y1{}, fast_radial_dirty_x2{}, fast_radial_dirty_y2{};
};
unsigned stride_alignment = 1;
uint32_t lv_draw_buf_width_to_stride(uint32_t width, int) {
  const auto bytes = width * 4;
  return (bytes + stride_alignment - 1) / stride_alignment * stride_alignment;
}
int lottie_argb_color_format(const LottieContext *) { return 0; }
namespace before {
uint64_t cleared = 0;
void *counted_memset(void *target, int value, size_t bytes) {
  cleared += bytes;
  return std::memset(target, value, bytes);
}
#include "radial_before.inc"
}
namespace after {
uint64_t cleared = 0;
void *counted_memset(void *target, int value, size_t bytes) {
  cleared += bytes;
  return std::memset(target, value, bytes);
}
#include "radial_after.inc"
}

constexpr uint32_t SENTINEL = 0xa5a5a5a5;
constexpr size_t GUARD = 128;
uint64_t frames_checked = 0, pixels_checked = 0;
void require(bool value) { if (!value) std::abort(); }

struct FrameBuffer {
  std::vector<uint32_t> storage;
  size_t pixels;
  explicit FrameBuffer(size_t count) : storage(count + 2 * GUARD, SENTINEL), pixels(count) {}
  uint32_t *data() { return storage.data() + GUARD; }
  bool guard_ok() const {
    return std::all_of(storage.begin(), storage.begin() + GUARD, [](uint32_t x) { return x == SENTINEL; }) &&
           std::all_of(storage.end() - GUARD, storage.end(), [](uint32_t x) { return x == SENTINEL; });
  }
};

std::vector<uint32_t> circle_reference(const LottieContext &ctx, uint32_t stride) {
  std::vector<uint32_t> pixels(stride * ctx.render_height, 0);
  const float scale = float(std::min(ctx.render_width, ctx.render_height)) / 128.0f;
  const float center_x = (float(ctx.render_width) - 1.0f) * 0.5f;
  const float center_y = (float(ctx.render_height) - 1.0f) * 0.5f;
  const float inner = 26.0f * scale;
  const float outer = inner + 3.5f * scale;
  before::LottieFastRadialPalette palette{};
  before::lottie_fast_radial_prepare_palette(&palette,
      before::lottie_fast_radial_opaque_color(ctx.fast_radial_outline_color),
      before::lottie_fast_radial_opaque_color(ctx.fast_radial_fill_color));
  before::lottie_fast_radial_draw_circle(pixels.data(), stride, ctx.render_width, ctx.render_height,
      center_x, center_y, outer,
      before::lottie_fast_radial_make_coverage(before::lottie_fast_radial_q8(inner)),
      before::lottie_fast_radial_make_coverage(before::lottie_fast_radial_q8(outer)), palette);
  return pixels;
}

struct Result {
  unsigned width, height, alignment, frame_span;
  std::string sequence;
  uint64_t frames{}, different_pixels{}, baseline_core_errors{}, candidate_core_errors{};
  int first_frame{-1}, first_x{-1}, first_y{-1};
  uint64_t old_clear{}, new_clear{};
};

Result check(unsigned width, unsigned height, const std::string &sequence, unsigned alignment, unsigned frame_span) {
  stride_alignment = alignment;
  const unsigned stride = lv_draw_buf_width_to_stride(width, 0) / 4;
  const size_t count = size_t(stride) * height;
  LottieContext old, now;
  old.render_width = now.render_width = width;
  old.render_height = now.render_height = height;
  old.end_frame = now.end_frame = frame_span;
  FrameBuffer old0(count), old1(count), now0(count), now1(count);
  FrameBuffer *old_buffers[]{&old0, &old1}, *now_buffers[]{&now0, &now1};
  for (int index = 0; index < 2; index++) {
    old.fast_radial_buffer_ptr[index] = reinterpret_cast<uint8_t *>(old_buffers[index]->data());
    now.fast_radial_buffer_ptr[index] = reinterpret_cast<uint8_t *>(now_buffers[index]->data());
  }
  auto core = circle_reference(old, stride);
  const auto old_bytes = before::cleared, new_bytes = after::cleared;
  Result result{width, height, alignment, frame_span, sequence};
  const unsigned iterations = sequence == "wrapped" ? 2 * frame_span : frame_span;
  for (unsigned index = 0; index < iterations; index++) {
    int32_t frame = index % frame_span;
    if (sequence == "reverse") frame = frame_span - 1 - frame;
    if (sequence == "skipped") frame = (index * 73) % frame_span;
    if (sequence == "clamped") frame = index < 90 ? int(index) - 100 : int(index) + 20;
    if (sequence == "restart_recolor" && index % 41 == 0) {
      old.fast_radial_last_frame[0] = old.fast_radial_last_frame[1] = -1;
      now.fast_radial_last_frame[0] = now.fast_radial_last_frame[1] = -1;
      if (index % 82 == 0) {
        old.fast_radial_fill_color = now.fast_radial_fill_color = {uint8_t(index), 0x71, 0xe3};
        old.fast_radial_outline_color = now.fast_radial_outline_color = {0x11, uint8_t(index), 0x13};
        core = circle_reference(old, stride);
      }
    }
    const unsigned buffer = index % 2;
    auto *old_pixels = old_buffers[buffer]->data();
    auto *now_pixels = now_buffers[buffer]->data();
    require(before::lottie_render_fast_radial_frame(&old, frame, reinterpret_cast<uint8_t *>(old_pixels)));
    require(after::lottie_render_fast_radial_frame(&now, frame, reinterpret_cast<uint8_t *>(now_pixels)));
    require(old_buffers[buffer]->guard_ok() && now_buffers[buffer]->guard_ok());
    require(old.fast_radial_last_frame[buffer] == now.fast_radial_last_frame[buffer]);
    require(old.fast_radial_dirty_x1 == now.fast_radial_dirty_x1 &&
            old.fast_radial_dirty_y1 == now.fast_radial_dirty_y1 &&
            old.fast_radial_dirty_x2 == now.fast_radial_dirty_x2 &&
            old.fast_radial_dirty_y2 == now.fast_radial_dirty_y2);
    for (size_t pixel = 0; pixel < count; pixel++) {
      if (old_pixels[pixel] != now_pixels[pixel]) {
        result.different_pixels++;
        if (result.first_frame == -1) {
          result.first_frame = frame; result.first_x = pixel % stride; result.first_y = pixel / stride;
        }
      }
      if (core[pixel] != 0) {
        if (old_pixels[pixel] != core[pixel]) result.baseline_core_errors++;
        if (now_pixels[pixel] != core[pixel]) result.candidate_core_errors++;
      }
    }
    frames_checked++;
    pixels_checked += count;
    result.frames++;
  }
  result.old_clear = before::cleared - old_bytes;
  result.new_clear = after::cleared - new_bytes;
  return result;
}

int main() {
  std::vector<Result> results;
  const unsigned geometry[][2]{{192,192},{384,384},{193,197},{383,385},{192,385},{385,192},{97,133},{64,64}};
  for (unsigned frame_span : {309U, 360U})
    for (const auto &size : geometry)
      for (const auto &sequence : {"sequential", "reverse", "skipped", "wrapped", "restart_recolor", "clamped"})
        results.push_back(check(size[0], size[1], sequence, 1, frame_span));
  results.push_back(check(193, 197, "wrapped", 64, 360));
  for (unsigned width : {512U, 768U, 1024U})
    results.push_back(check(width, width, "sequential", 1, 360));
  uint64_t mismatches = 0, core_errors = 0, introduced_core_errors = 0;
  std::printf("{\"frames_checked\":%llu,\"pixels_compared\":%llu,\"results\":[", frames_checked, pixels_checked);
  for (size_t index = 0; index < results.size(); index++) {
    const auto &row = results[index];
    mismatches += row.different_pixels;
    core_errors += row.candidate_core_errors;
    if (row.candidate_core_errors > row.baseline_core_errors)
      introduced_core_errors += row.candidate_core_errors - row.baseline_core_errors;
    std::printf("%s{\"width\":%u,\"height\":%u,\"alignment\":%u,\"frame_span\":%u,"
                "\"sequence\":\"%s\",\"frames\":%llu,"
                "\"different_pixels\":%llu,\"baseline_core_errors\":%llu,\"candidate_core_errors\":%llu,"
                "\"first_frame\":%d,\"first_x\":%d,\"first_y\":%d,\"baseline_clear_bytes\":%llu,"
                "\"candidate_clear_bytes\":%llu}",
        index ? "," : "", row.width, row.height, row.alignment, row.frame_span, row.sequence.c_str(), row.frames,
        row.different_pixels, row.baseline_core_errors, row.candidate_core_errors,
        row.first_frame, row.first_x, row.first_y,
        row.old_clear, row.new_clear);
  }
  std::printf("],\"different_pixels\":%llu,\"candidate_core_errors\":%llu,\"introduced_core_errors\":%llu}\n",
              mismatches, core_errors, introduced_core_errors);
  return mismatches == 0 && introduced_core_errors == 0 ? 0 : 1;
}

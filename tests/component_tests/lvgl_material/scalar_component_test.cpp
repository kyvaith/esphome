#include "scalar_test_support.h"
#include "component_fixture.h"
#include "material_direct_scalar_control.h"
#include <iostream>

using namespace esphome::lvgl_material;

struct Scalar : MaterialDirectScalarControl {
  using MaterialDirectScalarControl::MaterialDirectScalarControl;
  void run() {
    this->stop_.store(false);
    const auto hook = test_notify_hook;
    test_notify_hook = [this, hook]() {
      if (hook)
        hook();
      // Model a real worker blocked after a current failed completion. Production
      // stays alive until the owner handles that completion or wakes new work.
      if (!this->active_.load() ||
          (!this->worker_busy_.load() && this->completion_sequence_.load() != 0 && !this->completed_success_.load() &&
           this->completed_id_.load() == this->requested_id_.load()))
        this->stop_.store(true);
    };
    this->render_worker_();
    test_notify_hook = hook;
  }
  void stop() { this->stop_.store(true); }
  uint32_t completion_sequence() const { return this->completion_sequence_.load(std::memory_order_acquire); }
  uint32_t completion_base() const { return this->completed_base_.load(std::memory_order_acquire); }
  bool completion_accepted() const { return this->completed_success_.load(std::memory_order_acquire); }
  // Worker-only motion seam; actual Component pointer dispatch is tested below.
  void request(uint32_t id, bool known, uint8_t value) {
    this->publish_request_(id, known, value, false, this->lvgl_->get_direct_region_base_generation());
  }
  bool request_ready() const {
    Request request;
    return this->read_request_(request);
  }
  void own_region() { this->region_owned_.store(true); }
  void set_borrowed(bool value) {
    this->source_in_flight_.store(value);
    this->worker_busy_.store(value);
  }
  bool alias() const { return this->alias_visible_; }
  bool owns_pause() const { return this->pause_owned_; }
  uint8_t *canvas() const { return this->canvas_; }
  const ScalarRasterStyle &style() const { return this->style_; }
  const uint8_t *masks() const { return this->glyph_masks_; }
};

struct ScalarFixture {
  Fixture entity;
  esphome::lvgl::LvglNavigation navigation;
  esphome::lvgl::LvglApplication application;
  lv_obj_t region, track, bubble, value, minimum, maximum;
  Scalar scalar{&this->entity.lvgl, &this->entity.component, &this->navigation, &this->application};
  explicit ScalarFixture(bool enabled = true) {
    test_notify_hook = {};
    test_delay_hook = {};
    test_refresh_hook = {};
    test_missing_glyph = false;
    this->region.bounds = {118, 448, 681, 559};
    this->track.bounds = {142, 512, 657, 535};
    this->bubble.bounds = {344, 452, 455, 495};
    this->value.bounds = {356, 460, 443, 487};
    this->minimum.bounds = {142, 540, 237, 558};
    this->minimum.text = "0%";
    this->maximum.bounds = {562, 540, 657, 558};
    this->maximum.text = "100%";
    this->navigation.active = &this->application;
    this->scalar.set_widgets(&this->region, &this->track, &this->bubble, &this->value, &this->minimum, &this->maximum);
    this->scalar.set_active_color(0x3498FF);
    this->scalar.set_initial_enabled(enabled);
    this->scalar.setup();
    if (enabled)
      assert(!this->scalar.is_failed());
  }
  void open(bool known = true) {
    this->entity.grant(known);
    assert(this->scalar.resume(this->entity.component.get_selection_generation()));
    assert(!this->entity.component.owner().is_scalar_ready());
  }
  void render_once() {
    test_delay_hook = [this]() { this->scalar.stop(); };
    this->scalar.run();
    test_delay_hook = {};
    this->scalar.loop();
  }
};

static uint64_t checksum(const uint8_t *data, size_t length) {
  uint64_t value = 0;
  for (size_t i = 0; i < length; i++)
    value = value * 131 + data[i];
  return value;
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const int scenario = std::stoi(argv[1]);
  if (scenario == 0) {
    const auto before = test_allocations;
    ScalarFixture f(false);
    assert(test_allocations == before && f.scalar.canvas_capacity() == 0 && f.navigation.guard == nullptr);
    assert(!f.scalar.resume(1));
  } else if (scenario == 1) {
    ScalarFixture f;
    assert(f.scalar.canvas_capacity() == 189568 && f.scalar.cached_glyph_bytes() <= Scalar::GLYPH_BYTES);
    assert(f.navigation.guard == &f.scalar && test_allocations == 3);
    assert(f.entity.commands() == 0);
  } else if (scenario == 2) {
    ScalarFixture f;
    f.open();
    f.render_once();
    assert(f.entity.component.owner().is_scalar_ready());
    assert(f.entity.commands() == 0 && f.entity.lvgl.submissions == 1 && f.scalar.rendered_count() == 1);
  } else if (scenario == 3) {
    ScalarFixture f;
    f.open();
    f.entity.lvgl.submit_result = LVGL_DIRECT_BLIT_BUSY;
    size_t waits = 0;
    test_delay_hook = [&]() {
      if (++waits == 1) {
        f.scalar.request(20, true, 75);
        f.entity.lvgl.submit_result = LVGL_DIRECT_BLIT_SUBMITTED;
      } else
        f.scalar.stop();
    };
    f.scalar.run();
    assert(f.entity.lvgl.submissions == 2 && f.scalar.rendered_count() == 1);
    std::vector<uint8_t> expected(f.scalar.canvas_capacity());
    assert(scalar_raster_rgb888(expected.data(), expected.size(), f.scalar.style(), f.scalar.masks(),
                                f.scalar.cached_glyph_bytes(), true, 75));
    assert(std::memcmp(expected.data(), f.scalar.canvas(), 564 * 112 * 3) == 0);
  } else if (scenario == 4) {
    ScalarFixture f;
    f.open();
    f.entity.lvgl.immediate_retire = false;
    uint64_t borrowed = 0;
    size_t renders = 0, waits = 0;
    f.entity.lvgl.on_submit = [&]() {
      borrowed = checksum(f.entity.lvgl.borrowed, f.entity.lvgl.borrowed_bytes);
      renders++;
    };
    test_notify_hook = [&]() {
      if (f.entity.lvgl.callback) {
        f.scalar.request(20, true, 75);
        f.scalar.request(21, true, 100);
        assert(checksum(f.entity.lvgl.borrowed, f.entity.lvgl.borrowed_bytes) == borrowed);
        f.entity.lvgl.immediate_retire = true;
        f.entity.lvgl.retire();
      }
    };
    test_delay_hook = [&]() {
      if (++waits == 2)
        f.scalar.stop();
    };
    const auto allocations = test_allocations;
    f.scalar.run();
    assert(renders == 2 && test_allocations == allocations && f.entity.commands() == 0);
  } else if (scenario == 5) {
    ScalarFixture f;
    f.open();
    f.entity.lvgl.generation++;
    f.scalar.run();
    f.scalar.loop();
    assert(!f.entity.component.owner().is_scalar_ready() && !f.entity.component.owner().is_view_ready() &&
           f.entity.commands() == 0);
    assert(f.entity.lvgl.callback == nullptr);
  } else if (scenario == 6) {
    ScalarFixture f;
    f.open();
    f.entity.lvgl.queue_carry = false;
    f.scalar.run();
    f.scalar.loop();
    assert(!f.entity.component.owner().is_scalar_ready() && f.entity.commands() == 0);
  } else if (scenario == 7) {
    ScalarFixture f;
    f.open();
    test_delay_hook = [&]() {
      f.entity.component.invalidate(ControlReason::CONTROL_REASON_NAVIGATION);
      f.scalar.stop();
    };
    f.scalar.run();
    f.scalar.loop();
    assert(!f.entity.component.owner().has_selection() && !f.entity.component.owner().is_scalar_ready());
  } else if (scenario == 8) {
    ScalarFixture f;
    f.open();
    f.scalar.set_borrowed(true);
    const auto *canvas = f.scalar.canvas();
    assert(!f.scalar.suspend_for_snapshot(3));
    assert(f.scalar.canvas() == canvas && f.entity.lvgl.captures == 0 && f.entity.lvgl.releases == 0);
    assert(!f.scalar.is_quiescent());
  } else if (scenario == 9) {
    ScalarFixture f;
    f.scalar.own_region();
    f.entity.lvgl.pause_wait_ok = false;
    assert(!f.scalar.suspend_for_snapshot());
    assert(f.scalar.owns_pause() && f.entity.lvgl.pause_depth == 1 && f.entity.lvgl.captures == 0);
  } else if (scenario == 10) {
    ScalarFixture f;
    f.scalar.own_region();
    f.entity.lvgl.pause_depth = 1;
    assert(!f.scalar.suspend_for_snapshot());
    assert(f.entity.lvgl.pause_depth == 2 && f.entity.lvgl.handoffs == 0 && f.entity.lvgl.captures == 0);
    f.entity.lvgl.pause_depth--;
    assert(f.scalar.suspend_for_snapshot());
    assert(f.entity.lvgl.pause_depth == 0 && !f.scalar.owns_pause() && f.scalar.is_quiescent());
  } else if (scenario == 11) {
    ScalarFixture f;
    f.scalar.own_region();
    f.entity.lvgl.capture_ok = false;
    assert(!f.scalar.suspend_for_snapshot());
    assert(f.entity.lvgl.releases == 0 && f.scalar.owns_pause() && !f.scalar.alias());
    f.entity.lvgl.capture_ok = true;
    assert(f.scalar.suspend_for_snapshot() && f.scalar.alias());
    assert(f.scalar.canvas()[0] == 0xA5 && f.entity.lvgl.pause_depth == 0);
  } else if (scenario == 12) {
    ScalarFixture f;
    f.scalar.own_region();
    f.entity.lvgl.release_ok = false;
    assert(!f.scalar.suspend_for_snapshot());
    assert(f.scalar.owns_pause() && !f.scalar.alias() && !f.scalar.is_quiescent());
  } else if (scenario == 13) {
    ScalarFixture f;
    f.scalar.own_region();
    f.entity.lvgl.consume_handoff_ok = false;
    assert(!f.scalar.suspend_for_snapshot());
    assert(!f.scalar.owns_pause() && f.scalar.alias() && !f.scalar.is_quiescent());
    // Prior failed coherence must never become success via the empty fast path.
    assert(!f.scalar.suspend_for_snapshot());
    f.entity.lvgl.consume_handoff_ok = true;
    assert(f.scalar.suspend_for_snapshot() && f.scalar.is_quiescent());
  } else if (scenario == 14) {
    ScalarFixture f;
    f.open();
    f.render_once();
    assert(f.scalar.suspend_for_snapshot() && f.scalar.alias());
    const auto drops = test_cache_drops, refreshes = test_refreshes;
    assert(f.scalar.resume(f.entity.component.get_selection_generation()));
    assert(!f.scalar.alias() && test_cache_drops > drops && test_refreshes > refreshes);
    assert(!f.entity.component.owner().is_scalar_ready());
  } else if (scenario == 15) {
    ScalarFixture f;
    f.open();
    f.scalar.own_region();
    f.entity.lvgl.capture_ok = false;
    assert(!f.scalar.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CLOSE, &f.application));
    assert(f.entity.component.owner().has_selection() && !f.entity.component.owner().is_view_ready() &&
           !f.scalar.is_quiescent());
    assert(f.entity.commands() == 0);
  } else if (scenario == 16) {
    ScalarFixture f;
    f.open(false);
    f.render_once();
    assert(f.entity.component.owner().is_scalar_ready() && !f.entity.component.owner().candidate_known());
    assert(f.entity.commands() == 0);
  } else if (scenario == 17) {
    ScalarFixture f;
    f.open();
    f.entity.lvgl.immediate_retire = false;
    test_notify_hook = [&]() {
      if (f.entity.lvgl.callback) {
        f.entity.lvgl.generation++;
        f.entity.lvgl.retire();
      }
    };
    f.scalar.run();
    f.scalar.loop();
    assert(!f.entity.component.owner().is_scalar_ready() && f.entity.commands() == 0);
  } else if (scenario == 18) {
    ScalarFixture f;
    f.scalar.own_region();
    f.entity.lvgl.pause_start_ok = false;
    assert(!f.scalar.suspend_for_snapshot());
    assert(!f.scalar.owns_pause() && f.entity.lvgl.pause_depth == 0 && !f.scalar.is_quiescent());
    assert(f.entity.lvgl.captures == 0 && f.entity.lvgl.releases == 0);
    f.entity.lvgl.pause_start_ok = true;
    assert(f.scalar.suspend_for_snapshot() && f.scalar.is_quiescent());
  } else if (scenario == 19) {
    ScalarFixture f;
    f.open();
    f.render_once();
    const auto generation = f.entity.component.get_selection_generation();
    assert(f.scalar.suspend_for_direct_overlay());
    f.scalar.loop();
    assert(!f.entity.component.owner().is_view_ready());
    assert(!f.scalar.resume(generation));
    f.scalar.resume_after_direct_overlay();
    assert(f.entity.component.owner().is_view_ready() && !f.entity.component.owner().is_scalar_ready());
    assert(f.entity.commands() == 0);
  } else if (scenario == 20) {
    ScalarFixture f;
    f.open();
    f.entity.lvgl.queue_carry = false;
    f.scalar.run();
    assert(f.scalar.suspend_for_direct_overlay());
    f.scalar.loop();
    assert(!f.entity.component.owner().is_view_ready() && !f.entity.component.owner().is_scalar_ready());
    assert(!f.scalar.resume(f.entity.component.get_selection_generation()));
    assert(f.entity.commands() == 0);
  } else if (scenario == 21) {
    ScalarFixture f;
    f.open();
    f.entity.lvgl.queue_carry = false;
    f.scalar.run();
    assert(f.scalar.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CAPTURE, nullptr));
    f.scalar.loop();
    assert(!f.entity.component.owner().is_view_ready() && !f.entity.component.owner().is_scalar_ready());
    assert(f.entity.commands() == 0);
  } else if (scenario == 22) {
    ScalarFixture f;
    const auto &s = f.scalar.style();
    const size_t pixels = static_cast<size_t>(s.region.width) * s.region.height * 3;
    std::vector<uint8_t> target(pixels + 32, 0xCD);
    assert(
        scalar_raster_rgb888(target.data() + 16, pixels, s, f.scalar.masks(), f.scalar.cached_glyph_bytes(), false, 0));
    assert(target[16] == s.background.blue && target[17] == s.background.green && target[18] == s.background.red);
    const size_t center =
        16 + (static_cast<size_t>(s.track.y + s.track.height / 2) * s.region.width + s.track.x + s.track.width / 2) * 3;
    assert(target[center] == s.inactive.blue);
    assert(
        scalar_raster_rgb888(target.data() + 16, pixels, s, f.scalar.masks(), f.scalar.cached_glyph_bytes(), true, 50));
    assert(target[center] == s.active.blue);
    for (size_t i = 0; i < 16; i++)
      assert(target[i] == 0xCD && target[target.size() - 1 - i] == 0xCD);
    assert(!scalar_raster_rgb888(target.data() + 16, pixels - 1, s, f.scalar.masks(), f.scalar.cached_glyph_bytes(),
                                 true, 50));
    assert(!scalar_raster_rgb888(target.data() + 16, pixels, s, f.scalar.masks(), f.scalar.cached_glyph_bytes(), true,
                                 101));
  } else if (scenario == 23) {
    ScalarFixture f;
    f.open();
    f.render_once();
    f.entity.receive(f.entity.state_message(75));
    assert(!f.scalar.request_ready());
    const auto before = f.entity.lvgl.generation;
    test_refresh_hook = [&]() { f.entity.lvgl.generation++; };
    f.scalar.loop();
    test_refresh_hook = {};
    assert(f.entity.lvgl.generation > before && f.scalar.request_ready());
    f.render_once();
    assert(f.entity.component.owner().is_scalar_ready() && f.entity.commands() == 0);
  } else if (scenario == 24) {
    ScalarFixture f;
    f.open();
    f.entity.lvgl.immediate_retire = false;
    test_notify_hook = [&]() {
      if (f.entity.lvgl.callback) {
        f.entity.receive(f.entity.state_message(75));
        f.entity.lvgl.generation++;
        f.entity.lvgl.retire();
      } else if (!f.scalar.request_ready())
        f.scalar.stop();
    };
    f.scalar.run();
    test_notify_hook = {};
    f.scalar.loop();
    assert(f.entity.component.owner().is_view_ready() && f.scalar.request_ready());
    f.entity.lvgl.immediate_retire = true;
    f.render_once();
    assert(f.entity.component.owner().is_scalar_ready() && f.entity.commands() == 0);
  } else if (scenario == 25) {
    ScalarFixture f;
    f.open();
    f.render_once();
    f.entity.press(50);
    const auto refreshes = test_refreshes, writes = label_writes;
    for (int value = 50; value <= 100; value++)
      f.entity.move(value);
    f.scalar.loop();
    assert(test_refreshes == refreshes && label_writes == writes && f.entity.commands() == 0);
    f.entity.release(100);
    assert(f.entity.commands() == 1 && !f.scalar.request_ready());
    f.scalar.loop();
    assert(test_refreshes > refreshes && f.scalar.request_ready());
  } else if (scenario == 26) {
    ScalarFixture f;
    f.entity.grant();
    const auto generation = f.entity.component.get_selection_generation();
    assert(f.entity.component.set_view_ready(generation, false));
    f.navigation.transition = true;
    assert(!f.scalar.resume(generation) && !f.entity.component.owner().is_view_ready());
    assert(f.entity.lvgl.submissions == 0 && f.entity.commands() == 0);
  } else if (scenario == 27) {
    // A newer boundary arrives after old failure publication but before the
    // owner task observes it. The old completion cannot close the new view.
    ScalarFixture f;
    f.open();
    f.entity.lvgl.queue_carry = false;
    bool replaced = false;
    test_notify_hook = [&]() {
      if (!replaced && !f.scalar.request_ready())
        return;
      if (!replaced && f.entity.lvgl.submissions == 1 && f.entity.lvgl.callback == nullptr) {
        replaced = true;
        f.entity.receive(f.entity.state_message(75));
        f.scalar.stop();
      }
    };
    f.scalar.run();
    test_notify_hook = {};
    assert(replaced);
    f.scalar.loop();
    assert(f.entity.component.owner().is_view_ready() && f.scalar.request_ready());
    f.entity.lvgl.queue_carry = true;
    f.render_once();
    assert(f.entity.component.owner().is_scalar_ready() && f.entity.commands() == 0);
  } else if (scenario == 28) {
    ScalarFixture f;
    f.open();
    f.render_once();
    f.entity.receive(f.entity.state_message(75));
    assert(!f.scalar.request_ready());
    f.entity.press(75);
    f.entity.move(85);
    // Motion keeps the pending boundary fenced until its native flush happens.
    assert(!f.scalar.request_ready());
    const auto before = test_refreshes;
    f.scalar.loop();
    assert(test_refreshes == before + 1 && f.scalar.request_ready());
    f.render_once();
    assert(f.entity.component.owner().candidate() == 85);
    assert(f.entity.commands() == 0);
  } else if (scenario == 29) {
    ScalarFixture f;
    f.maximum.text_color = 0x112233;
    assert(!f.scalar.refresh_style(1));
    f.entity.grant();
    assert(!f.scalar.resume(f.entity.component.get_selection_generation()));
    assert(!f.entity.component.owner().is_scalar_ready() && f.entity.commands() == 0);
  } else if (scenario == 30) {
    ScalarFixture f;
    f.open();
    f.render_once();
    const auto generation = f.entity.component.get_selection_generation();
    assert(f.scalar.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CLOSE, &f.application));
    f.application.call_on_prepare_close_callbacks();
    f.navigation.transition = true;
    assert(f.entity.component.is_selection_current(generation) && !f.entity.component.owner().is_view_ready());
    f.application.call_on_close_cancelled_callbacks();
    f.scalar.loop();
    assert(!f.entity.component.owner().is_view_ready());
    f.navigation.transition = false;
    f.scalar.loop();
    assert(f.entity.component.owner().is_view_ready() && !f.entity.component.owner().is_scalar_ready());
    f.render_once();
    assert(f.entity.component.owner().is_scalar_ready() && f.entity.commands() == 0);
  } else if (scenario == 31) {
    ScalarFixture f;
    f.open();
    f.render_once();
    assert(f.scalar.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CLOSE, &f.application));
    f.application.call_on_prepare_close_callbacks();
    f.application.call_on_closed_callbacks();
    assert(!f.entity.component.owner().has_selection() && f.entity.commands() == 0);
  } else if (scenario == 32) {
    ScalarFixture f;
    f.open();
    f.render_once();
    assert(f.scalar.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CLOSE, &f.application));
    f.application.call_on_prepare_close_callbacks();
    const auto previous = f.entity.component.get_selection_generation();
    f.entity.grant();
    const auto replacement = f.entity.component.get_selection_generation();
    assert(replacement != previous);
    f.application.call_on_closed_callbacks();
    assert(f.entity.component.get_selection_generation() == replacement && f.entity.component.owner().has_selection());
    assert(f.entity.commands() == 0);
  } else if (scenario == 33) {
    ScalarFixture f;
    f.open();
    f.render_once();
    assert(f.scalar.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CLOSE, &f.application));
    f.application.call_on_prepare_close_callbacks();
    f.application.call_on_close_cancelled_callbacks();
    assert(f.scalar.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CAPTURE, nullptr));
    f.scalar.loop();
    assert(!f.entity.component.owner().is_view_ready() && f.entity.commands() == 0);
  } else if (scenario == 34) {
    ScalarFixture f;
    f.open();
    f.render_once();
    assert(f.scalar.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CLOSE, &f.application));
    f.application.call_on_prepare_close_callbacks();
    f.entity.grant();
    const auto replacement = f.entity.component.get_selection_generation();
    assert(f.entity.component.set_view_ready(replacement, false));
    f.application.call_on_close_cancelled_callbacks();
    f.scalar.loop();
    assert(f.entity.component.is_selection_current(replacement) && !f.entity.component.owner().is_view_ready());
    assert(f.entity.commands() == 0);
  } else if (scenario == 35) {
    ScalarFixture f;
    f.open();
    f.render_once();
    assert(f.scalar.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CLOSE, &f.application));
    f.application.call_on_prepare_close_callbacks();
    f.navigation.transition = true;
    f.application.call_on_close_cancelled_callbacks();
    test_now += 30001;
    f.entity.component.loop();
    f.navigation.transition = false;
    f.scalar.loop();
    assert(!f.entity.component.owner().has_selection() && !f.entity.component.owner().is_view_ready());
    assert(f.entity.commands() == 0);
  } else if (scenario == 36) {
    ScalarFixture disabled(false);
    assert(disabled.scalar.memory_usage_bytes() == 0);
    ScalarFixture f;
    assert(f.scalar.memory_usage_bytes() == 189568 + 49152 + 8192);
    assert(f.scalar.cached_glyph_bytes() < 49152);
    f.scalar.log_memory_usage("setup");
    f.open();
    f.render_once();
    assert(f.scalar.suspend_for_snapshot());
    assert(f.scalar.memory_usage_bytes() == 189568 + 49152 + 8192);
    f.scalar.log_memory_usage("suspended");
    f.scalar.on_shutdown();
    assert(f.scalar.memory_usage_bytes() == 8192);
    f.scalar.log_memory_usage("shutdown-retained-stack");
    assert(f.entity.commands() == 0);
  } else if (scenario == 37) {
    ScalarFixture f;
    f.open();
    const uint32_t base = f.entity.lvgl.get_direct_region_base_generation();
    // Publish and retire a real accepted worker frame without consuming its completion.
    test_delay_hook = [&]() { f.scalar.stop(); };
    f.scalar.run();
    test_delay_hook = {};
    assert(f.scalar.completion_sequence() != 0 && (f.scalar.completion_sequence() & 1U) == 0);
    assert(f.scalar.completion_accepted() && f.scalar.completion_base() == base);
    assert(f.entity.lvgl.submissions == 1 && f.scalar.rendered_count() == 1);
    assert(f.entity.component.owner().is_view_ready() && !f.entity.component.owner().is_scalar_ready());
    // An external base change before the owner consumes that frame must still close both gates.
    ++f.entity.lvgl.generation;
    f.scalar.loop();
    assert(!f.entity.component.owner().is_view_ready() && !f.entity.component.owner().is_scalar_ready());
    test_event(f.entity.on, LV_EVENT_PRESSED, &f.entity.pointer);
    test_event(f.entity.on, LV_EVENT_RELEASED, &f.entity.pointer);
    test_event(f.entity.off, LV_EVENT_PRESSED, &f.entity.pointer);
    test_event(f.entity.off, LV_EVENT_RELEASED, &f.entity.pointer);
    f.entity.press(20);
    f.entity.move(95);
    f.entity.release(95);
    f.entity.component.loop();
    f.scalar.loop();
    assert(!f.entity.component.owner().is_view_ready() && !f.entity.component.owner().is_scalar_ready());
    assert(f.entity.commands() == 0 && f.entity.lvgl.submissions == 1);
  } else if (scenario == 38) {
    ScalarFixture f;
    f.open();
    f.render_once();
    const auto previous = f.entity.component.get_selection_generation();
    assert(f.scalar.suspend_for_direct_overlay());
    f.entity.grant();
    const auto replacement = f.entity.component.get_selection_generation();
    assert(replacement != previous);
    assert(f.entity.component.set_view_ready(replacement, false));
    f.scalar.resume_after_direct_overlay();
    f.scalar.loop();
    assert(f.entity.component.is_selection_current(replacement));
    assert(!f.entity.component.owner().is_view_ready() && !f.entity.component.owner().is_scalar_ready());
    assert(f.entity.commands() == 0 && f.entity.lvgl.submissions == 1);
  } else if (scenario == 39) {
    ScalarFixture f;
    f.open();
    f.render_once();
    Scalar disabled(&f.entity.lvgl, &f.entity.component, &f.navigation, &f.application);
    disabled.setup();
    assert(f.navigation.guard == &f.scalar);
    assert(f.scalar.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CLOSE, &f.application));
    f.application.call_on_prepare_close_callbacks();
    f.navigation.transition = true;
    f.application.call_on_close_cancelled_callbacks();
    disabled.loop();
    assert(!f.entity.component.owner().is_view_ready());
    f.navigation.transition = false;
    disabled.loop();
    assert(!f.entity.component.owner().is_view_ready());
    f.scalar.loop();
    assert(f.entity.component.owner().is_view_ready() && !f.entity.component.owner().is_scalar_ready());
    f.render_once();
    assert(f.entity.component.owner().is_scalar_ready());
    assert(f.entity.commands() == 0 && f.entity.lvgl.submissions == 2);
  } else {
    return 2;
  }
  std::cout << "scalar case " << scenario << " PASS\n";
}

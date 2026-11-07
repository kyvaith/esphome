#include "navigation_test_support.h"
#include "lvgl_navigation.h"
#include <iostream>

using namespace esphome::lvgl;
struct Guard : NavigationAdmissionGuard {
  bool allow{true};
  size_t calls{0};
  std::function<void()> during;
  bool prepare_navigation(NavigationAdmissionKind, LvglApplication *) override {
    calls++;
    if (during)
      during();
    return allow;
  }
};
struct Navigation : LvglNavigation {
  using LvglNavigation::LvglNavigation;
  bool external() const { return this->external_touch_capture_; }
};
struct Fixture {
  LvglComponent lvgl;
  Navigation nav{&lvgl};
  LvglSnapshotCompositor compositor;
  LvglApplication app;
  LvPageType host;
  lv_obj_t home, root;
  Guard guard;
  size_t prepared{0}, closed{0};
  Fixture(bool snapshots = true) {
    host.index = 0;
    host.obj = &home;
    nav.set_home_widget_page(&host);
    nav.add_home_widget(&home);
    app.set_page(&host);
    app.set_widget(&root);
    nav.add_application(&app);
    app.add_on_prepare_close_callback([this]() { prepared++; });
    app.add_on_closed_callback([this]() { closed++; });
    if (snapshots)
      nav.set_snapshot_compositor(&compositor);
#ifdef USE_LVGL_NAVIGATION_ADMISSION_GUARD
    assert(nav.add_admission_guard(&guard));
#endif
    nav.set_swipe_start_distance(0);
    nav.set_axis_bias(0);
  }
  void open() {
    assert(nav.try_open_application(&app));
    assert(nav.is_application_open(&app));
  }
};

int main(int argc, char **argv) {
  assert(argc == 2);
  const int scenario = std::stoi(argv[1]);
  if (scenario == 0) {
    Fixture f;
    f.open();
    assert(f.prepared == 0);
    assert(f.nav.try_show_home());
    assert(f.prepared == 1 && f.closed == 1);
    const std::vector<LvglApplication *> subset{&f.app};
    assert(f.nav.try_prepare_application_snapshots(subset));
    assert(f.compositor.captured_applications == subset);
    std::cout << "navigation sizeof=" << sizeof(Navigation) << "\n";
#ifdef USE_LVGL_NAVIGATION_ADMISSION_GUARD
  } else if (scenario == 1) {
    Fixture f;
    assert(f.nav.try_prepare_application_snapshots());
    assert(f.nav.last_application_snapshot_preparation_succeeded());
    const auto captures = f.compositor.prepares;
    f.guard.allow = false;
    assert(!f.nav.try_prepare_application_snapshots());
    assert(!f.nav.last_application_snapshot_preparation_succeeded() && f.compositor.prepares == captures);
    f.guard.allow = true;
    assert(f.nav.try_prepare_application_snapshots());
    f.guard.allow = false;
    f.nav.prepare_application_snapshots();
    assert(!f.nav.last_application_snapshot_preparation_succeeded() && f.compositor.prepares == captures + 1);
  } else if (scenario == 2) {
    Fixture f;
    f.nav.set_external_touch_capture(true);
    f.guard.allow = false;
    const auto resets = test_indev_resets, cancels = f.compositor.cancels;
    assert(!f.nav.try_open_application(&f.app));
    assert(f.nav.external() && test_indev_resets == resets && f.compositor.cancels == cancels);
    assert(!f.nav.is_application_open(&f.app));
  } else if (scenario == 3) {
    Fixture f;
    f.open();
    f.guard.allow = false;
    assert(!f.nav.try_close_application() && !f.nav.try_show_home());
    assert(f.prepared == 0 && f.closed == 0 && !f.app.is_close_prepared());
    assert(f.nav.is_application_open(&f.app) && f.compositor.begins == 0);
  } else if (scenario == 4) {
    Fixture f;
    f.open();
    f.guard.allow = false;
    f.nav.touch_begin(400, 790);
    assert(f.nav.touch_update(400, 760));
    f.guard.allow = true;
    assert(f.nav.touch_end());
    assert(f.prepared == 0 && f.closed == 0 && f.compositor.begins == 0 && f.nav.is_application_open(&f.app));
  } else if (scenario == 5) {
    Fixture f;
    f.open();
    f.app.set_close_on_threshold(true);
    f.guard.allow = false;
    f.nav.touch_begin(400, 790);
    assert(f.nav.touch_update(400, 100));
    f.guard.allow = true;
    assert(f.nav.touch_end());
    f.nav.loop();
    assert(f.prepared == 0 && f.closed == 0 && f.nav.is_application_open(&f.app));
  } else if (scenario == 6) {
    Fixture f(false);
    f.open();
    f.guard.allow = false;
    f.nav.touch_begin(400, 790);
    assert(f.nav.touch_update(400, 100));
    assert(f.nav.touch_end());
    assert(f.prepared == 0 && !f.app.is_close_prepared() && f.nav.is_application_open(&f.app));
  } else if (scenario == 7) {
    Fixture f;
    f.compositor.home_active = true;
    f.guard.allow = false;
    f.nav.touch_begin(300, 300);
    assert(f.nav.should_defer_press(&f.home));
    assert(f.nav.touch_end());
    assert(f.compositor.takeovers == 0 && f.compositor.primes == 0);
  } else if (scenario == 8) {
    Fixture f;
    f.guard.allow = false;
    f.nav.touch_begin(300, 300);
    assert(f.nav.should_defer_press(&f.home));
    assert(f.nav.touch_end());
    assert(f.compositor.primes == 0);
  } else if (scenario == 9) {
    Fixture f;
    f.open();
    f.compositor.opening = true;
    f.guard.allow = false;
    assert(!f.nav.try_close_application());
    f.guard.allow = true;
    f.compositor.opening = false;
    f.nav.loop();
    assert(f.nav.is_application_open(&f.app) && f.prepared == 0);
  } else if (scenario == 10) {
    Fixture f;
    f.guard.allow = false;
    assert(!f.nav.try_refresh_home() && f.compositor.prepares == 0);
  } else if (scenario == 11) {
    Fixture f;
    LvglApplication other;
    lv_obj_t other_root;
    other.set_page(&f.host);
    other.set_widget(&other_root);
    f.nav.add_application(&other);
    const std::vector<LvglApplication *> subset{&f.app};
    assert(f.nav.try_prepare_application_snapshots(subset));
    assert(f.compositor.captured_applications == subset && f.compositor.prepares == 1);
    assert(f.guard.calls == 1 && f.nav.last_application_snapshot_preparation_succeeded());
    f.compositor.prepare_result = false;
    assert(!f.nav.try_prepare_application_snapshots(subset));
    assert(!f.nav.last_application_snapshot_preparation_succeeded() && f.compositor.prepares == 2);
  } else if (scenario == 12) {
    Fixture f;
    const std::vector<LvglApplication *> subset{&f.app};
    f.guard.allow = false;
    assert(!f.nav.try_prepare_application_snapshots(subset));
    assert(f.guard.calls == 1 && f.compositor.prepares == 0);
    f.guard.allow = true;
    f.compositor.home_active = true;
    assert(!f.nav.try_prepare_application_snapshots(subset));
    f.compositor.home_active = false;
    f.compositor.application_active = true;
    assert(!f.nav.try_prepare_application_snapshots(subset));
    f.compositor.application_active = false;
    f.nav.touch_begin(300, 300);
    assert(!f.nav.try_prepare_application_snapshots(subset));
    f.nav.touch_cancel();
    f.nav.set_external_touch_capture(true);
    assert(!f.nav.try_prepare_application_snapshots(subset));
    assert(f.compositor.prepares == 0);
  } else if (scenario == 13) {
    Fixture f;
    LvglApplication unknown, incomplete;
    lv_obj_t other_root;
    unknown.set_page(&f.host);
    unknown.set_widget(&other_root);
    f.nav.add_application(&incomplete);
    for (const auto &subset : std::vector<std::vector<LvglApplication *>>{
             {}, {nullptr}, {&unknown}, {&incomplete}, {&f.app, &f.app}})
      assert(!f.nav.try_prepare_application_snapshots(subset));
    assert(f.guard.calls == 0 && f.compositor.prepares == 0);
  } else if (scenario == 14) {
    Fixture f;
    LvglApplication other;
    lv_obj_t other_root;
    other.set_page(&f.host);
    other.set_widget(&other_root);
    f.nav.add_application(&other);
    f.open();
    const auto calls = f.guard.calls;
    assert(!f.nav.try_prepare_application_snapshots({&other}));
    assert(f.guard.calls == calls && f.compositor.prepares == 0);
    assert(f.nav.try_prepare_application_snapshots({&f.app}));
    assert(f.nav.is_application_open(&f.app) && f.prepared == 0 && f.closed == 0);
    assert(f.compositor.captured_applications == std::vector<LvglApplication *>{&f.app});
  } else if (scenario == 15) {
    Fixture f(false);
    assert(!f.nav.try_prepare_application_snapshots({&f.app}));
    assert(f.guard.calls == 0 && !f.nav.last_application_snapshot_preparation_succeeded());
  } else if (scenario == 16) {
    Fixture f;
    f.guard.during = [&f]() { f.compositor.home_active = true; };
    assert(!f.nav.try_prepare_application_snapshots({&f.app}));
    assert(f.guard.calls == 1 && f.compositor.prepares == 0);
  } else if (scenario == 17) {
    Fixture f;
    f.guard.during = [&f]() { f.app.set_widget(nullptr); };
    assert(!f.nav.try_prepare_application_snapshots({&f.app}));
    assert(f.guard.calls == 1 && f.compositor.prepares == 0);
    assert(!f.nav.last_application_snapshot_preparation_succeeded());
  } else if (scenario == 18) {
    Fixture f;
    f.guard.during = [&f]() { f.app.set_page(nullptr); };
    assert(!f.nav.try_prepare_application_snapshots({&f.app}));
    assert(f.guard.calls == 1 && f.compositor.prepares == 0);
  } else if (scenario == 19) {
    Fixture f;
    std::vector<LvglApplication *> subset{&f.app};
    f.guard.during = [&]() { subset.push_back(&f.app); };
    assert(!f.nav.try_prepare_application_snapshots(subset));
    assert(f.guard.calls == 1 && f.compositor.prepares == 0);
  } else if (scenario == 20) {
    Fixture f;
    LvglApplication other;
    lv_obj_t other_root;
    other.set_page(&f.host);
    other.set_widget(&other_root);
    f.nav.add_application(&other);
    f.open();
    const auto calls = f.guard.calls;
    std::vector<LvglApplication *> subset{&f.app};
    f.guard.during = [&]() { subset.front() = &other; };
    assert(!f.nav.try_prepare_application_snapshots(subset));
    assert(f.guard.calls == calls + 1 && f.compositor.prepares == 0);
    assert(f.nav.is_application_open(&f.app) && !f.nav.last_application_snapshot_preparation_succeeded());
#endif
  } else
    return 2;
  std::cout << "navigation case " << scenario << " PASS\n";
}

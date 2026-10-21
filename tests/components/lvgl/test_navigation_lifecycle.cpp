#include <gtest/gtest.h>

#include "esphome/components/lvgl/lvgl_navigation.h"
#include "esphome/components/lvgl/lvgl_snapshot_compositor.h"

#if LV_USE_SNAPSHOT && LV_USE_IMAGE

namespace esphome::lvgl::testing {

class LifecycleNavigation : public LvglNavigation {
 public:
  LifecycleNavigation() : LvglNavigation(nullptr) {}
  void set_active_application(LvglApplication *application) { this->active_application_ = application; }
  bool has_pending_close() const { return this->close_deferred_; }
};

// Exercise the real navigation lifecycle without creating a framebuffer or
// running an animation clock. Only the display compositor is replaced.
class LifecycleCompositor : public LvglSnapshotCompositor {
 public:
  LifecycleCompositor() : LvglSnapshotCompositor(nullptr, nullptr) {}
  void begin_open(LvglApplication *application) {
    this->application_ = application;
    this->application_active_ = true;
    this->application_opening_ = true;
    this->application_close_committed_ = false;
  }
  void loop() override {
    if (!this->application_active_)
      return;
    if (this->application_opening_ && this->finish_opening) {
      this->navigation_->complete_application_transition(this->application_, true, false);
      this->application_active_ = false;
    } else if (this->application_close_committed_ && this->finish_closing) {
      this->navigation_->complete_application_transition(this->application_, false, true);
      this->application_active_ = false;
    }
  }
  bool begin_application_close(LvglApplication *application, int home_index) override {
    this->application_ = application;
    this->application_active_ = true;
    this->application_opening_ = false;
    this->application_close_committed_ = false;
    return true;
  }
  bool settle_application_close(bool close) override {
    this->application_close_committed_ = close;
    return true;
  }
  void cancel_application() override { this->application_active_ = false; }

  bool finish_opening{true};
  bool finish_closing{true};
};

TEST(NavigationLifecycle, EarlyRepeatedClosePairsOpeningAndClosingLeases) {
  LifecycleNavigation navigation;
  LifecycleCompositor compositor;
  LvglApplication application;
  navigation.set_snapshot_compositor(&compositor);
  navigation.set_active_application(&application);
  compositor.begin_open(&application);

  int leases = 1;  // Acquired by the opening preparation callback.
  int opened = 0;
  int preparing_close = 0;
  int closed = 0;
  application.add_on_opened_callback([&]() {
    opened++;
    leases--;
  });
  application.add_on_prepare_close_callback([&]() {
    preparing_close++;
    leases++;
  });
  application.add_on_closed_callback([&]() {
    closed++;
    leases--;
  });

  navigation.close_application();
  navigation.close_application();
  EXPECT_TRUE(navigation.has_pending_close());
  EXPECT_EQ(preparing_close, 0);
  EXPECT_EQ(leases, 1);

  navigation.loop();
  EXPECT_EQ(opened, 1);
  EXPECT_EQ(preparing_close, 0);
  EXPECT_EQ(leases, 0);
  EXPECT_TRUE(navigation.has_pending_close());

  navigation.loop();
  EXPECT_EQ(preparing_close, 1);
  EXPECT_EQ(leases, 1);
  navigation.close_application();  // A committed close must not acquire twice.
  EXPECT_EQ(preparing_close, 1);

  navigation.loop();
  EXPECT_EQ(opened, 1);
  EXPECT_EQ(closed, 1);
  EXPECT_EQ(leases, 0);
  EXPECT_FALSE(navigation.has_pending_close());
}

TEST(NavigationLifecycle, HomeRequestDuringOpeningUsesDeferredClose) {
  LifecycleNavigation navigation;
  LifecycleCompositor compositor;
  LvglApplication application;
  navigation.set_snapshot_compositor(&compositor);
  navigation.set_active_application(&application);
  compositor.begin_open(&application);

  navigation.show_home();
  EXPECT_TRUE(navigation.has_pending_close());
  EXPECT_TRUE(compositor.is_application_opening());
}

TEST(NavigationLifecycle, CancelledOpeningDoesNotCloseReplacementApplication) {
  LifecycleNavigation navigation;
  LifecycleCompositor compositor;
  LvglApplication original;
  LvglApplication replacement;
  navigation.set_snapshot_compositor(&compositor);
  navigation.set_active_application(&original);
  compositor.begin_open(&original);
  int replacement_closes = 0;
  replacement.add_on_prepare_close_callback([&]() { replacement_closes++; });

  navigation.close_application();
  compositor.cancel_application();
  navigation.set_active_application(&replacement);
  navigation.loop();
  EXPECT_EQ(replacement_closes, 0);
  EXPECT_FALSE(navigation.has_pending_close());
}

}  // namespace esphome::lvgl::testing

#endif

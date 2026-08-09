#pragma once

#include <cstdint>

namespace esphome::lvgl {

struct DirectSceneFrame {
  const uint8_t *data{nullptr};
  int stride{0};
  int width{0};
  int height{0};
};

/** Coordinates exclusive full-screen presentation with direct overlays.
 *
 * Direct scene renderers can own the DSI framebuffers outside LVGL's normal
 * draw loop. A full-screen overlay must suspend those producers before it
 * captures or modifies the presented frame, then resume only the producers it
 * actually suspended.
 */
class DirectSceneController {
 public:
  virtual ~DirectSceneController() = default;

  virtual bool suspend_for_direct_overlay() = 0;
  virtual void resume_after_direct_overlay() = 0;

  /** Return the exact RGB888 frame preserved while the scene is suspended.
   *
   * Most direct renderers can simply return false. A renderer which already
   * captured the presented frame for its handoff can lend that immutable
   * surface to the overlay and avoid a second full-screen PSRAM allocation.
   * The frame remains owned by the scene controller and is valid only until
   * resume_after_direct_overlay() is called.
   */
  virtual bool get_direct_overlay_frame(DirectSceneFrame &frame) const {
    frame = {};
    return false;
  }

  /** Temporarily expose the clean scene source for a small off-screen render.
   *
   * A direct scene can preserve native LVGL overlays in its frozen DSI frame.
   * Full-screen overlays use this hook to reconstruct an area hidden by their
   * activation widget without baking that widget back into the background.
   */
  virtual bool begin_direct_overlay_background_render() { return false; }
  virtual void end_direct_overlay_background_render() {}
};

}  // namespace esphome::lvgl

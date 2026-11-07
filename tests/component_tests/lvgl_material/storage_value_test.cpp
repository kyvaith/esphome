#include "material_entity_control.h"
#include <cassert>
#include <type_traits>

using namespace esphome::lvgl_material;
static_assert(std::is_trivially_copyable_v<ControlSnapshot>);
static_assert(std::is_trivially_destructible_v<ControlSnapshot>);
static_assert(std::is_nothrow_default_constructible_v<ControlSnapshot>);
static_assert(std::is_nothrow_copy_constructible_v<ControlSnapshot>);
static_assert(std::is_nothrow_copy_assignable_v<ControlSnapshot>);
static_assert(std::is_nothrow_move_constructible_v<ControlSnapshot>);
static_assert(std::is_nothrow_move_assignable_v<ControlSnapshot>);

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
template<typename Family> void check_family(ControlSnapshot &state) {
  state.clear_family();
  assert(state.has_no_family());
  assert(state.family_if<Family>() == nullptr);
  const auto &view = state;
  assert(view.family_if<Family>() == nullptr && state.has_no_family());
  const auto &family = state.emplace_family<Family>();
  assert(family == Family{});
  assert(state.family_if<Family>() == &family && view.family_if<Family>() == &family);
  const ControlSnapshot saved = state;
  assert(saved == state);
  state.clear_family();
  assert(state != saved);
  state = saved;
  assert(state == saved);
  state.emplace_family<ControlNumberSnapshot>().index = 123;
  assert(state.emplace_family<Family>() == Family{});
  assert(state.title.view() == "Common title");
  assert(state.on && state.brightness_known && state.brightness_pct == 71);
}
#endif

int main() {
  ControlSnapshot state;
  state.title.assign("Common title");
  state.on = state.brightness_known = true;
  state.brightness_pct = 71;
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  check_family<ControlNumberSnapshot>(state);
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  check_family<ControlSnapshot::Choice>(state);
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  check_family<ControlCoverSnapshot>(state);
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  check_family<ControlSnapshot::Fan>(state);
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  check_family<ControlSnapshot::Humidifier>(state);
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  check_family<ControlSnapshot::Robot>(state);
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  check_family<ControlSnapshot::Panel>(state);
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  check_family<ControlDetailSnapshot>(state);
  auto &detail = state.emplace_family<ControlDetailSnapshot>();
  assert(!state.matches_family(ControlEntityKind::CONTROL_ENTITY_LIGHT));
  detail.present = true;
  assert(state.matches_family(ControlEntityKind::CONTROL_ENTITY_LIGHT));
  assert(!state.matches_family(ControlEntityKind::CONTROL_ENTITY_PANEL));
  detail.climate = true;
  assert(state.matches_family(ControlEntityKind::CONTROL_ENTITY_PANEL));
  assert(!state.matches_family(ControlEntityKind::CONTROL_ENTITY_LIGHT));
#endif
  state.clear_family();
  assert(state.matches_family(ControlEntityKind::CONTROL_ENTITY_LIGHT));
  assert(!state.matches_family(ControlEntityKind::CONTROL_ENTITY_NUMBER));
#endif
}

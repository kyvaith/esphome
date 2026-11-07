#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS

namespace esphome::lvgl_material {

uint8_t control_action_eligible(const ControlActionSnapshot &action) {
  if (!action.available())
    return 0;
  uint8_t eligible = 0;
  switch (action.type) {
    case 0:  // Button and input button are momentary, including first use.
    case 1:
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
    case ControlActionSnapshot::TILE_EVENT_TYPE:
#endif
      eligible = 1;
      break;
    case 2:
      eligible = 2;
      break;
    case 3:
      eligible = action.phase == 2 ? 8 : action.run_blocked == 0 ? 4 : 0;
      break;
    case 4:
      eligible = 64 | (action.phase == 4 ? 16 : 0) | (action.phase == 3 || action.stop_actions == 1 ? 32 : 0);
      break;
    default:
      break;
  }
  return eligible & action.operations;
}

bool control_action_capabilities_equal(const ControlActionSnapshot &first, const ControlActionSnapshot &second) {
  return first.type == second.type && first.operations == second.operations &&
         first.skip_condition == second.skip_condition && first.stop_actions == second.stop_actions &&
         first.run_blocked == second.run_blocked
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
         && first.parameters_version == second.parameters_version &&
         first.parameter_status == second.parameter_status && first.parameter_label == second.parameter_label &&
         first.parameter_default == second.parameter_default
#endif
      ;
}

bool control_action_command_state_equal(const ControlActionSnapshot &first, const ControlActionSnapshot &second) {
  // Last-use text and current count are telemetry, not a held command's meaning.
  return first.source == second.source && first.phase == second.phase;
}

bool control_valid_action_snapshot(const ControlSnapshot &snapshot) {
  constexpr uint8_t MASKS[] = {1,
                               1,
                               2,
                               12,
                               112
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
                               ,
                               1
#endif
  };
  const auto *action = snapshot.family_if<ControlActionSnapshot>();
  if (action == nullptr || !control_valid_light_fields(snapshot) || action->type >= std::size(MASKS) ||
      action->source > 2 || action->phase > 5 || action->state_generation == 0 ||
      action->state_generation > CONTROL_MAX_SEQUENCE || action->last_used_status > 3 || action->run_blocked > 3 ||
      !number_plain_utf8(action->last_used.view()) ||
      ((action->last_used_status == 0) != !action->last_used.view().empty()) ||
      (!action->current_known && action->current != 0))
    return false;
  if ((action->operations & ~MASKS[action->type]) != 0 || action->eligible != control_action_eligible(*action))
    return false;
  if (action->type == 4) {
    if (action->skip_condition > 1 || action->stop_actions > 1)
      return false;
  } else if (action->skip_condition != ControlActionSnapshot::UNKNOWN_BOOL ||
             action->stop_actions != ControlActionSnapshot::UNKNOWN_BOOL) {
    return false;
  }
  if (action->type == 3) {
    // Registered Run remains visible when metadata conservatively blocks it.
    if (((action->operations & 4U) == 0) != (action->run_blocked == 3))
      return false;
  } else if (action->run_blocked != 0) {
    return false;
  }
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  if (action->parameters_version > 1 || action->parameter_status > 2 ||
      (action->parameter_default > 1 && action->parameter_default != ControlActionSnapshot::UNKNOWN_BOOL))
    return false;
  if (action->parameters_version == 1) {
    if (action->type != 3 || !number_plain_utf8(action->parameter_label.view()) ||
        (action->parameter_status == 0 ? action->parameter_label.view().empty()
                                       : (!action->parameter_label.view().empty() ||
                                          action->parameter_default != ControlActionSnapshot::UNKNOWN_BOOL)) ||
        action->run_blocked != ((action->operations & 4U) == 0 ? 3 : action->parameter_status))
      return false;
  } else if (action->parameter_status != 0 || !action->parameter_label.view().empty() ||
             action->parameter_default != ControlActionSnapshot::UNKNOWN_BOOL) {
    return false;
  }
#endif
  const bool stateless = action->type <= 2 || action->type == ControlActionSnapshot::TILE_EVENT_TYPE;
  if (stateless && action->current_known)
    return false;
  if (!action->available())
    return action->phase == 5 && action->last_used_status == 1 && !action->current_known;
  if (stateless)
    return action->phase == 0;
  return action->type == 3 ? action->phase == 1 || action->phase == 2 : action->phase == 3 || action->phase == 4;
}

}  // namespace esphome::lvgl_material
#endif

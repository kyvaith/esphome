#ifdef USE_LVGL_MATERIAL_ROBOT_TILE_ROUTING
if (test >= 132) {
  assert(test <= 141);
  surface.set_control_press_enabled(true);
  surface.set_number_controls_enabled(true);
  surface.set_robot_controls_enabled(true);
  auto robot_payload = control_payload;
  robot_payload.replace(robot_payload.find("\"light\""), 7, "\"robot\"");
  auto number_payload = control_payload;
  number_payload.replace(number_payload.find("\"light\""), 7, "\"number\"");
  int typed = 0;
  uint8_t kind = 255;
  if (test != 133)
    surface.add_on_entity_control_press_callback([&](uint8_t slot, const char *id, uint64_t revision, uint8_t type) {
      assert(slot == 2 && std::string_view(id) == "card-2" && revision == 7);
      typed++;
      kind = type;
    });
  assert(surface.configure(test == 134 ? control_payload : robot_payload));
  surface.mark_slots_ready(4);
  if (test == 132 || test == 133) {
    click();
    assert(typed == (test == 132 ? 1 : 0));
    assert(test != 132 || kind == 6);
  } else if (test == 134 || test == 135) {
    surface.event(0, LV_EVENT_PRESSED);
    assert(surface.configure(test == 134 ? robot_payload : number_payload));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
    click();
    assert(typed == 1 && kind == (test == 134 ? 6 : 1));
  } else if (test == 136) {
    surface.event(0, LV_EVENT_PRESSED);
    surface.set_robot_controls_enabled(false);
    surface.set_robot_controls_enabled(true);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
    click();
    assert(typed == 1 && kind == 6);
  } else if (test == 137) {
    std::string exported;
    assert(surface.serialize_presentation(exported));
    assert(exported.find("\"control_kind\":\"robot\"") != std::string::npos);
    assert(surface.configure(exported));
    assert(surface.get_pending_slots() == 0 && surface.get_pending_context_slots() == 0);
    click();
    assert(typed == 1 && kind == 6);
  } else if (test == 138 || test == 139) {
    std::string before, after;
    assert(surface.serialize_presentation(before));
    auto invalid = robot_payload;
    if (test == 138)
      surface.set_robot_controls_enabled(false);
    else
      invalid.replace(invalid.find("\"robot\""), 7, "\"climate\"");
    assert(!surface.configure(invalid));
    assert(surface.serialize_presentation(after) && before == after);
    assert(typed == 0);
  } else if (test == 140) {
    surface.set_require_snapshot_ready(false);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
  } else if (test == 141) {
    surface.event(0, LV_EVENT_PRESSED);
    surface.set_robot_controls_enabled(false);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
  }
  assert(controls == 0 && legacy == 0 && contextual == 0);
  std::printf("case %d PASS\n", test);
  return 0;
}
#endif

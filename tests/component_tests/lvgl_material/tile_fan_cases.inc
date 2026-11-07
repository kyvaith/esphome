#ifdef USE_LVGL_MATERIAL_FAN_TILE_ROUTING
if (test >= 112) {
  assert(test <= 121);
  surface.set_control_press_enabled(true);
  surface.set_number_controls_enabled(true);
  surface.set_fan_controls_enabled(true);
  auto fan_payload = control_payload;
  fan_payload.replace(fan_payload.find("\"light\""), 7, "\"fan\"");
  auto number_payload = control_payload;
  number_payload.replace(number_payload.find("\"light\""), 7, "\"number\"");
  int typed = 0;
  uint8_t kind = 255;
  if (test != 113)
    surface.add_on_entity_control_press_callback([&](uint8_t slot, const char *id, uint64_t revision, uint8_t type) {
      assert(slot == 2 && std::string_view(id) == "card-2" && revision == 7);
      typed++;
      kind = type;
    });
  assert(surface.configure(test == 114 ? control_payload : fan_payload));
  surface.mark_slots_ready(4);
  if (test == 112 || test == 113) {
    click();
    assert(typed == (test == 112 ? 1 : 0));
    assert(test != 112 || kind == 4);
  } else if (test == 114 || test == 115) {
    surface.event(0, LV_EVENT_PRESSED);
    assert(surface.configure(test == 114 ? fan_payload : number_payload));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
    click();
    assert(typed == 1 && kind == (test == 114 ? 4 : 1));
  } else if (test == 116) {
    surface.event(0, LV_EVENT_PRESSED);
    surface.set_fan_controls_enabled(false);
    surface.set_fan_controls_enabled(true);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
    click();
    assert(typed == 1 && kind == 4);
  } else if (test == 117) {
    std::string exported;
    assert(surface.serialize_presentation(exported));
    assert(exported.find("\"control_kind\":\"fan\"") != std::string::npos);
    assert(surface.configure(exported));
    assert(surface.get_pending_slots() == 0 && surface.get_pending_context_slots() == 0);
    click();
    assert(typed == 1 && kind == 4);
  } else if (test == 118 || test == 119) {
    std::string before, after;
    assert(surface.serialize_presentation(before));
    auto invalid = fan_payload;
    if (test == 118)
      surface.set_fan_controls_enabled(false);
    else
      invalid.replace(invalid.find("\"fan\""), 5, "\"climate\"");
    assert(!surface.configure(invalid));
    assert(surface.serialize_presentation(after) && before == after);
    assert(typed == 0);
  } else if (test == 120) {
    surface.set_require_snapshot_ready(false);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
  } else if (test == 121) {
    surface.event(0, LV_EVENT_PRESSED);
    surface.set_fan_controls_enabled(false);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
  }
  assert(controls == 0 && legacy == 0 && contextual == 0);
  std::printf("case %d PASS\n", test);
  return 0;
}
#endif

#ifdef USE_LVGL_MATERIAL_PANEL_TILE_ROUTING
if (test >= 142) {
  assert(test <= 151);
  surface.set_control_press_enabled(true);
  surface.set_number_controls_enabled(true);
  surface.set_panel_controls_enabled(true);
  auto panel_payload = control_payload;
  panel_payload.replace(panel_payload.find("\"light\""), 7, "\"panel\"");
  auto number_payload = control_payload;
  number_payload.replace(number_payload.find("\"light\""), 7, "\"number\"");
  int typed = 0;
  uint8_t kind = 255;
  if (test != 143)
    surface.add_on_entity_control_press_callback([&](uint8_t slot, const char *id, uint64_t revision, uint8_t type) {
      assert(slot == 2 && std::string_view(id) == "card-2" && revision == 7);
      typed++;
      kind = type;
    });
  assert(surface.configure(test == 144 ? control_payload : panel_payload));
  surface.mark_slots_ready(4);
  if (test == 142 || test == 143) {
    click();
    assert(typed == (test == 142 ? 1 : 0));
    assert(test != 142 || kind == 7);
  } else if (test == 144 || test == 145) {
    surface.event(0, LV_EVENT_PRESSED);
    assert(surface.configure(test == 144 ? panel_payload : number_payload));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
    click();
    assert(typed == 1 && kind == (test == 144 ? 7 : 1));
  } else if (test == 146) {
    surface.event(0, LV_EVENT_PRESSED);
    surface.set_panel_controls_enabled(false);
    surface.set_panel_controls_enabled(true);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
    click();
    assert(typed == 1 && kind == 7);
  } else if (test == 147) {
    std::string exported;
    assert(surface.serialize_presentation(exported));
    assert(exported.find("\"control_kind\":\"panel\"") != std::string::npos);
    assert(surface.configure(exported));
    assert(surface.get_pending_slots() == 0 && surface.get_pending_context_slots() == 0);
    click();
    assert(typed == 1 && kind == 7);
  } else if (test == 148 || test == 149) {
    std::string before, after;
    assert(surface.serialize_presentation(before));
    auto invalid = panel_payload;
    if (test == 148)
      surface.set_panel_controls_enabled(false);
    else
      invalid.replace(invalid.find("\"panel\""), 7, "\"climate\"");
    assert(!surface.configure(invalid));
    assert(surface.serialize_presentation(after) && before == after);
    assert(typed == 0);
  } else if (test == 150) {
    surface.set_require_snapshot_ready(false);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
  } else if (test == 151) {
    surface.event(0, LV_EVENT_PRESSED);
    surface.set_panel_controls_enabled(false);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
  }
  assert(controls == 0 && legacy == 0 && contextual == 0);
  std::printf("case %d PASS\n", test);
  return 0;
}
#endif

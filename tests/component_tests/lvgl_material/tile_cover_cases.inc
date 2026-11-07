#ifdef USE_LVGL_MATERIAL_COVER_TILE_ROUTING
if (test >= 102) {
  assert(test <= 111);
  surface.set_control_press_enabled(true);
  surface.set_number_controls_enabled(true);
  surface.set_cover_controls_enabled(true);
  auto cover_payload = control_payload;
  cover_payload.replace(cover_payload.find("\"light\""), 7, "\"cover\"");
  auto number_payload = control_payload;
  number_payload.replace(number_payload.find("\"light\""), 7, "\"number\"");
  int typed = 0;
  uint8_t kind = 255;
  if (test != 103)
    surface.add_on_entity_control_press_callback([&](uint8_t slot, const char *id, uint64_t revision, uint8_t type) {
      assert(slot == 2 && std::string_view(id) == "card-2" && revision == 7);
      typed++;
      kind = type;
    });
  assert(surface.configure(test == 104 ? control_payload : cover_payload));
  surface.mark_slots_ready(4);
  if (test == 102 || test == 103) {
    click();
    assert(typed == (test == 102 ? 1 : 0));
    assert(test != 102 || kind == 3);
  } else if (test == 104 || test == 105) {
    surface.event(0, LV_EVENT_PRESSED);
    assert(surface.configure(test == 104 ? cover_payload : number_payload));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
    click();
    assert(typed == 1 && kind == (test == 104 ? 3 : 1));
  } else if (test == 106) {
    surface.event(0, LV_EVENT_PRESSED);
    surface.set_cover_controls_enabled(false);
    surface.set_cover_controls_enabled(true);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
    click();
    assert(typed == 1 && kind == 3);
  } else if (test == 107) {
    std::string exported;
    assert(surface.serialize_presentation(exported));
    assert(exported.find("\"control_kind\":\"cover\"") != std::string::npos);
    assert(surface.configure(exported));
    assert(surface.get_pending_slots() == 0 && surface.get_pending_context_slots() == 0);
    click();
    assert(typed == 1 && kind == 3);
  } else if (test == 108 || test == 109) {
    std::string before, after;
    assert(surface.serialize_presentation(before));
    auto invalid = cover_payload;
    if (test == 108)
      surface.set_cover_controls_enabled(false);
    else
      invalid.replace(invalid.find("\"cover\""), 7, "\"fan\"");
    assert(!surface.configure(invalid));
    assert(surface.serialize_presentation(after) && before == after);
    assert(typed == 0);
  } else if (test == 110) {
    surface.set_require_snapshot_ready(false);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
  } else if (test == 111) {
    surface.event(0, LV_EVENT_PRESSED);
    surface.set_cover_controls_enabled(false);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
  }
  assert(controls == 0 && legacy == 0 && contextual == 0);
  std::printf("case %d PASS\n", test);
  return 0;
}
#endif

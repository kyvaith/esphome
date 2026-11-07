#ifdef USE_LVGL_MATERIAL_CHOICE_TILE_ROUTING
if (test >= 92) {
  assert(test <= 101);
  surface.set_control_press_enabled(true);
  surface.set_number_controls_enabled(true);
  surface.set_choice_controls_enabled(true);
  auto choice_payload = control_payload;
  choice_payload.replace(choice_payload.find("\"light\""), 7, "\"choice\"");
  auto number_payload = control_payload;
  number_payload.replace(number_payload.find("\"light\""), 7, "\"number\"");
  int typed = 0;
  uint8_t kind = 255;
  if (test != 93)
    surface.add_on_entity_control_press_callback([&](uint8_t slot, const char *id, uint64_t revision, uint8_t type) {
      assert(slot == 2 && std::string_view(id) == "card-2" && revision == 7);
      typed++;
      kind = type;
    });
  assert(surface.configure(test == 94 ? control_payload : choice_payload));
  surface.mark_slots_ready(4);
  if (test == 92 || test == 93) {
    click();
    assert(typed == (test == 92 ? 1 : 0));
    assert(test != 92 || kind == 2);
  } else if (test == 94 || test == 95) {
    surface.event(0, LV_EVENT_PRESSED);
    assert(surface.configure(test == 94 ? choice_payload : number_payload));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
    click();
    assert(typed == 1 && kind == (test == 94 ? 2 : 1));
  } else if (test == 96) {
    surface.event(0, LV_EVENT_PRESSED);
    surface.set_choice_controls_enabled(false);
    surface.set_choice_controls_enabled(true);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
    click();
    assert(typed == 1 && kind == 2);
  } else if (test == 97) {
    std::string exported;
    assert(surface.serialize_presentation(exported));
    assert(exported.find("\"control_kind\":\"choice\"") != std::string::npos);
    assert(surface.configure(exported));
    assert(surface.get_pending_slots() == 0 && surface.get_pending_context_slots() == 0);
    click();
    assert(typed == 1 && kind == 2);
  } else if (test == 98 || test == 99) {
    std::string before, after;
    assert(surface.serialize_presentation(before));
    auto invalid = choice_payload;
    if (test == 98)
      surface.set_choice_controls_enabled(false);
    else
      invalid.replace(invalid.find("\"choice\""), 8, "\"fan\"");
    assert(!surface.configure(invalid));
    assert(surface.serialize_presentation(after) && before == after);
    assert(typed == 0);
  } else if (test == 100) {
    surface.set_require_snapshot_ready(false);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
  } else if (test == 101) {
    surface.event(0, LV_EVENT_PRESSED);
    surface.set_choice_controls_enabled(false);
    surface.event(0, LV_EVENT_CLICKED);
    assert(typed == 0);
  }
  assert(controls == 0 && legacy == 0 && contextual == 0);
  std::printf("case %d PASS\n", test);
  return 0;
}
#endif

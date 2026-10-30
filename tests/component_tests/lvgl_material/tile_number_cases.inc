#ifdef USE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING
  if (test >= 83) {
    assert(test <= 91);
    surface.set_control_press_enabled(true);
    surface.set_number_controls_enabled(true);
    auto number_payload = control_payload;
    number_payload.replace(number_payload.find("\"light\""), 7, "\"number\"");
    int typed = 0;
    uint8_t kind = 255;
    if (test != 84)
      surface.add_on_entity_control_press_callback([&](uint8_t slot, const char *id, uint64_t revision, uint8_t type) {
        assert(slot == 2 && std::string_view(id) == "card-2" && revision == 7);
        typed++;
        kind = type;
      });
    if (test == 85 || test == 86)
      assert(surface.configure(control_payload));
    else
      assert(surface.configure(number_payload));
    surface.mark_slots_ready(4);
    if (test == 83) {
      click();
      assert(typed == 1 && kind == 1 && controls == 0 && legacy == 0 && contextual == 0);
    } else if (test == 84) {
      click();
      assert(typed == 0 && controls == 0 && legacy == 0 && contextual == 0);
    } else if (test == 85) {
      click();
      assert(typed == 1 && kind == 0 && controls == 0 && legacy == 0 && contextual == 0);
    } else if (test == 86) {
      surface.event(0, LV_EVENT_PRESSED);
      assert(surface.configure(number_payload));
      surface.mark_slots_ready(4);
      surface.event(0, LV_EVENT_CLICKED);
      assert(typed == 0 && controls == 0 && legacy == 0 && contextual == 0);
      click();
      assert(typed == 1 && kind == 1);
    } else if (test == 87) {
      surface.event(0, LV_EVENT_PRESSED);
      surface.set_number_controls_enabled(false);
      surface.set_number_controls_enabled(true);
      surface.event(0, LV_EVENT_CLICKED);
      assert(typed == 0 && controls == 0);
      click();
      assert(typed == 1 && kind == 1);
    } else if (test == 88) {
      std::string exported;
      assert(surface.serialize_presentation(exported));
      assert(exported.find("\"control_kind\":\"number\"") != std::string::npos);
      assert(surface.configure(exported));
      assert(surface.get_pending_slots() == 0 && surface.get_pending_context_slots() == 0);
      click();
      assert(typed == 1 && kind == 1 && controls == 0);
    } else if (test == 89 || test == 90) {
      std::string before, after;
      assert(surface.serialize_presentation(before));
      if (test == 89)
        surface.set_number_controls_enabled(false);
      auto invalid = number_payload;
      if (test == 90)
        invalid.replace(invalid.find("\"number\""), 8, "\"fan\"");
      assert(!surface.configure(invalid));
      assert(surface.serialize_presentation(after) && before == after);
      assert(typed == 0 && controls == 0 && legacy == 0 && contextual == 0);
    } else if (test == 91) {
      surface.set_require_snapshot_ready(false);
      surface.event(0, LV_EVENT_CLICKED);
      assert(typed == 0 && controls == 0 && legacy == 0 && contextual == 0);
    }
    std::printf("case %d PASS\n", test);
    return 0;
  }
#endif

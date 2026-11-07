if (test == 174) {
  surface.set_control_press_enabled(true);
  auto detail = control_payload;
  detail.replace(detail.find("\"control_kind\""), 14, "\"control_detail_version\":1,\"control_kind\"");
  assert(surface.configure(control_payload));
  surface.mark_slots_ready(4);
  std::string before, after;
  assert(surface.serialize_presentation(before));
  assert(!surface.configure(detail));
  assert(surface.serialize_presentation(after) && before == after);
  assert(controls == 0 && legacy == 0 && contextual == 0);
  std::printf("case %d PASS\n", test);
  return 0;
}
#ifdef USE_LVGL_MATERIAL_DETAIL_TILE_ROUTING
if (test >= 152) {
  assert(test <= 173);
  surface.set_control_press_enabled(true);
  surface.set_number_controls_enabled(true);
  surface.set_light_detail_controls_enabled(true);
  auto detail = control_payload;
  detail.replace(detail.find("\"control_kind\""), 14, "\"control_detail_version\":1,\"control_kind\"");
  int detailed = 0, typed = 0;
  surface.add_on_entity_control_press_callback([&](uint8_t, const char *, uint64_t, uint8_t kind) {
    assert(kind == 0);
    typed++;
  });
  if (test != 153)
    surface.add_on_entity_detail_control_press_callback([&](uint8_t slot, const char *id, uint64_t revision) {
      assert(slot == 2 && std::string_view(id) == "card-2" && revision == 7);
      detailed++;
    });
  assert(surface.configure(test == 155 ? control_payload : detail));
  if (test != 160)
    surface.mark_slots_ready(4);
  if (test == 152 || test == 153) {
    click();
    assert(detailed == (test == 152 ? 1 : 0));
  } else if (test == 154 || test >= 166) {
    std::string before, after;
    assert(surface.serialize_presentation(before));
    auto invalid = detail;
    if (test == 154)
      surface.set_light_detail_controls_enabled(false);
    else if (test <= 170 || test == 173) {
      const char *value = test == 166   ? "2"
                          : test == 167 ? "0"
                          : test == 168 ? "true"
                          : test == 169 ? "\"1\""
                          : test == 170 ? "null"
                                        : "1.0";
      const auto position = invalid.find("\"control_detail_version\":1") + 25;
      invalid.replace(position, 1, value);
    } else if (test == 171)
      invalid.replace(invalid.find("\"light\""), 7, "\"number\"");
    else
      invalid.erase(invalid.find("\"control_kind\""), 23);
    assert(!surface.configure(invalid));
    assert(surface.serialize_presentation(after) && before == after);
  } else if (test == 155 || test == 156 || test == 157) {
    surface.event(0, LV_EVENT_PRESSED);
    assert(surface.configure(test == 155 ? detail : control_payload));
    if (test == 157)
      assert(surface.configure(detail));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_CLICKED);
    assert(detailed == 0 && typed == 0);
    click();
    assert(detailed == (test == 156 ? 0 : 1) && typed == (test == 156 ? 1 : 0));
  } else if (test == 158 || test == 159) {
    surface.event(0, LV_EVENT_PRESSED);
    surface.set_light_detail_controls_enabled(false);
    if (test == 158)
      surface.set_light_detail_controls_enabled(true);
    surface.event(0, LV_EVENT_CLICKED);
    assert(detailed == 0);
    if (test == 158) {
      click();
      assert(detailed == 1);
    }
  } else if (test == 160) {
    click();
    assert(detailed == 0);
    surface.mark_slots_ready(4);
    click();
    assert(detailed == 1);
  } else if (test == 161) {
    std::string exported;
    assert(surface.serialize_presentation(exported));
    assert(exported.find("\"control_detail_version\":1") != std::string::npos);
    assert(surface.configure(exported));
    assert(surface.get_pending_slots() == 0 && surface.get_pending_context_slots() == 0);
    click();
    assert(detailed == 1);
  } else if (test == 162) {
    surface.event(0, LV_EVENT_PRESSED);
    detail.replace(detail.find("Lamp"), 4, "New title");
    assert(surface.configure(detail));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_CLICKED);
    assert(detailed == 1);
  } else if (test == 163) {
    surface.event(0, LV_EVENT_PRESSED);
    surface.event(0, LV_EVENT_PRESS_LOST);
    surface.event(0, LV_EVENT_CLICKED);
    assert(detailed == 0);
  } else if (test == 164) {
    surface.event(0, LV_EVENT_PRESSED);
    detail.replace(detail.find("revision\":7"), 11, "revision\":8");
    assert(surface.configure(detail));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_CLICKED);
    assert(detailed == 0);
  } else if (test == 165) {
    surface.set_require_snapshot_ready(false);
    surface.event(0, LV_EVENT_CLICKED);
    assert(detailed == 0);
  }
  assert(typed == (test == 156 ? 1 : 0));
  assert(controls == 0 && legacy == 0 && contextual == 0);
  std::printf("case %d PASS\n", test);
  return 0;
}
#endif

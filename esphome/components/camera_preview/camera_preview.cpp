#include "camera_preview.h"

#include "esphome/components/api/api_server.h"
#include "esphome/components/json/json_util.h"
#include "esphome/core/hal.h"

#include <algorithm>
#include <string_view>

namespace esphome::camera_preview {

static bool valid_key(std::string_view key) {
  return key.size() == 32 && std::all_of(key.begin(), key.end(),
                                         [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
}

void CameraPreview::setup() {
  this->application_->add_on_closed_callback([this]() { this->finish_(); });
}

bool CameraPreview::apply_catalog(const std::string &payload) {
  if (payload.size() > 16384)
    return false;
  return json::parse_json(payload, [this](JsonObject root) {
    const auto entries = root["sources"].as<JsonArrayConst>();
    if (entries.isNull() || entries.size() > this->camera_->max_runtime_sources())
      return false;
    std::vector<std::string> keys, names, urls;
    keys.reserve(entries.size());
    names.reserve(entries.size());
    urls.reserve(entries.size());
    for (JsonObjectConst entry : entries) {
      const auto *key = entry["key"].as<const char *>();
      const auto *name = entry["name"].as<const char *>();
      const auto *url = entry["url"].as<const char *>();
      if (key == nullptr || name == nullptr || url == nullptr || !valid_key(key))
        return false;
      keys.emplace_back(key);
      names.emplace_back(name);
      urls.emplace_back(url);
    }
    return this->camera_->replace_keyed_sources(keys, names, urls);
  });
}

const char *CameraPreview::open(const std::string &source_key, const std::string &event_id, int duration_seconds) {
  if (!valid_key(source_key) || !valid_key(event_id) || duration_seconds < 15 || duration_seconds > 90)
    return "invalid";
  for (const auto &recent : this->recent_events_) {
    if (std::equal(recent.begin(), recent.end(), event_id.begin()))
      return "duplicate";
  }
  auto *navigation = this->application_->get_parent();
  if (this->active_ || !navigation->is_idle_home() || this->camera_->is_running())
    return "busy";
  if (!this->camera_->begin_preview(source_key))
    return "source_unavailable";

  this->active_ = true;
  if (!navigation->try_open_application(this->application_)) {
    this->finish_();
    return "busy";
  }
  std::copy(event_id.begin(), event_id.end(), this->recent_events_[this->next_event_].begin());
  this->next_event_ = (this->next_event_ + 1) % this->recent_events_.size();
  this->started_ms_ = millis();
  this->duration_ms_ = static_cast<uint32_t>(duration_seconds) * 1000;
  this->set_interval("preview", 200, [this]() { this->check_preview_(); });
  return "opened";
}

void CameraPreview::check_preview_() {
  auto *navigation = this->application_->get_parent();
  if (!navigation->is_application_open(this->application_) && !navigation->is_transition_active()) {
    this->finish_();
    return;
  }
  if (navigation->is_transition_active() || navigation->is_touch_active())
    return;
  if (millis() - this->started_ms_ >= this->duration_ms_ || !this->camera_->preview_available() ||
      !api::global_api_server->is_connected(true)) {
    navigation->try_close_application();
  }
}

void CameraPreview::finish_() {
  if (!this->active_)
    return;
  this->active_ = false;
  this->cancel_interval("preview");
  this->camera_->end_preview();
}

}  // namespace esphome::camera_preview

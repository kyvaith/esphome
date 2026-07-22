#include "sendspin_hub.h"

#ifdef USE_ESP32

#include "esphome/components/network/util.h"
#ifdef USE_ETHERNET
#include "esphome/components/ethernet/ethernet_component.h"
#endif
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#include <esp_wifi.h>
#endif

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"

#include <esp_log.h>
#include <esp_timer.h>

namespace esphome::sendspin_ {

static const char *const TAG = "sendspin.hub";

static void log_slow_callback(const char *name, int64_t started_us) {
  const int64_t elapsed_us = esp_timer_get_time() - started_us;
  if (elapsed_us > 30000) {
    ESP_LOGW(TAG, "slow callback %s: %lldms", name, elapsed_us / 1000);
  }
}

void SendspinHub::setup() {
  auto config = this->build_client_config_();
  this->client_ = std::make_unique<sendspin::SendspinClient>(std::move(config));

  // Set up persistence (preferences must be initialized before providers are added to the client)
  this->last_played_server_pref_ =
      global_preferences->make_preference<LastPlayedServerPref>(fnv1a_hash("sendspin_last_played"));
#ifdef USE_SENDSPIN_PLAYER
  this->static_delay_pref_ = global_preferences->make_preference<StaticDelayPref>(fnv1a_hash("sendspin_static_delay"));
#endif

  // Wire providers and client listener
  this->client_->set_listener(this);
  this->client_->set_network_provider(this);
  this->client_->set_persistence_provider(this);

#ifdef USE_SENDSPIN_ARTWORK
  sendspin::ArtworkRoleConfig artwork_config;
  sendspin::ImageSlotPreference artwork_preference{};
  artwork_preference.source = sendspin::SendspinImageSource::ALBUM;
  artwork_preference.format = sendspin::SendspinImageFormat::JPEG;
  artwork_preference.width = this->artwork_width_;
  artwork_preference.height = this->artwork_height_;
  artwork_config.preferred_formats.push_back(artwork_preference);
  this->artwork_role_ = &this->client_->add_artwork(std::move(artwork_config));
  this->artwork_role_->set_listener(this);
#endif

#ifdef USE_SENDSPIN_CONTROLLER
  this->controller_role_ = &this->client_->add_controller();
  this->controller_role_->set_listener(this);
#endif

#ifdef USE_SENDSPIN_METADATA
  this->metadata_role_ = &this->client_->add_metadata();
  this->metadata_role_->set_listener(this);
#endif

#ifdef USE_SENDSPIN_PLAYER
  this->client_->add_player(this->player_config_).set_listener(this->player_listener_);
#endif

  if (!this->client_->start_server()) {
    ESP_LOGE(TAG, "Failed to start Sendspin server");
    this->mark_failed();
    return;
  }
}

void SendspinHub::loop() { this->client_->loop(); }

void SendspinHub::dump_config() {
  char mac_buf[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
  ESP_LOGCONFIG(TAG,
                "Sendspin Hub:\n"
                "  Client ID: %s\n"
                "  Task stack in PSRAM: %s",
                get_client_id_into_buffer(mac_buf), YESNO(this->task_stack_in_psram_));
}

// --- Delegating methods ---

// THREAD CONTEXT: Main loop (invoked from Sendspin components)
void SendspinHub::connect_to_server(const std::string &url) {
  if (this->is_ready()) {
    this->client_->connect_to(url);
  }
}

// THREAD CONTEXT: Main loop (invoked from Sendspin components)
void SendspinHub::disconnect_from_server(sendspin::SendspinGoodbyeReason reason) {
  if (this->is_ready()) {
    this->client_->disconnect(reason);
  }
}

// THREAD CONTEXT: Main loop (invoked from Sendspin components)
void SendspinHub::update_state(sendspin::SendspinClientState state) {
  if (this->is_ready()) {
    this->client_->update_state(state);
  }
}

const char *SendspinHub::get_client_id_into_buffer(std::span<char, MAC_ADDRESS_PRETTY_BUFFER_SIZE> buf) {
  // The server matches client_id against the L2 source MAC of the device's multicast traffic.
  // ESP-IDF derives the ethernet MAC as base+3 by default on ESP32-S3, so we cannot use the
  // eFuse base MAC when ethernet is the active interface. ESP32-P4 boards commonly use an
  // external Wi-Fi coprocessor; in that case the STA interface MAC can also differ from the
  // chip's base MAC.
#ifdef USE_ETHERNET
  if (ethernet::global_eth_component != nullptr) {
    return ethernet::global_eth_component->get_eth_mac_address_pretty_into_buffer(buf);
  }
#endif
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr) {
    uint8_t mac[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK && mac_address_is_valid(mac)) {
      snprintf(buf.data(), buf.size(), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
               mac[5]);
      return buf.data();
    }
  }
#endif
  return get_mac_address_pretty_into_buffer(buf);
}

sendspin::SendspinClientConfig SendspinHub::build_client_config_() {
  sendspin::SendspinClientConfig config;

  char mac_buf[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
  config.client_id = SendspinHub::get_client_id_into_buffer(mac_buf);
  config.name = App.get_friendly_name();
  config.product_name = App.get_name();
  config.manufacturer = "ESPHome";
  config.software_version = ESPHOME_VERSION;
  config.httpd_psram_stack = this->task_stack_in_psram_;

  return config;
}

// --- SendspinClientListener overrides ---
// THREAD CONTEXT: Main loop (fired from client_->loop())

void SendspinHub::on_group_update(const sendspin::GroupUpdateObject &group) {
  const int64_t started_us = esp_timer_get_time();
  this->group_update_callbacks_.call(group);
  log_slow_callback("group_update", started_us);
}

void SendspinHub::on_request_high_performance() {
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr) {
    wifi::global_wifi_component->request_high_performance();
  }
#endif
}

void SendspinHub::on_release_high_performance() {
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr) {
    wifi::global_wifi_component->release_high_performance();
  }
#endif
}

// --- SendspinNetworkProvider override ---

// THREAD CONTEXT: Main loop (polled by client_->loop())
bool SendspinHub::is_network_ready() { return network::is_connected(); }

// --- SendspinPersistenceProvider overrides ---

// THREAD CONTEXT: Main loop (invoked by client_->loop() during lifecycle events)
bool SendspinHub::save_last_server_hash(uint32_t hash) {
  LastPlayedServerPref pref{.server_id_hash = hash};
  bool ok = this->last_played_server_pref_.save(&pref);
  if (ok) {
    ESP_LOGD(TAG, "Persisted last played server hash: 0x%08" PRIX32, hash);
  } else {
    ESP_LOGW(TAG, "Failed to persist last played server hash");
  }
  return ok;
}

// THREAD CONTEXT: Main loop (invoked by client_->loop() during lifecycle events)
std::optional<uint32_t> SendspinHub::load_last_server_hash() {
  LastPlayedServerPref pref{};
  if (this->last_played_server_pref_.load(&pref)) {
    ESP_LOGI(TAG, "Loaded last played server hash: 0x%08" PRIX32, pref.server_id_hash);
    return pref.server_id_hash;
  }
  return std::nullopt;
}

// --- Sendspin role specific methods/overrides ---

#ifdef USE_SENDSPIN_ARTWORK
// THREAD CONTEXT: Sendspin artwork decode task.
void SendspinHub::on_image_decode(uint8_t slot, const uint8_t *data, size_t length,
                                  sendspin::SendspinImageFormat format) {
  if (slot >= 4 || data == nullptr || length == 0) {
    return;
  }
  auto &target = this->artwork_slots_[slot];
  target.data.clear();
  target.format = format;
  target.ready = true;
  ESP_LOGD(TAG, "Artwork slot %u received: %zu bytes", slot, length);
  const int64_t started_us = esp_timer_get_time();
  this->artwork_image_callbacks_.call(slot, data, length, format);
  log_slow_callback("artwork_decode", started_us);
}

// THREAD CONTEXT: Main loop (ArtworkRoleListener override, fired from client_->loop()).
void SendspinHub::on_image_display(uint8_t slot) {
  if (slot >= 4) {
    return;
  }
  if (!this->artwork_slots_[slot].ready) {
    return;
  }
  const int64_t started_us = esp_timer_get_time();
  this->artwork_display_callbacks_.call(slot);
  log_slow_callback("artwork_display", started_us);
}

// THREAD CONTEXT: Main loop (ArtworkRoleListener override, fired from client_->loop()).
void SendspinHub::on_image_clear(uint8_t slot) {
  if (slot < 4) {
    this->artwork_slots_[slot].ready = false;
    this->artwork_slots_[slot].data.clear();
  }
  const int64_t started_us = esp_timer_get_time();
  this->artwork_clear_callbacks_.call(slot);
  log_slow_callback("artwork_clear", started_us);
}
#endif

#ifdef USE_SENDSPIN_CONTROLLER
// THREAD CONTEXT: Main loop (invoked from ESPHome actions / other components)
void SendspinHub::send_client_command(sendspin::SendspinControllerCommand command, std::optional<uint8_t> volume,
                                      std::optional<bool> mute) {
  ESP_LOGW(TAG, "controller command %u: checking hub readiness", static_cast<unsigned>(command));
  if (this->is_ready()) {
    ESP_LOGW(TAG, "controller command %u: entering sendspin-cpp", static_cast<unsigned>(command));
    this->controller_role_->send_command(command, volume, mute);
    ESP_LOGW(TAG, "controller command %u: sendspin-cpp returned", static_cast<unsigned>(command));
  }
}

// THREAD CONTEXT: Main loop (ControllerRoleListener override, fired from client_->loop())
void SendspinHub::on_controller_state(const sendspin::ServerStateControllerObject &state) {
  const int64_t started_us = esp_timer_get_time();
  this->controller_state_callbacks_.call(state);
  log_slow_callback("controller_state", started_us);
}
#endif

#ifdef USE_SENDSPIN_METADATA
// THREAD CONTEXT: Main loop (MetadataRoleListener override, fired from client_->loop())
void SendspinHub::on_metadata(const sendspin::ServerMetadataStateObject &metadata) {
  const int64_t started_us = esp_timer_get_time();
  this->metadata_update_callbacks_.call(metadata);
  log_slow_callback("metadata", started_us);
}

// THREAD CONTEXT: Main loop (invoked from Sendspin components)
uint32_t SendspinHub::get_track_progress_ms() const {
  if (this->is_ready()) {
    return this->metadata_role_->get_track_progress_ms();
  }
  return 0;
}
#endif

#ifdef USE_SENDSPIN_PLAYER
// THREAD CONTEXT: Main loop, called from child component setup() after player role is created and configured
sendspin::PlayerRole *SendspinHub::get_player_role() {
  if (this->is_ready()) {
    return this->client_->player();
  }
  return nullptr;
}

// THREAD CONTEXT: Main loop (SendspinPersistenceProvider override)
bool SendspinHub::save_static_delay(uint16_t delay_ms) {
  StaticDelayPref pref{.delay_ms = delay_ms};
  bool ok = this->static_delay_pref_.save(&pref);
  if (ok) {
    ESP_LOGD(TAG, "Persisted static delay: %u ms", delay_ms);
  } else {
    ESP_LOGW(TAG, "Failed to persist static delay");
  }
  return ok;
}

// THREAD CONTEXT: Main loop (SendspinPersistenceProvider override)
std::optional<uint16_t> SendspinHub::load_static_delay() {
  StaticDelayPref pref{};
  if (this->static_delay_pref_.load(&pref)) {
    ESP_LOGI(TAG, "Loaded static delay: %u ms", pref.delay_ms);
    return pref.delay_ms;
  }
  return std::nullopt;
}

#endif

}  // namespace esphome::sendspin_

#endif  // USE_ESP32

#include "i18n.h"

#include "esphome/core/log.h"

namespace esphome::i18n {

static const char *const TAG = "i18n";

void I18nComponent::copy_locale_(char *destination, const char *source) {
  if (source == nullptr)
    source = "en-US";
  std::strncpy(destination, source, 8);
  destination[8] = '\0';
}

void I18nComponent::add_translation(const char *locale, const char *key, const char *value) {
  if (locale == nullptr || key == nullptr || value == nullptr)
    return;
#ifdef USE_I18N_FLASH_TABLE
  ESP_LOGE(TAG, "Flash translation table is immutable; cannot register %s/%s", locale, key);
#else
  if (this->translation_count_ >= MAX_TRANSLATIONS) {
    ESP_LOGE(TAG, "Translation capacity exhausted (%u); cannot register %s/%s", static_cast<unsigned>(MAX_TRANSLATIONS),
             locale, key);
    return;
  }
  this->translations_[this->translation_count_++] = {locale, key, value};
#endif
}

#ifdef USE_I18N_FLASH_TABLE
void I18nComponent::set_translation_table(const Translation *table, size_t count) {
  if ((table == nullptr && count != 0) || count > MAX_TRANSLATIONS) {
    ESP_LOGE(TAG, "Invalid flash translation table binding (%u entries)", static_cast<unsigned>(count));
    return;
  }
  this->translations_ = table;
  this->translation_count_ = static_cast<uint16_t>(count);
}
#endif

bool I18nComponent::set_locale(const std::string &locale) {
  if (locale.empty() || locale.size() >= sizeof(this->locale_))
    return false;
  std::strncpy(this->locale_, locale.c_str(), sizeof(this->locale_) - 1);
  this->locale_[sizeof(this->locale_) - 1] = '\0';
  ESP_LOGI(TAG, "Locale set to %s", this->locale_);
  return true;
}

const char *I18nComponent::find_(const char *locale, const char *key) const {
  for (uint16_t index = 0; index < this->translation_count_; index++) {
    const auto &entry = this->translations_[index];
#ifdef USE_I18N_FLASH_TABLE
    // Match add_translation's rejection of incomplete manual entries.
    if (entry.locale == nullptr || entry.key == nullptr || entry.value == nullptr)
      continue;
#endif
    if (std::strcmp(entry.locale, locale) == 0 && std::strcmp(entry.key, key) == 0)
      return entry.value;
  }
  return nullptr;
}

const char *I18nComponent::translate(const char *key) const {
  if (key == nullptr)
    return "";
  if (const char *value = this->find_(this->locale_, key); value != nullptr)
    return value;
  if (const char *value = this->find_(this->default_locale_, key); value != nullptr)
    return value;
  return key;
}

const char *I18nComponent::translate_existing(const char *text) const {
  if (text == nullptr || text[0] == '\0')
    return text == nullptr ? "" : text;
  for (uint16_t index = 0; index < this->translation_count_; index++) {
    const auto &entry = this->translations_[index];
#ifdef USE_I18N_FLASH_TABLE
    if (entry.locale == nullptr || entry.key == nullptr || entry.value == nullptr)
      continue;
#endif
    if (std::strcmp(entry.locale, this->default_locale_) == 0 && std::strcmp(entry.value, text) == 0)
      return this->translate(entry.key);
  }
  return text;
}

}  // namespace esphome::i18n

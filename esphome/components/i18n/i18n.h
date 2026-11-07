#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/defines.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace esphome::i18n {

class I18nComponent : public Component {
 public:
  // Code generation bounds every configured entry. Strings remain literals;
  // the optional immutable table also keeps their pointers in flash on ESP32.
  static constexpr size_t MAX_TRANSLATIONS = I18N_TRANSLATION_CAPACITY;
  static_assert(MAX_TRANSLATIONS > 0 && MAX_TRANSLATIONS <= UINT16_MAX);
  struct Translation {
    const char *locale;
    const char *key;
    const char *value;
  };

  void set_default_locale(const char *locale) { this->copy_locale_(this->default_locale_, locale); }
  void add_translation(const char *locale, const char *key, const char *value);
#ifdef USE_I18N_FLASH_TABLE
  // The caller owns the immutable table and strings for the component lifetime.
  // Generated tables have static storage. Invalid bindings leave the old table.
  void set_translation_table(const Translation *table, size_t count);
#endif
  bool set_locale(const std::string &locale);
  const char *translate(const char *key) const;
  // Translate a locally-owned current label by matching its source value in
  // the packs. Unknown or HA-owned text is returned unchanged.
  const char *translate_existing(const char *text) const;
  const char *current_locale() const { return this->locale_; }

 protected:
  static void copy_locale_(char *destination, const char *source);
  const char *find_(const char *locale, const char *key) const;

#ifdef USE_I18N_FLASH_TABLE
  const Translation *translations_{nullptr};
#else
  std::array<Translation, MAX_TRANSLATIONS> translations_{};
#endif
  uint16_t translation_count_{0};
  char default_locale_[9]{"en-US"};
  char locale_[9]{"en-US"};
};

template<typename... Ts> class I18nSetLocaleAction final : public Action<Ts...> {
 public:
  I18nSetLocaleAction(I18nComponent *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, locale)

 protected:
  void play(const Ts &...x) override { this->parent_->set_locale(this->locale_.value(x...)); }
  I18nComponent *parent_;
};

}  // namespace esphome::i18n

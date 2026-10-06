#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace esphome::i18n {

class I18nComponent : public Component {
 public:
  // Keep the table bounded while leaving room for product UI, status labels,
  // and several local language packs. Entries point at flash-resident string
  // literals, so this does not allocate per-translation storage.
  static constexpr size_t MAX_TRANSLATIONS = 512;
  struct Translation {
    const char *locale;
    const char *key;
    const char *value;
  };

  void set_default_locale(const char *locale) { this->copy_locale_(this->default_locale_, locale); }
  void add_translation(const char *locale, const char *key, const char *value);
  bool set_locale(const std::string &locale);
  const char *translate(const char *key) const;
  // Translate a locally-owned current label by matching its source value in
  // the packs. Unknown or HA-owned text is returned unchanged.
  const char *translate_existing(const char *text) const;
  const char *current_locale() const { return this->locale_; }

 protected:
  static void copy_locale_(char *destination, const char *source);
  const char *find_(const char *locale, const char *key) const;

  std::array<Translation, MAX_TRANSLATIONS> translations_{};
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

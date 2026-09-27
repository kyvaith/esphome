#include "esphome/core/defines.h"
#ifdef USE_TEXT_SENSOR_FILTER

#include "filter.h"
#include "text_sensor.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <string_view>

namespace esphome::text_sensor {

static const char *const TAG = "text_sensor.filter";

// Filter
void Filter::input(std::string value) {
  ESP_LOGVV(TAG, "Filter(%p)::input(%s)", this, value.c_str());
  if (this->new_value(value))
    this->output(value);
}
void Filter::output(std::string &value) {
  if (this->next_ == nullptr) {
    ESP_LOGVV(TAG, "Filter(%p)::output(%s) -> SENSOR", this, value.c_str());
    this->parent_->internal_send_state_to_frontend(value);
  } else {
    ESP_LOGVV(TAG, "Filter(%p)::output(%s) -> %p", this, value.c_str(), this->next_);
    this->next_->input(std::move(value));
  }
}
void Filter::initialize(TextSensor *parent, Filter *next) {
  ESP_LOGVV(TAG, "Filter(%p)::initialize(parent=%p next=%p)", this, parent, next);
  this->parent_ = parent;
  this->next_ = next;
}

// LambdaFilter
LambdaFilter::LambdaFilter(lambda_filter_t lambda_filter) : lambda_filter_(std::move(lambda_filter)) {}
const lambda_filter_t &LambdaFilter::get_lambda_filter() const { return this->lambda_filter_; }
void LambdaFilter::set_lambda_filter(const lambda_filter_t &lambda_filter) { this->lambda_filter_ = lambda_filter; }

bool LambdaFilter::new_value(std::string &value) {
  auto result = this->lambda_filter_(value);
  if (result.has_value()) {
    ESP_LOGVV(TAG, "LambdaFilter(%p)::new_value(%s) -> %s (continue)", this, value.c_str(), result->c_str());
    value = std::move(*result);
    return true;
  }
  ESP_LOGVV(TAG, "LambdaFilter(%p)::new_value(%s) -> (stop)", this, value.c_str());
  return false;
}

// ToUpperFilter
bool ToUpperFilter::new_value(std::string &value) {
  for (char &c : value)
    c = ::toupper(c);
  return true;
}

// ToLowerFilter
bool ToLowerFilter::new_value(std::string &value) {
  for (char &c : value)
    c = ::tolower(c);
  return true;
}

// Append
bool AppendFilter::new_value(std::string &value) {
  value.append(this->suffix_);
  return true;
}

// Prepend
bool PrependFilter::new_value(std::string &value) {
  value.insert(0, this->prefix_);
  return true;
}

namespace {

struct HtmlEntity {
  std::string_view name;
  uint32_t code_point;
};

constexpr HtmlEntity HTML_ENTITIES[] = {
    {"amp", '&'},       {"apos", '\''},    {"bull", 0x2022},   {"copy", 0x00A9},  {"deg", 0x00B0},
    {"eacute", 0x00E9}, {"gt", '>'},       {"hellip", 0x2026}, {"ldquo", 0x201C}, {"lsquo", 0x2018},
    {"lt", '<'},        {"mdash", 0x2014}, {"middot", 0x00B7}, {"nbsp", ' '},     {"ndash", 0x2013},
    {"quot", '"'},      {"rdquo", 0x201D}, {"reg", 0x00AE},    {"rsquo", 0x2019}, {"trade", 0x2122},
};

bool parse_uint(std::string_view text, uint8_t base, uint32_t &value) {
  if (text.empty())
    return false;
  value = 0;
  for (const char character : text) {
    uint8_t digit;
    if (character >= '0' && character <= '9') {
      digit = character - '0';
    } else if (base == 16 && character >= 'a' && character <= 'f') {
      digit = character - 'a' + 10;
    } else if (base == 16 && character >= 'A' && character <= 'F') {
      digit = character - 'A' + 10;
    } else {
      return false;
    }
    if (digit >= base || value > (0x10FFFFU - digit) / base)
      return false;
    value = value * base + digit;
  }
  return value != 0 && value <= 0x10FFFFU && !(value >= 0xD800U && value <= 0xDFFFU);
}

bool decode_html_entity(std::string_view entity, uint32_t &code_point) {
  if (!entity.empty() && entity.front() == '#') {
    entity.remove_prefix(1);
    uint8_t base = 10;
    if (!entity.empty() && (entity.front() == 'x' || entity.front() == 'X')) {
      base = 16;
      entity.remove_prefix(1);
    }
    return parse_uint(entity, base, code_point);
  }
  for (const auto &candidate : HTML_ENTITIES) {
    if (candidate.name == entity) {
      code_point = candidate.code_point;
      return true;
    }
  }
  return false;
}

void append_utf8(std::string &output, uint32_t code_point) {
  if (code_point <= 0x7FU) {
    output.push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7FFU) {
    output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else if (code_point <= 0xFFFFU) {
    output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else {
    output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  }
}

}  // namespace

bool SanitizeHtmlFilter::new_value(std::string &value) {
  std::string output;
  output.reserve(value.size());
  bool pending_space = false;

  for (size_t index = 0; index < value.size();) {
    if (this->strip_tags_ && value[index] == '<') {
      const size_t close = value.find('>', index + 1);
      if (close != std::string::npos) {
        pending_space = pending_space || !output.empty();
        index = close + 1;
        continue;
      }
    }

    uint32_t code_point = static_cast<unsigned char>(value[index]);
    size_t consumed = 1;
    if (this->decode_entities_ && value[index] == '&') {
      const size_t close = value.find(';', index + 1);
      if (close != std::string::npos && close - index <= 16 &&
          decode_html_entity(std::string_view(value).substr(index + 1, close - index - 1), code_point)) {
        consumed = close - index + 1;
      }
    }

    const bool whitespace =
        code_point <= 0x7FU && (code_point == ' ' || code_point == '\t' || code_point == '\r' || code_point == '\n');
    if (this->collapse_whitespace_ && whitespace) {
      pending_space = pending_space || !output.empty();
    } else {
      if (pending_space && !output.empty())
        output.push_back(' ');
      pending_space = false;
      if (consumed == 1 && static_cast<unsigned char>(value[index]) >= 0x80U) {
        output.push_back(value[index]);
      } else {
        append_utf8(output, code_point);
      }
    }
    index += consumed;
  }

  value = std::move(output);
  return true;
}

// Substitute — non-template helper
bool substitute_filter_apply(const Substitution *substitutions, size_t count, std::string &value) {
  for (size_t i = 0; i < count; i++) {
    const size_t from_len = strlen(substitutions[i].from);
    const size_t to_len = strlen(substitutions[i].to);
    std::size_t pos = 0;
    while ((pos = value.find(substitutions[i].from, pos, from_len)) != std::string::npos) {
      value.replace(pos, from_len, substitutions[i].to, to_len);
      pos += to_len;
    }
  }
  return true;
}

// Map — non-template helper
bool map_filter_apply(const Substitution *mappings, size_t count, std::string &value) {
  for (size_t i = 0; i < count; i++) {
    if (value == mappings[i].from) {
      value.assign(mappings[i].to);
      return true;
    }
  }
  return true;
}

}  // namespace esphome::text_sensor

#endif  // USE_TEXT_SENSOR_FILTER

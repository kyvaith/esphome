#pragma once

#include "esphome/core/defines.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
#include <variant>
#endif

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
#include "material_number_domain.h"
#endif

namespace esphome::lvgl_material {

static constexpr size_t CONTROL_MAX_PAYLOAD_BYTES = 2048;
static constexpr uint32_t CONTROL_MAX_SEQUENCE = 0x7fffffff;
static constexpr uint64_t CONTROL_MAX_REVISION = 9007199254740991ULL;
static constexpr uint32_t CONTROL_LEASE_MS = 30000;

template<size_t N> struct ControlText {
  std::array<char, N + 1> bytes{};
  bool assign(std::string_view value) {
    if (value.size() > N || value.find('\0') != std::string_view::npos)
      return false;
    this->bytes.fill(0);
    for (size_t i = 0; i < value.size(); i++)
      this->bytes[i] = value[i];
    return true;
  }
  std::string_view view() const {
    size_t size = 0;
    while (size < this->bytes.size() && this->bytes[size] != 0)
      size++;
    return {this->bytes.data(), size};
  }
  bool operator==(const ControlText &) const = default;
};

enum class ControlKind : uint8_t {
  CONTROL_KIND_CAPABILITY,
  CONTROL_KIND_BIND,
  CONTROL_KIND_OPEN,
  CONTROL_KIND_CANCEL_OPEN,
  CONTROL_KIND_GRANT,
  CONTROL_KIND_STATE,
  CONTROL_KIND_COMMAND,
  CONTROL_KIND_RESULT,
  CONTROL_KIND_CLOSE,
#if defined(USE_LVGL_MATERIAL_DETAIL_CONTROLS) || defined(USE_LVGL_MATERIAL_MEDIA_CONTROLS)
  CONTROL_KIND_FACET,
  CONTROL_KIND_FACET_RESULT,
#endif
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  CONTROL_KIND_PAGE,
  CONTROL_KIND_PAGE_RESULT,
#endif
};
enum class ControlOperation : uint8_t {
  CONTROL_OPERATION_BRIGHTNESS,
  CONTROL_OPERATION_TURN_ON,
  CONTROL_OPERATION_TURN_OFF,
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  CONTROL_OPERATION_STEP,
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  CONTROL_OPERATION_CHOOSE,
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  CONTROL_OPERATION_COVER_OPEN,
  CONTROL_OPERATION_COVER_STOP,
  CONTROL_OPERATION_COVER_CLOSE,
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  CONTROL_OPERATION_COVER_SET_POSITION,
  CONTROL_OPERATION_COVER_OPEN_TILT,
  CONTROL_OPERATION_COVER_STOP_TILT,
  CONTROL_OPERATION_COVER_CLOSE_TILT,
  CONTROL_OPERATION_COVER_SET_TILT_POSITION,
#endif
#endif
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
  CONTROL_OPERATION_THERMOSTAT_TARGET,
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  CONTROL_OPERATION_FAN_POWER,
  CONTROL_OPERATION_FAN_SPEED,
  CONTROL_OPERATION_FAN_PRESET,
  CONTROL_OPERATION_FAN_OSCILLATION,
  CONTROL_OPERATION_FAN_DIRECTION,
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  CONTROL_OPERATION_HUMIDIFIER_POWER,
  CONTROL_OPERATION_HUMIDIFIER_TARGET,
  CONTROL_OPERATION_HUMIDIFIER_MODE,
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  CONTROL_OPERATION_ROBOT_START,
  CONTROL_OPERATION_ROBOT_PAUSE,
  CONTROL_OPERATION_ROBOT_STOP,
  CONTROL_OPERATION_ROBOT_DOCK,
  CONTROL_OPERATION_ROBOT_LOCATE,
  CONTROL_OPERATION_ROBOT_SPOT,
  CONTROL_OPERATION_ROBOT_SPEED,
  CONTROL_OPERATION_ROBOT_AREA,
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  CONTROL_OPERATION_PANEL_START,
  CONTROL_OPERATION_PANEL_RESUME,
  CONTROL_OPERATION_PANEL_PAUSE,
  CONTROL_OPERATION_PANEL_CANCEL,
  CONTROL_OPERATION_PANEL_DURATION,
  CONTROL_OPERATION_PANEL_POWER,
  CONTROL_OPERATION_PANEL_ACTIVITY,
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  CONTROL_OPERATION_FACET_NUMBER,
  CONTROL_OPERATION_FACET_CHOICE,
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  CONTROL_OPERATION_FACET_COLOR,
#endif
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  CONTROL_OPERATION_FACET_RANGE,
#endif
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
  CONTROL_OPERATION_EXTRA_LOCK,
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  CONTROL_OPERATION_MEDIA_PLAY,
  CONTROL_OPERATION_MEDIA_PAUSE,
  CONTROL_OPERATION_MEDIA_PREVIOUS,
  CONTROL_OPERATION_MEDIA_NEXT,
  CONTROL_OPERATION_MEDIA_STOP,
  CONTROL_OPERATION_MEDIA_TURN_ON,
  CONTROL_OPERATION_MEDIA_TURN_OFF,
  CONTROL_OPERATION_MEDIA_VOLUME,
  CONTROL_OPERATION_MEDIA_VOLUME_UP,
  CONTROL_OPERATION_MEDIA_VOLUME_DOWN,
  CONTROL_OPERATION_MEDIA_MUTE,
  CONTROL_OPERATION_MEDIA_SHUFFLE,
  CONTROL_OPERATION_MEDIA_CHOICE,
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
  CONTROL_OPERATION_ACTION_PRESS,
  CONTROL_OPERATION_ACTION_ACTIVATE,
  CONTROL_OPERATION_ACTION_RUN,
  CONTROL_OPERATION_ACTION_STOP,
  CONTROL_OPERATION_ACTION_ENABLE,
  CONTROL_OPERATION_ACTION_DISABLE,
  CONTROL_OPERATION_ACTION_TRIGGER,
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
  CONTROL_OPERATION_DATETIME_SET,
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  CONTROL_OPERATION_TEXT_SET,
#endif
};
enum class ControlResult : uint8_t {
  CONTROL_RESULT_PENDING,
  CONTROL_RESULT_HA_ACCEPTED,
  CONTROL_RESULT_STATE_MATCHES,
  CONTROL_RESULT_ALREADY_MATCHED,
  CONTROL_RESULT_NOT_CONFIRMED,
  CONTROL_RESULT_REJECTED,
#if defined(USE_LVGL_MATERIAL_ROBOT_CONTROLS) || defined(USE_LVGL_MATERIAL_PANEL_CONTROLS)
  CONTROL_RESULT_SERVICE_COMPLETED,
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
  CONTROL_RESULT_SERVICE_DISPATCHED = 7,
#endif
};
enum class ControlReason : uint8_t {
  CONTROL_REASON_NONE,
  CONTROL_REASON_SERVICE_ERROR,
  CONTROL_REASON_SERVICE_TIMEOUT,
  CONTROL_REASON_STATE_TIMEOUT,
  CONTROL_REASON_SELECTION_CHANGED,
  CONTROL_REASON_CONNECTION_LOST,
  CONTROL_REASON_ENTITY_UNAVAILABLE,
  CONTROL_REASON_UNSUPPORTED,
  CONTROL_REASON_EXPIRED,
  CONTROL_REASON_SEQUENCE_CONFLICT,
  CONTROL_REASON_STALE_SEQUENCE,
  CONTROL_REASON_BUSY,
  CONTROL_REASON_UNAUTHORIZED,
  CONTROL_REASON_USER,
  CONTROL_REASON_NAVIGATION,
  CONTROL_REASON_SLEEP,
  CONTROL_REASON_VOICE,
  CONTROL_REASON_CAPABILITIES_CHANGED,
  CONTROL_REASON_UNLOAD,
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  CONTROL_REASON_STALE_STATE,
#endif
};
enum class ControlViewEvent : uint8_t {
  CONTROL_VIEW_EVENT_GRANTED,
  CONTROL_VIEW_EVENT_STATE,
  CONTROL_VIEW_EVENT_PREVIEW,
  CONTROL_VIEW_EVENT_CANCELLED,
  CONTROL_VIEW_EVENT_SENDING,
  CONTROL_VIEW_EVENT_RESULT,
  CONTROL_VIEW_EVENT_CLOSED
};

struct ControlCapabilities {
  bool brightness{false};
  bool turn_on{false};
  bool turn_off{false};
  bool operator==(const ControlCapabilities &) const = default;
};
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
enum class ControlEntityKind : uint8_t {
  CONTROL_ENTITY_LIGHT,
  CONTROL_ENTITY_NUMBER,
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  CONTROL_ENTITY_CHOICE = 2,
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  CONTROL_ENTITY_COVER = 3,
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  CONTROL_ENTITY_FAN = 4,
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  CONTROL_ENTITY_HUMIDIFIER = 5,
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  CONTROL_ENTITY_ROBOT = 6,
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  CONTROL_ENTITY_PANEL = 7,
#endif
};
#endif

struct ControlContext {
  ControlText<64> card_id;
  uint64_t revision{0};
  uint8_t slot{0};
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  ControlEntityKind control_kind{ControlEntityKind::CONTROL_ENTITY_LIGHT};
#endif
  bool operator==(const ControlContext &) const = default;
};
struct ControlIdentity {
  ControlText<32> channel;
  ControlText<32> token;
  ControlContext context;
  uint32_t open_seq{0};
  bool operator==(const ControlIdentity &) const = default;
};
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
struct ControlThermostatSnapshot {
  static constexpr uint8_t UNKNOWN_LEVEL = 255;
  ControlText<12> target, current, minimum, maximum;
  uint8_t level{UNKNOWN_LEVEL};
  uint8_t activity{0};
  bool present{false};
  bool fahrenheit{false};
  bool dial{false};
  bool operator==(const ControlThermostatSnapshot &) const = default;
};
#endif
struct ControlNumberSnapshot {
  MaterialNumberDomain domain;
  ControlText<64> value_text;
  uint32_t index{0};
  uint32_t state_generation{0};
  bool available{false};
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
  ControlThermostatSnapshot thermostat;
#endif
  bool operator==(const ControlNumberSnapshot &) const = default;
};
#endif

#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
enum class ControlCoverState : uint8_t {
  CONTROL_COVER_STATE_UNKNOWN,
  CONTROL_COVER_STATE_OPEN,
  CONTROL_COVER_STATE_OPENING,
  CONTROL_COVER_STATE_CLOSED,
  CONTROL_COVER_STATE_CLOSING,
};
struct ControlCoverCapabilities {
  bool open{false};
  bool stop{false};
  bool close{false};
  bool operator==(const ControlCoverCapabilities &) const = default;
};
struct ControlCoverSnapshot {
  ControlCoverCapabilities caps;
  uint32_t state_generation{0};
  ControlCoverState state{ControlCoverState::CONTROL_COVER_STATE_UNKNOWN};
  uint8_t position{0};
  bool position_known{false};
  bool available{false};
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  struct Panel {
    struct Capabilities {
      bool set_position{false}, open_tilt{false}, close_tilt{false}, stop_tilt{false}, set_tilt_position{false};
      bool operator==(const Capabilities &) const = default;
    } caps;
    uint8_t device_class{0};  // Unknown, then the ten standard Cover classes.
    uint8_t tilt_position{0};
    bool tilt_known{false}, present{false};
    bool operator==(const Panel &) const = default;
  } panel;
#endif
  bool operator==(const ControlCoverSnapshot &) const = default;
};
#endif

#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
enum class ControlFacet : uint8_t {
  CONTROL_FACET_MAIN,
  CONTROL_FACET_KELVIN,
  CONTROL_FACET_EFFECT,
  CONTROL_FACET_HUMIDITY,
  CONTROL_FACET_HVAC_MODE,
  CONTROL_FACET_PRESET_MODE,
  CONTROL_FACET_FAN_MODE,
  CONTROL_FACET_SWING_MODE,
  CONTROL_FACET_SWING_HORIZONTAL_MODE,
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  CONTROL_FACET_COLOR = 9,
#endif
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  CONTROL_FACET_RANGE = 10,
#endif
};
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
struct ControlFacetColor {
  // Fixed-point degrees/percent, never brightness. COMMAND reuses this variant
  // for the two target values; source-only metadata is not serialized there.
  uint32_t hue_milli{0}, saturation_milli{0};
  std::array<uint8_t, 3> rgb{};
  uint8_t write_mode{0}, source_mode{5}, reason{5};
  bool known{false};
  bool operator==(const ControlFacetColor &) const = default;
};
struct ControlColorDraft {
  uint32_t hue_milli{0}, saturation_milli{0}, generation{0}, view_seq{0};
  bool active{false};
};
bool control_valid_facet_color(const ControlFacetColor &color, bool editable);
#endif
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
struct ControlFacetRange {
  MaterialNumberDomain domain;
  uint32_t low_index{0}, high_index{0};
  ControlText<12> current;
  uint8_t mode{0}, activity{0}, reason{3};
  bool known{false};
  bool operator==(const ControlFacetRange &) const = default;
};
bool control_valid_facet_range(const ControlFacetRange &range, bool available, bool editable);
#endif
struct ControlFacetNumber {
  MaterialNumberDomain domain;
  ControlText<20> value, current;
  uint32_t index{0};
  bool known{false};
  bool operator==(const ControlFacetNumber &) const = default;
};
struct ControlFacetChoice {
  std::array<ControlText<32>, 8> options{};
  uint8_t count{0}, index{255};
  bool known{false};
  bool operator==(const ControlFacetChoice &) const = default;
};
struct ControlClimateMain {
  ControlFacetNumber number;
  ControlText<12> target, current, low, high, minimum, maximum;
  uint8_t unit{0}, mode{0}, activity{0};
  bool operator==(const ControlClimateMain &) const = default;
};
struct ControlDetailSnapshot {
  // Only one live facet/catalogue is retained; variants never own heap storage.
  std::variant<std::monostate, ControlFacetNumber, ControlFacetChoice, ControlClimateMain
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
               ,
               ControlFacetColor
#endif
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
               ,
               ControlFacetRange
#endif
               >
      data;
  uint32_t state_generation{0}, view_seq{0};
  ControlFacet facet{ControlFacet::CONTROL_FACET_MAIN};
  uint8_t facets{0};
  bool present{false}, climate{false}, available{false}, editable{false};
  bool operator==(const ControlDetailSnapshot &) const = default;
  const ControlFacetNumber *numeric() const {
    if (auto *main = std::get_if<ControlClimateMain>(&this->data))
      return &main->number;
    return std::get_if<ControlFacetNumber>(&this->data);
  }
  const ControlFacetChoice *choice() const { return std::get_if<ControlFacetChoice>(&this->data); }
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  const ControlFacetRange *range() const { return std::get_if<ControlFacetRange>(&this->data); }
#endif
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  const ControlFacetColor *color() const { return std::get_if<ControlFacetColor>(&this->data); }
#endif
};
struct ControlNumericView {
  const MaterialNumberDomain *domain{nullptr};
  uint32_t index{0};
  bool known{false}, editable{false};
};
#endif

#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
struct ControlExtraSnapshot {
  static constexpr uint8_t UNKNOWN_BOOL = 255;
  // Types: sensor, binary sensor, calendar, alarm, lock, person, sun. Sources: known, unknown, unavailable.
  struct Sensor {
    ControlText<32> device_class;
    ControlText<96> value;
    ControlText<24> unit;
    uint8_t status{4};
    bool operator==(const Sensor &) const = default;
  };
  struct State {
    ControlText<32> device_class;
    ControlText<96> text;
    uint8_t status{4}, phase{UNKNOWN_BOOL};
    bool operator==(const State &) const = default;
  };
  struct Calendar {
    ControlText<128> summary;
    ControlText<40> start, end;
    ControlText<48> zone;
    ControlText<96> location;
    ControlText<64> description;
    uint8_t event{3}, active{UNKNOWN_BOOL}, all_day{UNKNOWN_BOOL}, truncated{0};
    bool operator==(const Calendar &) const = default;
  };
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  struct Agenda {
    ControlText<96> summary;
    ControlText<40> start, end;
    ControlText<48> zone;
    ControlText<64> location;
    ControlText<32> description;
    uint32_t request_seq{0};
    // Status: loading, ready, error, unknown. No event uses index/all_day=255.
    uint8_t index{255}, count{0}, status{3}, all_day{UNKNOWN_BOOL}, truncated{0};
    bool has_more{false};
    bool operator==(const Agenda &) const = default;
  };
  static_assert(sizeof(Agenda) <= sizeof(Calendar));
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  struct Statistics {
    std::array<std::array<uint16_t, 3>, 24> points{};
    uint32_t valid_mask{0}, window_end_hour{0};
    ControlText<24> axis_min, axis_max;
    ControlText<32> unit, start, end;
    ControlText<48> zone;
    // Status: loading, ready, empty, unsupported, error. Current source is independent of history.
    uint8_t status{0}, current_status{1}, reason{0};
    bool flat{false};
    bool operator==(const Statistics &) const = default;
  };
  static_assert(sizeof(Statistics) <= sizeof(Calendar));
#endif
  struct Lock {
    uint8_t phase{7};
    bool capability{false};
    bool operator==(const Lock &) const = default;
  };
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
  struct Person {
    ControlText<96> text;
    ControlText<40> changed;
    ControlText<48> zone;
    uint8_t phase{3}, status{4}, changed_status{1};
    bool operator==(const Person &) const = default;
  };
  struct Sun {
    ControlText<40> rising, setting;
    ControlText<24> elevation, azimuth;
    ControlText<48> zone;
    uint8_t phase{2}, rising_status{1}, setting_status{1}, elevation_status{1}, azimuth_status{1};
    bool operator==(const Sun &) const = default;
  };
  static_assert(sizeof(Person) <= sizeof(Calendar) && sizeof(Sun) <= sizeof(Calendar));
#endif
  std::variant<std::monostate, Sensor, State, Calendar, Lock
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
               ,
               Person, Sun
#endif
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
               ,
               Agenda
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
               , Statistics
#endif
               >
      data;
  ControlText<48> updated;
  uint32_t state_generation{0};
  uint8_t type{255}, source{1};
  bool operator==(const ControlExtraSnapshot &) const = default;
  bool available() const { return this->source == 0; }
  uint8_t profile() const { return this->type == 4 ? 2 : this->type == 5 || this->type == 6 ? 33 : 1; }
};
#endif

#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
enum class ControlMediaFacet : uint8_t { MAIN, VOLUME, SOURCE, SOUND_MODE, REPEAT };
struct ControlMediaSnapshot {
  static constexpr uint8_t UNKNOWN_BOOL = 255;
  static constexpr uint16_t ALL_OPERATIONS = 4095;
  struct Volume {
    ControlText<24> raw;
    ControlText<26> percent;
    uint16_t rail{0};  // Presentation geometry only; never an authoritative target.
    bool known{false};
    bool operator==(const Volume &) const = default;
  };
  struct Main {
    Volume volume;
    ControlText<128> title;
    ControlText<96> artist;
    uint8_t phase{7}, muted{UNKNOWN_BOOL}, shuffle{UNKNOWN_BOOL}, title_status{1}, artist_status{1};
    bool operator==(const Main &) const = default;
  };
  struct Choice {
    std::array<ControlText<32>, 8> options{};
    uint8_t count{0}, index{255};
    bool operator==(const Choice &) const = default;
  };
  // Commands borrow this same otherwise-unused state storage for expected raw
  // volume/index and their facet; no second catalogue or command buffer exists.
  std::variant<std::monostate, Main, Volume, Choice> data;
  uint32_t state_generation{0}, view_seq{0};
  uint16_t operations{0}, eligible{0};
  ControlMediaFacet facet{ControlMediaFacet::MAIN};
  uint8_t device_type{0}, facets{0}, source{1};
  bool editable{false};
  bool operator==(const ControlMediaSnapshot &) const = default;
  bool available() const { return this->source == 0; }
};
bool control_media_volume_valid(const ControlMediaSnapshot::Volume &volume);
bool control_media_raw_valid(std::string_view raw);
uint16_t control_media_main_eligible(const ControlMediaSnapshot &media);
#endif

#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
struct ControlActionSnapshot {
  static constexpr uint8_t UNKNOWN_BOOL = 255;
  static constexpr uint8_t ALL_OPERATIONS = 127;
  static constexpr uint8_t TILE_EVENT_TYPE = 5;
  // Types: button, input button, scene, script, automation, tile event. Sources: known, unknown, unavailable.
  // Phases: stateless, idle, running, enabled, disabled, unknown. Timestamps are HA-normalized display text.
  ControlText<48> last_used;
  uint32_t state_generation{0};
  uint16_t current{0};
  uint8_t type{255}, source{1}, phase{5}, operations{0}, eligible{0}, last_used_status{1};
  uint8_t skip_condition{UNKNOWN_BOOL}, stop_actions{UNKNOWN_BOOL}, run_blocked{0};
  bool current_known{false};
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  // Independent negotiated form extension; absent metadata keeps Action2 unchanged.
  ControlText<96> parameter_label;
  uint8_t parameters_version{0}, parameter_status{0}, parameter_default{UNKNOWN_BOOL};
#endif
  uint16_t profile() const { return this->type == TILE_EVENT_TYPE ? 280U : 8U; }
  bool operator==(const ControlActionSnapshot &) const = default;
  bool available() const { return this->source == 0; }
};
uint8_t control_action_eligible(const ControlActionSnapshot &action);
bool control_action_capabilities_equal(const ControlActionSnapshot &first, const ControlActionSnapshot &second);
bool control_action_command_state_equal(const ControlActionSnapshot &first, const ControlActionSnapshot &second);
#endif

#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
enum class ControlDatetimeMode : uint8_t {
  CONTROL_DATETIME_MODE_DATE,
  CONTROL_DATETIME_MODE_TIME,
  CONTROL_DATETIME_MODE_DATETIME
};
enum class ControlDatetimeField : uint8_t {
  CONTROL_DATETIME_FIELD_YEAR,
  CONTROL_DATETIME_FIELD_MONTH,
  CONTROL_DATETIME_FIELD_DAY,
  CONTROL_DATETIME_FIELD_HOUR,
  CONTROL_DATETIME_FIELD_MINUTE,
  CONTROL_DATETIME_FIELD_SECOND
};
enum class ControlDatetimeInput : uint8_t {
  CONTROL_DATETIME_INPUT_SELECT_FIELD,
  CONTROL_DATETIME_INPUT_DIGIT,
  CONTROL_DATETIME_INPUT_BACKSPACE,
  CONTROL_DATETIME_INPUT_ACCEPT_FIELD,
  CONTROL_DATETIME_INPUT_STEP_DOWN,
  CONTROL_DATETIME_INPUT_STEP_UP,
  CONTROL_DATETIME_INPUT_CANCEL,
  CONTROL_DATETIME_INPUT_SAVE
};
struct ControlDatetimeTuple {
  std::array<uint16_t, 6> values{};
  uint8_t known_mask{0};
  bool operator==(const ControlDatetimeTuple &) const = default;
};
struct ControlDatetimeSnapshot {
  // COMMAND reuses this family storage for only its six-field target.
  ControlDatetimeTuple value;
  ControlText<48> zone, updated;
  uint32_t state_generation{0};
  ControlDatetimeMode mode{ControlDatetimeMode::CONTROL_DATETIME_MODE_DATE};
  uint8_t source{1};
  bool can_set{false};
  bool available() const { return this->source == 0; }
  bool operator==(const ControlDatetimeSnapshot &) const = default;
};
struct ControlDatetimeDraftView {
  ControlDatetimeTuple value;
  std::array<char, 5> digits{};
  uint32_t baseline_generation{0}, revision{0};
  uint8_t selected_field{255};
  bool active{false}, dirty{false}, editor_open{false}, replace_on_first_digit{false};
  bool field_valid{false}, tuple_valid{false}, can_save{false}, changed_elsewhere{false};
};
uint8_t control_datetime_mask(ControlDatetimeMode mode);
bool control_datetime_field_valid(uint8_t field, uint16_t value);
bool control_datetime_tuple_valid(const ControlDatetimeTuple &value, ControlDatetimeMode mode);
bool control_datetime_updated_valid(std::string_view value);
#endif

#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
static constexpr size_t CONTROL_TEXT_MAX_BYTES = 1020;
static constexpr uint16_t CONTROL_TEXT_MAX_CODEPOINTS = 255;
enum class ControlTextInput : uint8_t {
  CONTROL_TEXT_INPUT_BEGIN_EDITOR,
  CONTROL_TEXT_INPUT_INSERT,
  CONTROL_TEXT_INPUT_BACKSPACE,
  CONTROL_TEXT_INPUT_MOVE_LEFT,
  CONTROL_TEXT_INPUT_MOVE_RIGHT,
  CONTROL_TEXT_INPUT_SELECT_CURSOR,
  CONTROL_TEXT_INPUT_CANCEL,
  CONTROL_TEXT_INPUT_SAVE,
  CONTROL_TEXT_INPUT_VIEW
};
enum class ControlTextDraftReason : uint8_t {
  CONTROL_TEXT_DRAFT_REASON_NONE,
  CONTROL_TEXT_DRAFT_REASON_BELOW_MIN,
  CONTROL_TEXT_DRAFT_REASON_ABOVE_MAX,
  CONTROL_TEXT_DRAFT_REASON_BYTE_LIMIT,
  CONTROL_TEXT_DRAFT_REASON_ENCODING,
  CONTROL_TEXT_DRAFT_REASON_CHARACTERS,
  CONTROL_TEXT_DRAFT_REASON_RESERVED
};
// Value bytes live outside every generic snapshot/message. Views are synchronous only.
struct ControlTextValueView {
  std::string_view value;
  bool present{false};
};
struct ControlTextRx {
  ControlText<CONTROL_TEXT_MAX_BYTES> value;
  bool present{false};
  ControlTextValueView view() const { return {this->value.view(), this->present}; }
  void clear() {
    this->value.bytes.fill(0);
    this->present = false;
  }
};
struct ControlTextSnapshot {
  uint32_t state_generation{0};
  uint8_t minimum{0}, maximum{0}, source{1}, reason{0};
  bool bounds_known{false}, can_set{false};
  bool available() const { return this->source == 0; }
  bool operator==(const ControlTextSnapshot &) const = default;
};
// Allocate once in the component's external RAM (or inject in host tests).
// RX is independent of source/draft so admission never runs inside a busy DOM.
struct ControlTextStorage {
  ControlText<CONTROL_TEXT_MAX_BYTES> source, draft;
  ControlTextRx rx;
  const uint32_t *repertoire{nullptr};
  size_t repertoire_size{0};
  uint32_t baseline_generation{0}, revision{0}, pressed_generation{0}, pressed_revision{0};
  uint32_t pressed_selection{0}, pressed_map{0}, argument{0};
  uint16_t cursor{0};
  ControlTextInput input{ControlTextInput::CONTROL_TEXT_INPUT_CANCEL};
  bool active{false}, changed_elsewhere{false};
};
struct ControlTextDraftView {
  std::string_view source, value;
  uint32_t baseline_generation{0}, revision{0};
  uint16_t cursor{0}, codepoints{0};
  bool active{false}, dirty{false}, valid{false}, can_save{false}, changed_elsewhere{false};
  ControlTextDraftReason reason{ControlTextDraftReason::CONTROL_TEXT_DRAFT_REASON_NONE};
};
bool control_text_next(std::string_view value, size_t &offset, uint32_t &scalar);
bool control_text_offset(std::string_view value, uint16_t index, size_t &offset);
bool control_text_measure(std::string_view value, uint16_t &count);
bool control_text_scalar_allowed(uint32_t scalar, const uint32_t *repertoire, size_t count);
ControlTextDraftReason control_text_validate(std::string_view value, uint8_t minimum, uint8_t maximum,
                                             const uint32_t *repertoire = nullptr, size_t count = 0);
#endif

struct ControlSnapshot {
  ControlText<160> title;
  ControlCapabilities caps;
  uint32_t state_seq{0};
  uint32_t ttl_ms{CONTROL_LEASE_MS};
  bool on{false};
  bool brightness_known{false};
  uint8_t brightness_pct{0};
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  struct Choice {
    static constexpr size_t MAX_OPTIONS = 8;
    static constexpr uint8_t EMPTY_INDEX = MAX_OPTIONS;
    static constexpr size_t MAX_OPTION_BYTES = 32;
    std::array<ControlText<MAX_OPTION_BYTES>, MAX_OPTIONS> options{};
    uint32_t state_generation{0};
    uint8_t count{0};
    uint8_t index{0};
    bool available{false};
    bool operator==(const Choice &) const = default;
  };
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  struct Fan {
    static constexpr size_t MAX_OPTIONS = 8;
    static constexpr uint8_t UNKNOWN = 255;
    struct Capabilities {
      std::array<uint8_t, MAX_OPTIONS> speeds{};
      std::array<ControlText<32>, MAX_OPTIONS> presets{};
      uint8_t speed_count{0}, preset_count{0};
      bool turn_on{false}, turn_off{false}, oscillate{false}, direction{false};
      bool operator==(const Capabilities &) const = default;
    } caps;
    uint32_t state_generation{0};
    uint8_t power{UNKNOWN}, percentage{UNKNOWN}, preset_index{UNKNOWN};
    uint8_t oscillating{UNKNOWN}, direction{UNKNOWN};
    bool available{false}, preset_known{false};
    bool operator==(const Fan &) const = default;
  };
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  struct Humidifier {
    static constexpr size_t MAX_OPTIONS = 8;
    static constexpr uint8_t UNKNOWN = 255;
    struct Capabilities {
      std::array<ControlText<32>, MAX_OPTIONS> modes{};
      uint8_t mode_count{0};
      bool turn_on{false}, turn_off{false};
      bool operator==(const Capabilities &) const = default;
    } caps;
    MaterialNumberDomain domain;
    ControlText<12> target_text, current;
    uint32_t state_generation{0};
    uint8_t power{UNKNOWN}, index{UNKNOWN}, mode_index{UNKNOWN}, device_class{UNKNOWN};
    bool available{false};
    bool operator==(const Humidifier &) const = default;
  };
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  struct Robot {
    static constexpr uint8_t UNKNOWN = 255;
    static constexpr size_t MAX_OPTIONS = 8;
    struct Capabilities {
      uint8_t actions{0}, speed_count{0}, area_count{0};
      std::array<ControlText<32>, MAX_OPTIONS> speeds{}, areas{};
      bool operator==(const Capabilities &) const = default;
    } caps;
    uint32_t state_generation{0};
    uint8_t type{0}, activity{0}, eligible_actions{0}, speed_index{UNKNOWN}, battery_percent{UNKNOWN};
    bool available{false}, area_enabled{false};
    bool operator==(const Robot &) const = default;
  };
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  struct Panel {
    static constexpr uint32_t UNKNOWN_VALUE = 0xffffffffU;
    static constexpr uint8_t UNKNOWN_INDEX = 255;
    static constexpr uint32_t MIN_UTC = 946684800U, MAX_UTC = 4102444799U;
    struct Capabilities {
      std::array<ControlText<32>, 8> activities{};
      uint32_t initial_duration{0};
      uint8_t activity_count{0}, device_class{0};
      bool start{false}, pause{false}, cancel{false}, turn_on{false}, turn_off{false};
      bool operator==(const Capabilities &) const = default;
    } caps;
    uint32_t state_generation{0}, duration{UNKNOWN_VALUE}, remaining{UNKNOWN_VALUE}, finishes_at{0}, sampled_at{0};
    uint8_t type{0}, phase{0}, power{0}, activity_index{UNKNOWN_INDEX}, eligible_actions{0};
    bool available{false};
    bool operator==(const Panel &) const = default;
  };
#endif

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  // Reading never constructs an alternative. Reacquire borrowed pointers after
  // assigning state, emplacing a family, callbacks, or navigation handoff.
  template<typename Family> Family *family_if() noexcept { return std::get_if<Family>(&this->family_); }
  template<typename Family> const Family *family_if() const noexcept { return std::get_if<Family>(&this->family_); }
  template<typename Family> Family &emplace_family() noexcept { return this->family_.template emplace<Family>(); }
  bool has_no_family() const noexcept { return std::holds_alternative<std::monostate>(this->family_); }
  void clear_family() noexcept { this->family_.template emplace<std::monostate>(); }
  bool matches_family(ControlEntityKind kind) const noexcept {
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
    if (const auto *detail = this->family_if<ControlDetailSnapshot>())
      return detail->present && (detail->climate ? kind == ControlEntityKind::CONTROL_ENTITY_PANEL
                                                 : kind == ControlEntityKind::CONTROL_ENTITY_LIGHT);
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
    if (this->family_if<ControlExtraSnapshot>() != nullptr)
      return kind == ControlEntityKind::CONTROL_ENTITY_PANEL;
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
    if (this->family_if<ControlMediaSnapshot>() != nullptr)
      return kind == ControlEntityKind::CONTROL_ENTITY_PANEL;
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
    if (this->family_if<ControlActionSnapshot>() != nullptr)
      return kind == ControlEntityKind::CONTROL_ENTITY_PANEL;
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
    if (this->family_if<ControlDatetimeSnapshot>() != nullptr)
      return kind == ControlEntityKind::CONTROL_ENTITY_PANEL;
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
    if (this->family_if<ControlTextSnapshot>() != nullptr)
      return kind == ControlEntityKind::CONTROL_ENTITY_PANEL;
#endif
    switch (kind) {
      case ControlEntityKind::CONTROL_ENTITY_LIGHT:
        return this->has_no_family();
      case ControlEntityKind::CONTROL_ENTITY_NUMBER:
        return this->family_if<ControlNumberSnapshot>() != nullptr;
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
      case ControlEntityKind::CONTROL_ENTITY_CHOICE:
        return this->family_if<Choice>() != nullptr;
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
      case ControlEntityKind::CONTROL_ENTITY_COVER:
        return this->family_if<ControlCoverSnapshot>() != nullptr;
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
      case ControlEntityKind::CONTROL_ENTITY_FAN:
        return this->family_if<Fan>() != nullptr;
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
      case ControlEntityKind::CONTROL_ENTITY_HUMIDIFIER:
        return this->family_if<Humidifier>() != nullptr;
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
      case ControlEntityKind::CONTROL_ENTITY_ROBOT:
        return this->family_if<Robot>() != nullptr;
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
      case ControlEntityKind::CONTROL_ENTITY_PANEL:
        return this->family_if<Panel>() != nullptr;
#endif
      default:
        return false;
    }
  }
#endif
  bool operator==(const ControlSnapshot &) const = default;

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
 private:
  using FamilyStorage = std::variant<std::monostate, ControlNumberSnapshot
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
                                     ,
                                     Choice
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
                                     ,
                                     ControlCoverSnapshot
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
                                     ,
                                     Fan
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
                                     ,
                                     Humidifier
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
                                     ,
                                     Robot
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
                                     ,
                                     Panel
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
                                     ,
                                     ControlDetailSnapshot
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
                                     ,
                                     ControlExtraSnapshot
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
                                     ,
                                     ControlMediaSnapshot
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
                                     ,
                                     ControlActionSnapshot
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
                                     ,
                                     ControlDatetimeSnapshot
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                                     ,
                                     ControlTextSnapshot
#endif
                                     >;
  FamilyStorage family_{};
#endif
};

// Decoded wire storage. Exact per-kind fields are checked by the JSON adapter.
// A single complete value is passed synchronously; no pointer is retained.
struct ControlMessage {
  ControlKind kind{ControlKind::CONTROL_KIND_CAPABILITY};
  ControlIdentity identity;
  ControlSnapshot state;
  uint32_t command_seq{0};
  ControlOperation operation{ControlOperation::CONTROL_OPERATION_BRIGHTNESS};
  uint8_t value{0};
  ControlResult result{ControlResult::CONTROL_RESULT_PENDING};
  ControlReason reason{ControlReason::CONTROL_REASON_NONE};
  bool enabled{false};
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  uint32_t view_seq{0}, facet_target_index{0};
  ControlFacet facet{ControlFacet::CONTROL_FACET_MAIN};
  bool detail_open{false}, expected_index_known{false}, facet_ready{false};
#endif
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  uint32_t expected_index{0};
  uint32_t expected_state_generation{0};
  int8_t direction{0};
  bool generic{false};
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  uint8_t target_index{0};
  bool choice_enabled{false};
#endif
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
  uint32_t thermostat_target_index{0};
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  bool cover_enabled{false};
#endif
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  uint8_t cover_target_index{0};
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  uint8_t fan_target_index{0};
  bool fan_enabled{false};
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  uint8_t humidifier_target_index{0};
  bool humidifier_enabled{false};
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  uint8_t robot_target_index{0};
  bool robot_enabled{false};
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  uint32_t panel_value{0};
  bool panel_enabled{false};
#endif
};

bool control_valid_hex(const ControlText<32> &text);
bool control_valid_context(const ControlContext &context);
bool control_valid_snapshot(const ControlSnapshot &snapshot);
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
bool control_valid_datetime_snapshot(const ControlSnapshot &snapshot);
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
bool control_valid_action_snapshot(const ControlSnapshot &snapshot);
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
bool control_valid_media_snapshot(const ControlSnapshot &snapshot);
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
bool control_valid_extra_snapshot(const ControlSnapshot &snapshot);
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
bool control_valid_agenda(const ControlExtraSnapshot &extra);
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
bool control_valid_statistics(const ControlExtraSnapshot &extra);
#endif
bool control_extra_metadata_equal(const ControlExtraSnapshot &first, const ControlExtraSnapshot &second);
uint8_t control_extra_eligible(const ControlExtraSnapshot &extra);
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
bool control_info_context_datetime(std::string_view value);
bool control_valid_info_context(const ControlExtraSnapshot &extra);
#endif
#endif
bool control_valid_light_fields(const ControlSnapshot &snapshot);
bool control_valid_result(ControlResult result, ControlReason reason);
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
bool control_valid_number_snapshot(const ControlSnapshot &snapshot);
bool control_valid_generic_result(ControlResult result, ControlReason reason, bool allow_service_completed = false,
                                  bool allow_service_dispatched = false);
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
bool control_valid_robot_snapshot(const ControlSnapshot &snapshot);
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
bool control_valid_panel_snapshot(const ControlSnapshot &snapshot);
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
bool control_valid_detail_snapshot(const ControlSnapshot &snapshot);
bool control_detail_facet_supported(const ControlDetailSnapshot &detail, ControlFacet facet);
#endif
bool panel_timer_remaining(const ControlSnapshot::Panel &panel, bool clock_valid, uint32_t utc_seconds,
                           uint32_t &remaining, bool &sample_only);
#endif
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
bool control_valid_choice_snapshot(const ControlSnapshot &snapshot, bool allow_empty = false);
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
bool control_valid_cover_snapshot(const ControlSnapshot &snapshot, bool allow_panel = false);
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
bool control_valid_fan_snapshot(const ControlSnapshot &snapshot);
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
bool control_valid_humidifier_snapshot(const ControlSnapshot &snapshot);
#endif
bool control_parse_json(std::string_view payload, ControlMessage &output);
bool control_serialize_json(const ControlMessage &message, char *output, size_t capacity, size_t &length);

#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
bool control_valid_text_metadata(const ControlSnapshot &snapshot);
bool control_valid_text_snapshot(const ControlSnapshot &snapshot, ControlTextValueView value,
                                 const uint32_t *repertoire = nullptr, size_t count = 0);
#endif

class MaterialEntityControlSink {
 public:
  virtual ~MaterialEntityControlSink() = default;
  // True means synchronously accepted into the adapter's bounded transport.
  // It is not HA acceptance. Copy the message before returning if needed.
  virtual bool emit(const ControlMessage &message) = 0;
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  // Complete serialization must finish before this returns; never retain the view.
  virtual bool emit_text(const ControlMessage &, ControlTextValueView) { return false; }
#endif
  virtual void view_event(ControlViewEvent event) = 0;
};

// Owner-task only. No worker, widget access, scheduler, service resolver or
// renderer exists here. Adapters must not call back into the owner recursively.
class MaterialEntityControl {
 public:
  explicit MaterialEntityControl(MaterialEntityControlSink &sink) : sink_(sink) {}
  bool set_enabled(bool enabled);
  bool bind(const ControlText<32> &channel);
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  bool bind_generic(const ControlText<32> &channel);
  bool is_generic() const { return this->generic_; }
  bool is_number() const {
    return this->generic_ && this->identity_.context.control_kind == ControlEntityKind::CONTROL_ENTITY_NUMBER;
  }
  bool is_discrete() const {
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
    if (this->is_text())
      return true;
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
    if (this->is_datetime())
      return true;
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
    if (this->is_action())
      return true;
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
    if (this->is_media())
      return true;
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
    const auto *detail = this->state_.family_if<ControlDetailSnapshot>();
    if (this->is_detail() && detail != nullptr &&
        (detail->climate || detail->facet != ControlFacet::CONTROL_FACET_MAIN))
      return true;
#endif
    if (this->is_number())
      return true;
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
    if (this->is_choice())
      return true;
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
    if (this->is_cover())
      return true;
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
    if (this->is_fan())
      return true;
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
    if (this->is_humidifier())
      return true;
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
    if (this->is_robot())
      return true;
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
    if (this->is_panel())
      return true;
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
    if (this->is_extra())
      return true;
#endif
#endif
    return false;
  }
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  bool set_robot_enabled(bool enabled);
  bool robot_enabled() const { return this->robot_enabled_; }
  bool is_robot() const {
    return this->generic_ && this->identity_.context.control_kind == ControlEntityKind::CONTROL_ENTITY_ROBOT;
  }
  bool can_robot(ControlOperation operation, uint8_t target = 0, bool allow_same = false) const;
  bool begin_robot(uint32_t pointer, ControlOperation operation, uint8_t target, uint32_t now);
  bool release_robot(uint32_t pointer, uint32_t now);
  bool begin_robot_navigation(uint32_t pointer, uint32_t now);
  bool release_robot_navigation(uint32_t pointer, uint32_t now);
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  bool set_info_statistics(bool enabled);
  bool accept_info_statistics(const ControlText<32> &channel);
  bool info_statistics_enabled() const { return this->statistics_enabled_; }
  bool info_statistics_bound() const { return this->statistics_peer_; }
  bool is_statistics() const;
#endif
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  bool set_info_agenda(bool enabled);
  bool accept_info_agenda(const ControlText<32> &channel);
  bool info_agenda_enabled() const { return this->agenda_enabled_; }
  bool info_agenda_bound() const { return this->agenda_peer_; }
  bool is_agenda() const;
  bool can_agenda_page(int8_t direction) const;
  bool begin_agenda_page(uint32_t pointer, int8_t direction, uint32_t now);
  bool release_agenda_page(uint32_t pointer, uint32_t now);
  bool agenda_page_pending() const { return this->agenda_flags_ & 1U; }
  bool agenda_page_failed() const { return this->agenda_flags_ & 4U; }
#endif
  bool set_extra_profiles(uint16_t profiles);
  uint16_t extra_profiles() const { return this->extra_profiles_; }
  uint16_t bound_extra_profiles() const { return this->extra_peer_profiles_; }
  bool accept_extra_features(const ControlText<32> &channel, uint16_t profiles);
  bool is_extra() const {
    return this->generic_ && this->selected_ &&
           this->identity_.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL &&
           this->state_.family_if<ControlExtraSnapshot>() != nullptr;
  }
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
  bool is_action() const {
    return this->generic_ && this->selected_ &&
           this->identity_.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL &&
           this->state_.family_if<ControlActionSnapshot>() != nullptr;
  }
  bool can_action(ControlOperation operation) const;
  bool begin_action(uint32_t pointer, ControlOperation operation, uint32_t now);
  bool release_action(uint32_t pointer, uint32_t now);
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  bool set_action_parameters(bool enabled);
  bool accept_action_parameters(const ControlText<32> &channel);
  bool action_parameters_enabled() const { return this->action_parameters_enabled_; }
  bool action_parameters_bound() const { return this->action_parameters_peer_; }
  bool is_action_parameters() const;
  bool can_choose_action_parameter() const;
  void set_action_parameter_ready(bool ready);
  bool set_action_parameter(bool value, uint32_t now);
  bool begin_action_parameter(uint32_t pointer, bool value, uint32_t now);
  bool release_action_parameter(uint32_t pointer, uint32_t now);
  uint8_t action_parameter_value() const { return this->action_parameter_value_; }
  bool action_parameter_is_default() const { return this->action_parameter_default_; }
  bool action_parameter_ready() const { return this->action_parameter_ready_; }
  uint32_t action_parameter_revision() const { return this->action_parameter_revision_; }
#endif

#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
  bool is_datetime() const {
    return this->generic_ && this->selected_ &&
           this->identity_.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL &&
           this->state_.family_if<ControlDatetimeSnapshot>() != nullptr;
  }
  ControlDatetimeDraftView datetime_draft() const;
  bool can_datetime_input(ControlDatetimeInput action, uint8_t argument = 0) const;
  bool begin_datetime_input(uint32_t pointer, ControlDatetimeInput action, uint8_t argument, uint32_t now);
  bool release_datetime_input(uint32_t pointer, uint32_t now);
  void cancel_datetime_input();
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  bool is_text() const {
    return this->generic_ && this->selected_ &&
           this->identity_.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL &&
           this->state_.family_if<ControlTextSnapshot>() != nullptr;
  }
  bool set_text_storage(ControlTextStorage *storage, const uint32_t *repertoire, size_t count);
  ControlTextDraftView text_draft() const;
  bool can_text_input(ControlTextInput input, uint32_t argument = 0) const;
  bool begin_text_input(uint32_t pointer, ControlTextInput input, uint32_t argument, uint32_t map_generation,
                        uint32_t now);
  bool release_text_input(uint32_t pointer, uint32_t map_generation, uint32_t now);
  void cancel_text_input();
#endif
  bool can_extra_lock() const;
  bool begin_extra_lock(uint32_t pointer, uint32_t now);
  bool release_extra_lock(uint32_t pointer, uint32_t now);
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  bool is_media() const {
    return this->generic_ && this->selected_ &&
           this->identity_.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL &&
           this->state_.family_if<ControlMediaSnapshot>() != nullptr;
  }
  bool media_view_ready() const { return this->media_view_ready_; }
  bool is_media_volume() const {
    const auto *media = this->state_.family_if<ControlMediaSnapshot>();
    return this->is_media() && media != nullptr && media->facet == ControlMediaFacet::VOLUME;
  }
  bool is_media_view_pending() const { return this->media_view_pending_; }
  bool can_request_media_facet(ControlMediaFacet facet) const;
  bool request_media_facet(ControlMediaFacet facet, uint32_t now);
  bool begin_media_navigation(uint32_t pointer, uint32_t now);
  bool release_media_navigation(uint32_t pointer, uint32_t now);
  bool can_media(ControlOperation operation, uint32_t target = 0) const;
  bool begin_media(uint32_t pointer, ControlOperation operation, uint32_t target, uint32_t now);
  bool update_media_volume(uint32_t pointer, uint32_t target, uint32_t now);
  bool release_media(uint32_t pointer, uint32_t target, uint32_t now);
#endif
#endif
  bool set_panel_types(uint8_t types);
  uint8_t panel_types() const { return this->panel_types_; }
  uint8_t bound_panel_types() const { return this->panel_peer_types_; }
  bool accept_panel_features(const ControlText<32> &channel, uint8_t version, uint8_t types);
  bool is_panel() const {
    return this->generic_ && this->identity_.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
           && !this->is_detail()
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
           && !this->is_extra()
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
           && !this->is_action()
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
           && !this->is_media()
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
           && !this->is_datetime()
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
           && !this->is_text()
#endif
        ;
  }
  bool can_panel(ControlOperation operation, uint32_t value = 0) const;
  bool begin_panel(uint32_t pointer, ControlOperation operation, uint32_t value, uint32_t now);
  bool release_panel(uint32_t pointer, uint32_t now);
  bool begin_panel_navigation(uint32_t pointer, uint32_t now);
  bool release_panel_navigation(uint32_t pointer, uint32_t now);
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  bool set_detail_profiles(uint8_t profiles);
  uint8_t detail_profiles() const { return this->detail_profiles_; }
  uint8_t bound_detail_profiles() const { return this->detail_peer_profiles_; }
  bool accept_detail_features(const ControlText<32> &channel, uint8_t profiles);
  bool request_open_detail(const ControlContext &context, uint32_t now);
  bool is_detail() const {
    const auto *detail = this->state_.family_if<ControlDetailSnapshot>();
    return this->generic_ &&
           (this->detail_open_ ||
            (this->selected_ && this->identity_.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL &&
             detail != nullptr && detail->present));
  }
  bool is_climate_detail() const {
    const auto *detail = this->state_.family_if<ControlDetailSnapshot>();
    return this->is_detail() && detail != nullptr && detail->climate;
  }
  bool is_detail_view_pending() const { return this->detail_view_pending_; }
  bool detail_view_ready() const { return this->detail_view_ready_; }
  bool can_request_facet(ControlFacet facet) const;
  bool request_facet(ControlFacet facet, uint32_t now);
  bool begin_detail_navigation(uint32_t pointer, uint32_t now);
  bool release_detail_navigation(uint32_t pointer, uint32_t now);
  ControlNumericView active_numeric() const;
  bool can_detail_number() const;
  bool can_detail_choice(uint8_t index) const;
  bool begin_detail_number(uint32_t pointer, uint32_t now);
  bool release_detail_number(uint32_t pointer, uint32_t target, uint32_t now);
  bool begin_detail_choice(uint32_t pointer, uint8_t target, uint32_t now);
  bool release_detail_choice(uint32_t pointer, uint32_t now);
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  bool set_detail_range_enabled(bool enabled);
  bool detail_range_enabled() const { return this->detail_range_enabled_; }
  bool detail_range_bound() const { return this->detail_range_enabled_ && this->detail_range_peer_; }
  bool accept_detail_range(const ControlText<32> &channel);
  bool is_detail_range() const;
  bool can_detail_range() const;
  bool begin_detail_range(uint32_t pointer, uint32_t now);
  bool release_detail_range(uint32_t pointer, uint32_t low, uint32_t high, uint32_t now);
#endif
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  bool set_detail_color_enabled(bool enabled);
  bool detail_color_enabled() const { return this->detail_color_enabled_; }
  bool detail_color_bound() const { return this->detail_color_enabled_ && this->detail_color_peer_; }
  bool accept_detail_color(const ControlText<32> &channel);
  bool is_detail_color() const;
  bool can_detail_color() const;
  bool can_apply_detail_color() const;
  const ControlColorDraft &detail_color_draft() const { return this->detail_color_draft_; }
  bool begin_detail_color(uint32_t pointer, uint32_t now);
  bool update_detail_color(uint32_t pointer, uint32_t hue_milli, uint32_t saturation_milli);
  bool release_detail_color(uint32_t pointer, uint32_t now);
  bool begin_detail_color_apply(uint32_t pointer, uint32_t now);
  bool release_detail_color_apply(uint32_t pointer, uint32_t now);
  bool cancel_detail_color(uint32_t pointer, uint32_t now);
#endif
#endif
  bool begin_step(uint32_t pointer_id, int8_t direction, uint32_t now_ms);
  bool release_step(uint32_t pointer_id, uint32_t now_ms);
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
  bool can_drag_thermostat() const;
  bool begin_thermostat_drag(uint32_t pointer_id, uint32_t now_ms);
  bool release_thermostat_drag(uint32_t pointer_id, uint32_t index, uint32_t now_ms);
#endif
  bool can_step(int8_t direction) const;
#endif
  bool choice_empty_active() const {
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
    return this->choice_empty_configured_ && this->choice_empty_peer_ && this->choice_enabled_ && this->generic_ &&
           this->enabled_;
#else
    return false;
#endif
  }
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  bool set_choice_enabled(bool enabled);
  bool choice_enabled() const { return this->choice_enabled_; }
  bool set_choice_empty_enabled(bool enabled);
  bool choice_empty_supported() const { return this->choice_empty_configured_ && this->choice_enabled_; }
  bool accept_choice_features(const ControlText<32> &channel, uint32_t version);
  bool is_choice() const {
    return this->generic_ && this->identity_.context.control_kind == ControlEntityKind::CONTROL_ENTITY_CHOICE;
  }
  bool choice_candidate_known() const { return this->choice_candidate_known_; }
  uint8_t choice_candidate() const { return this->choice_candidate_; }
  bool can_choose(uint8_t index) const;
  bool can_apply_choice() const;
  bool begin_choice(uint32_t pointer_id, uint8_t index, uint32_t now_ms);
  bool release_choice(uint32_t pointer_id, uint32_t now_ms);
  bool begin_choice_apply(uint32_t pointer_id, uint32_t now_ms);
  bool release_choice_apply(uint32_t pointer_id, uint32_t now_ms);
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  bool set_cover_enabled(bool enabled);
  bool cover_enabled() const { return this->cover_enabled_; }
  bool is_cover() const {
    return this->generic_ && this->identity_.context.control_kind == ControlEntityKind::CONTROL_ENTITY_COVER;
  }
  bool can_cover(ControlOperation operation) const;
  bool begin_cover(uint32_t pointer_id, ControlOperation operation, uint32_t now_ms);
  bool release_cover(uint32_t pointer_id, uint32_t now_ms);
#endif
  bool cover_panel_active() const {
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
    return this->cover_panel_configured_ && this->cover_panel_peer_ && this->cover_priority_stop_active();
#else
    return false;
#endif
  }
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  bool set_cover_panel_enabled(bool enabled);
  bool cover_panel_supported() const { return this->cover_panel_configured_ && this->cover_priority_stop_supported(); }
  bool accept_cover_panel_features(const ControlText<32> &channel, uint32_t version);
  bool begin_cover_target(uint32_t pointer, bool tilt, uint8_t target, uint32_t now);
  bool update_cover_target(uint32_t pointer, uint8_t target, uint32_t now);
#endif
#ifdef USE_LVGL_MATERIAL_COVER_PRIORITY_STOP
  bool set_cover_priority_stop_enabled(bool enabled);
  bool cover_priority_stop_supported() const { return this->cover_priority_stop_configured_ && this->cover_enabled_; }
  bool cover_priority_stop_active() const {
    return this->cover_priority_stop_supported() && this->cover_priority_stop_peer_ && this->enabled_ &&
           this->generic_ && this->is_bound();
  }
  bool accept_cover_features(const ControlText<32> &channel, uint32_t version);
#endif
  const ControlText<32> &bound_channel() const { return this->channel_; }
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  bool set_fan_enabled(bool enabled);
  bool fan_enabled() const { return this->fan_enabled_; }
  bool is_fan() const {
    return this->generic_ && this->identity_.context.control_kind == ControlEntityKind::CONTROL_ENTITY_FAN;
  }
  bool can_fan(ControlOperation operation, uint8_t target) const;
  bool begin_fan(uint32_t pointer_id, ControlOperation operation, uint8_t target, uint32_t now_ms);
  bool begin_fan_navigation(uint32_t pointer_id, uint32_t now_ms);
  bool release_fan(uint32_t pointer_id, uint32_t now_ms);
  bool release_fan_navigation(uint32_t pointer_id, uint32_t now_ms);
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  bool set_humidifier_enabled(bool enabled);
  bool humidifier_enabled() const { return this->humidifier_enabled_; }
  bool is_humidifier() const {
    return this->generic_ && this->identity_.context.control_kind == ControlEntityKind::CONTROL_ENTITY_HUMIDIFIER;
  }
  bool can_humidifier(ControlOperation operation, uint8_t target, bool allow_same = false) const;
  bool begin_humidifier(uint32_t pointer_id, ControlOperation operation, uint8_t target, uint32_t now_ms);
  bool begin_humidifier_navigation(uint32_t pointer_id, uint32_t now_ms);
  bool update_humidifier_target(uint32_t pointer_id, uint8_t target, uint32_t now_ms);
  bool release_humidifier(uint32_t pointer_id, uint32_t now_ms);
  bool release_humidifier_navigation(uint32_t pointer_id, uint32_t now_ms);
#endif
  bool request_open(const ControlContext &context, uint32_t now_ms);
  bool receive(const ControlMessage &message, uint32_t now_ms
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
               ,
               ControlTextValueView text_value = {}
#endif
  );
  bool set_view_ready(bool ready, uint32_t now_ms);
  bool set_scalar_ready(bool ready);
  bool begin_drag(uint32_t pointer_id, uint32_t now_ms);
  bool update_candidate(uint32_t pointer_id, int32_t value, uint32_t now_ms);
  bool release(uint32_t pointer_id, uint32_t now_ms);
  bool command(ControlOperation operation, uint32_t now_ms);
  bool cancel_drag();
  bool invalidate_admission(ControlReason reason);
  bool disconnect();
  bool tick(uint32_t now_ms);

  bool is_enabled() const { return this->enabled_; }
  bool is_bound() const { return !this->channel_.view().empty(); }
  bool is_open_pending() const { return this->open_pending_; }
  bool has_selection() const { return this->selected_; }
  bool is_view_ready() const { return this->view_ready_; }
  bool is_scalar_ready() const { return this->scalar_ready_; }
  bool is_dragging() const { return this->dragging_; }
  bool is_command_pending() const { return this->command_pending_; }
  bool candidate_known() const { return this->candidate_known_; }
  uint8_t candidate() const { return this->candidate_; }
  uint32_t admission_generation() const { return this->admission_generation_; }
  uint32_t command_sequence() const { return this->command_seq_; }
  const ControlIdentity &identity() const { return this->identity_; }
  const ControlSnapshot &state() const { return this->state_; }
  ControlResult last_result() const { return this->last_result_; }
  ControlReason last_reason() const { return this->last_reason_; }

 protected:
  bool matches_open_(const ControlIdentity &identity) const;
  bool can_input_(uint32_t now_ms);
  bool send_command_(ControlOperation operation, uint8_t value);
  bool revoke_(ControlReason reason, bool notify_peer);
  bool emit_(ControlMessage &message
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
             ,
             ControlTextValueView text_value = {}
#endif
  );
  void notify_(ControlViewEvent event);
  void sync_candidate_();
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  bool bind_mode_(const ControlText<32> &channel, bool generic);
  bool validate_snapshot_(const ControlMessage &message) const;
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  bool validate_detail_snapshot_(const ControlMessage &message) const;
  bool accept_detail_state_(const ControlSnapshot &snapshot);
  bool receive_facet_result_(const ControlMessage &message);
  bool send_detail_index_(ControlOperation operation, uint32_t target, uint32_t now);
  uint32_t detail_view_sequence_{0}, detail_view_at_{0}, detail_pressed_view_{0}, detail_view_state_seq_{0};
  ControlFacet detail_requested_facet_{ControlFacet::CONTROL_FACET_MAIN},
      detail_pressed_facet_{ControlFacet::CONTROL_FACET_MAIN};
  uint8_t detail_profiles_{0}, detail_peer_profiles_{0}, detail_pressed_choice_{0};
  bool detail_open_{false}, detail_open_requested_{false}, detail_view_pending_{false}, detail_view_ready_{false};
  bool detail_pressed_known_{false};
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  void clear_detail_color_draft_();
  bool detail_color_draft_current_() const;
  ControlColorDraft detail_color_draft_{};
  bool detail_color_enabled_{false}, detail_color_peer_{false};
#endif
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  bool detail_range_enabled_{false}, detail_range_peer_{false};
#endif
#endif
  bool accept_number_state_(const ControlSnapshot &state);
  bool begin_number_input_(uint32_t pointer_id, uint32_t now_ms);
  bool release_number_input_(uint32_t pointer_id, ControlMessage &message, uint32_t now_ms);
  uint32_t pressed_index_{0};
  uint32_t pressed_generation_{0};
  int8_t step_direction_{0};
  bool generic_{false};
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  bool accept_choice_state_(const ControlSnapshot &state);
  void sync_choice_candidate_();
  uint32_t choice_candidate_generation_{0};
  uint8_t choice_candidate_{0};
  uint8_t pressed_choice_{0};
  bool choice_candidate_known_{false};
  bool choice_applying_{false};
  bool choice_enabled_{false};
  bool choice_empty_configured_{false};
  bool choice_empty_peer_{false};
#endif

#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  bool accept_cover_state_(const ControlSnapshot &state);
  bool begin_cover_input_(uint32_t pointer, ControlOperation operation, uint32_t now);
  ControlOperation pressed_cover_operation_{ControlOperation::CONTROL_OPERATION_COVER_STOP};
  bool cover_enabled_{false};
  ControlOperation pending_cover_operation_{ControlOperation::CONTROL_OPERATION_BRIGHTNESS};
#endif
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  bool cover_panel_configured_{false}, cover_panel_peer_{false};
  uint8_t pressed_cover_target_{0};
#endif
#ifdef USE_LVGL_MATERIAL_COVER_PRIORITY_STOP
  bool cover_priority_stop_configured_{false};
  bool cover_priority_stop_peer_{false};
#endif
  MaterialEntityControlSink &sink_;
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  bool accept_fan_state_(const ControlSnapshot &state);
  ControlOperation pressed_fan_operation_{ControlOperation::CONTROL_OPERATION_FAN_POWER};
  uint8_t pressed_fan_target_{0};
  bool fan_enabled_{false};
#endif
  ControlText<32> channel_;
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  bool accept_humidifier_state_(const ControlSnapshot &state);
  bool humidifier_enabled_{false};
  ControlOperation pressed_humidifier_operation_{ControlOperation::CONTROL_OPERATION_HUMIDIFIER_POWER};
  uint8_t pressed_humidifier_target_{0};
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  bool accept_robot_state_(const ControlSnapshot &state);
  bool robot_enabled_{false};
  ControlOperation pressed_robot_operation_{ControlOperation::CONTROL_OPERATION_ROBOT_START};
  ControlOperation pending_robot_operation_{ControlOperation::CONTROL_OPERATION_ROBOT_START};
  uint8_t pressed_robot_target_{0};
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  bool accept_panel_state_(const ControlSnapshot &state);
  uint8_t panel_types_{0}, panel_peer_types_{0};
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
  bool accept_extra_state_(const ControlSnapshot &state);
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  bool accept_agenda_state_(const ControlSnapshot &state);
  bool receive_agenda_result_(const ControlMessage &message);
  void complete_agenda_page_();
  uint32_t agenda_sequence_{0}, agenda_at_{0}, agenda_state_seq_{0}, agenda_pressed_request_{0};
  uint8_t agenda_target_{255}, agenda_flags_{0};
  bool agenda_enabled_{false}, agenda_peer_{false};
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  bool statistics_enabled_{false}, statistics_peer_{false};
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  ControlTextStorage *text_storage_{nullptr};
  bool accept_text_state_(const ControlSnapshot &state, ControlTextValueView value);
  void commit_text_state_(const ControlSnapshot &state, ControlTextValueView value, bool grant);
  void clear_text_draft_(bool changed = false);
  void clear_text_storage_();
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
  bool accept_datetime_state_(const ControlSnapshot &state);
  bool datetime_effective_(ControlDatetimeTuple &value) const;
  bool datetime_digits_(uint16_t &value) const;
  void clear_datetime_draft_(bool changed = false);
  bool advance_datetime_revision_();
  void open_datetime_field_(uint8_t field);
  struct DatetimeDraft {
    uint32_t baseline_generation{0}, revision{0}, pressed_generation{0}, pressed_revision{0}, pressed_selection{0};
    ControlDatetimeTuple value;
    std::array<char, 5> digits{};
    uint8_t selected_field{255}, digit_count{0}, flags{0};
    ControlDatetimeInput input{ControlDatetimeInput::CONTROL_DATETIME_INPUT_CANCEL};
    uint8_t argument{0};
  } datetime_;
  static_assert(sizeof(DatetimeDraft) <= 64);
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
  bool accept_action_state_(const ControlSnapshot &state);
  bool action_service_only_() const;
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  void reset_action_parameter_(const ControlActionSnapshot *action = nullptr);
  uint32_t action_parameter_revision_{0}, action_parameter_pressed_revision_{0};
  uint8_t action_parameter_value_{ControlActionSnapshot::UNKNOWN_BOOL};
  bool action_parameters_enabled_{false}, action_parameters_peer_{false};
  bool action_parameter_default_{false}, action_parameter_choice_pressed_{false}, action_parameter_ready_{false};
#endif

#endif
  uint16_t extra_profiles_{0}, extra_peer_profiles_{0};
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  bool validate_media_snapshot_(const ControlMessage &message) const;
  bool accept_media_state_(const ControlSnapshot &snapshot);
  bool receive_media_facet_result_(const ControlMessage &message);
  uint32_t media_view_sequence_{0}, media_view_at_{0}, media_view_state_seq_{0}, media_pressed_view_{0};
  ControlMediaFacet media_requested_facet_{ControlMediaFacet::MAIN};
  ControlOperation media_operation_{ControlOperation::CONTROL_OPERATION_MEDIA_PLAY};
  bool media_view_pending_{false}, media_view_ready_{false};
#endif
#endif
  ControlOperation pressed_panel_operation_{ControlOperation::CONTROL_OPERATION_PANEL_START};
  ControlOperation pending_panel_operation_{ControlOperation::CONTROL_OPERATION_PANEL_START};
  uint32_t pressed_panel_value_{0};
#endif
  ControlIdentity identity_;
  ControlSnapshot state_;
  uint32_t open_seq_{0};
  uint32_t command_seq_{0};
  uint32_t admission_generation_{0};
  uint32_t open_generation_{0};
  uint32_t renewed_at_{0};
  uint32_t opened_at_{0};
  uint32_t command_at_{0};
  uint32_t pointer_id_{0};
  uint8_t candidate_{0};
  uint8_t drag_start_{0};
  ControlResult last_result_{ControlResult::CONTROL_RESULT_PENDING};
  ControlReason last_reason_{ControlReason::CONTROL_REASON_NONE};
  bool enabled_{false};
  bool selected_{false};
  bool open_pending_{false};
  bool view_ready_{false};
  bool scalar_ready_{false};
  bool dragging_{false};
  bool candidate_known_{false};
  bool drag_start_known_{false};
  bool command_pending_{false};
  bool in_callback_{false};
};

}  // namespace esphome::lvgl_material

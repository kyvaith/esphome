#pragma once

#include "esphome/core/defines.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

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
  CONTROL_KIND_CLOSE
};
enum class ControlOperation : uint8_t {
  CONTROL_OPERATION_BRIGHTNESS,
  CONTROL_OPERATION_TURN_ON,
  CONTROL_OPERATION_TURN_OFF,
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  CONTROL_OPERATION_STEP,
#endif
};
enum class ControlResult : uint8_t {
  CONTROL_RESULT_PENDING,
  CONTROL_RESULT_HA_ACCEPTED,
  CONTROL_RESULT_STATE_MATCHES,
  CONTROL_RESULT_ALREADY_MATCHED,
  CONTROL_RESULT_NOT_CONFIRMED,
  CONTROL_RESULT_REJECTED
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
enum class ControlEntityKind : uint8_t { CONTROL_ENTITY_LIGHT, CONTROL_ENTITY_NUMBER };
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
struct ControlNumberSnapshot {
  MaterialNumberDomain domain;
  ControlText<64> value_text;
  uint32_t index{0};
  uint32_t state_generation{0};
  bool available{false};
  bool operator==(const ControlNumberSnapshot &) const = default;
};
#endif

struct ControlSnapshot {
  ControlText<160> title;
  ControlCapabilities caps;
  uint32_t state_seq{0};
  uint32_t ttl_ms{CONTROL_LEASE_MS};
  bool on{false};
  bool brightness_known{false};
  uint8_t brightness_pct{0};
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  ControlNumberSnapshot number;
#endif
  bool operator==(const ControlSnapshot &) const = default;
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
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  uint32_t expected_index{0};
  uint32_t expected_state_generation{0};
  int8_t direction{0};
  bool generic{false};
#endif
};

bool control_valid_hex(const ControlText<32> &text);
bool control_valid_context(const ControlContext &context);
bool control_valid_snapshot(const ControlSnapshot &snapshot);
bool control_valid_result(ControlResult result, ControlReason reason);
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
bool control_valid_number_snapshot(const ControlSnapshot &snapshot);
bool control_valid_generic_result(ControlResult result, ControlReason reason);
#endif
bool control_parse_json(std::string_view payload, ControlMessage &output);
bool control_serialize_json(const ControlMessage &message, char *output, size_t capacity, size_t &length);

class MaterialEntityControlSink {
 public:
  virtual ~MaterialEntityControlSink() = default;
  // True means synchronously accepted into the adapter's bounded transport.
  // It is not HA acceptance. Copy the message before returning if needed.
  virtual bool emit(const ControlMessage &message) = 0;
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
  bool begin_step(uint32_t pointer_id, int8_t direction, uint32_t now_ms);
  bool release_step(uint32_t pointer_id, uint32_t now_ms);
  bool can_step(int8_t direction) const;
#endif
  bool request_open(const ControlContext &context, uint32_t now_ms);
  bool receive(const ControlMessage &message, uint32_t now_ms);
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
  bool emit_(const ControlMessage &message);
  void notify_(ControlViewEvent event);
  void sync_candidate_();
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  bool bind_mode_(const ControlText<32> &channel, bool generic);
  bool validate_snapshot_(const ControlMessage &message) const;
  bool accept_number_state_(const ControlSnapshot &state);
  uint32_t pressed_index_{0};
  uint32_t pressed_generation_{0};
  int8_t step_direction_{0};
  bool generic_{false};
#endif

  MaterialEntityControlSink &sink_;
  ControlText<32> channel_;
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

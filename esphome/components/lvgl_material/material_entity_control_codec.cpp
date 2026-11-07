#include "material_entity_control_codec.h"
#include <cstring>
#include <limits>

namespace esphome::lvgl_material {

void *MaterialEntityControlCodec::allocate(size_t size) {
  if (size == 0 || size > this->arena_.size() - sizeof(Block))
    return nullptr;
  const size_t alignment = alignof(std::max_align_t);
  const size_t rounded = (size + alignment - 1) & ~(alignment - 1);
  if (rounded > this->arena_.size() - sizeof(Block) || this->used_ > this->arena_.size() - sizeof(Block) - rounded)
    return nullptr;
  auto *block = reinterpret_cast<Block *>(this->arena_.data() + this->used_);
  block->size = size;
  this->used_ += sizeof(Block) + rounded;
  if (this->used_ > this->high_water_)
    this->high_water_ = this->used_;
  return block + 1;
}

void MaterialEntityControlCodec::deallocate(void *pointer) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (this->generic_transaction_ && pointer != nullptr) {
    auto *block = static_cast<Block *>(pointer) - 1;
    const size_t offset = reinterpret_cast<uint8_t *>(block) - this->arena_.data();
    const size_t alignment = alignof(std::max_align_t);
    const size_t rounded = (block->size + alignment - 1) & ~(alignment - 1);
    if (offset + sizeof(Block) + rounded == this->used_)
      this->used_ = offset;
  }
#else
  (void) pointer;
#endif
}

void *MaterialEntityControlCodec::reallocate(void *pointer, size_t size) {
  if (pointer == nullptr)
    return this->allocate(size);
  if (size == 0)
    return nullptr;
  auto *block = static_cast<Block *>(pointer) - 1;
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (this->generic_transaction_) {
    const size_t offset = reinterpret_cast<uint8_t *>(block) - this->arena_.data();
    const size_t alignment = alignof(std::max_align_t);
    const size_t old_rounded = (block->size + alignment - 1) & ~(alignment - 1);
    const size_t new_rounded = (size + alignment - 1) & ~(alignment - 1);
    if (offset + sizeof(Block) + old_rounded == this->used_ && size <= this->arena_.size() - sizeof(Block) &&
        new_rounded <= this->arena_.size() - offset - sizeof(Block)) {
      this->used_ = offset + sizeof(Block) + new_rounded;
      if (this->used_ > this->high_water_)
        this->high_water_ = this->used_;
      block->size = size;
      return pointer;
    }
  }
#endif
  if (size <= block->size) {
    block->size = size;
    return pointer;
  }
  void *replacement = this->allocate(size);
  if (replacement != nullptr)
    std::memcpy(replacement, pointer, block->size);
  return replacement;
}

#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
bool MaterialEntityControlCodec::parse_panel_bind(std::string_view payload, ControlText<32> &channel, uint8_t &types) {
  if (this->busy_)
    return false;
  this->busy_ = true;
  this->used_ = 0;
  this->generic_transaction_ = true;
  const bool result = control_parse_panel_bind_allocated(payload, channel, types, this);
  this->used_ = 0;
  this->generic_transaction_ = false;
  this->busy_ = false;
  return result;
}
#endif

#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
bool MaterialEntityControlCodec::parse_info_agenda_bind(std::string_view payload, ControlText<32> &channel) {
  if (this->busy_)
    return false;
  this->busy_ = true;
  this->used_ = 0;
  this->generic_transaction_ = true;
  const bool result = control_parse_info_agenda_bind_allocated(payload, channel, this);
  this->used_ = 0;
  this->generic_transaction_ = false;
  this->busy_ = false;
  return result;
}
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
bool MaterialEntityControlCodec::parse_info_statistics_bind(std::string_view payload, ControlText<32> &channel) {
  if (this->busy_)
    return false;
  this->busy_ = true;
  this->used_ = 0;
  this->generic_transaction_ = true;
  const bool result = control_parse_info_statistics_bind_allocated(payload, channel, this);
  this->used_ = 0;
  this->generic_transaction_ = false;
  this->busy_ = false;
  return result;
}
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
bool MaterialEntityControlCodec::parse_action_parameters_bind(std::string_view payload, ControlText<32> &channel) {
  if (this->busy_)
    return false;
  this->busy_ = true;
  this->used_ = 0;
  this->generic_transaction_ = true;
  const bool result = control_parse_action_parameters_bind_allocated(payload, channel, this);
  this->used_ = 0;
  this->generic_transaction_ = false;
  this->busy_ = false;
  return result;
}
#endif

#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
bool MaterialEntityControlCodec::parse_detail_range_bind(std::string_view payload, ControlText<32> &channel) {
  if (this->busy_)
    return false;
  this->busy_ = true;
  this->used_ = 0;
  this->generic_transaction_ = true;
  const bool result = control_parse_detail_range_bind_allocated(payload, channel, this);
  this->used_ = 0;
  this->generic_transaction_ = false;
  this->busy_ = false;
  return result;
}
#endif
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
bool MaterialEntityControlCodec::parse_detail_color_bind(std::string_view payload, ControlText<32> &channel) {
  if (this->busy_)
    return false;
  this->busy_ = true;
  this->used_ = 0;
  this->generic_transaction_ = true;
  const bool result = control_parse_detail_color_bind_allocated(payload, channel, this);
  this->used_ = 0;
  this->generic_transaction_ = false;
  this->busy_ = false;
  return result;
}
#endif
bool MaterialEntityControlCodec::parse_detail_bind(std::string_view payload, ControlText<32> &channel,
                                                   uint8_t &profiles) {
  if (this->busy_)
    return false;
  this->busy_ = true;
  this->used_ = 0;
  this->generic_transaction_ = true;
  const bool result = control_parse_detail_bind_allocated(payload, channel, profiles, this);
  this->used_ = 0;
  this->generic_transaction_ = false;
  this->busy_ = false;
  return result;
}
#endif

#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
bool MaterialEntityControlCodec::parse_extra_features(std::string_view payload, ControlText<32> &channel,
                                                      uint16_t &profiles, uint16_t configured, bool bind) {
  if (this->busy_)
    return false;
  this->busy_ = true;
  this->used_ = 0;
  this->generic_transaction_ = true;
  const bool result = control_parse_extra_features_allocated(payload, channel, profiles, configured, bind, this);
  this->used_ = 0;
  this->generic_transaction_ = false;
  this->busy_ = false;
  return result;
}
#endif

bool MaterialEntityControlCodec::parse(std::string_view payload, ControlMessage &output) {
  if (this->busy_)
    return false;
  this->busy_ = true;
  this->used_ = 0;
  const bool accepted = control_parse_json_allocated(payload, output, this);
  this->used_ = 0;
  this->busy_ = false;
  return accepted;
}

bool MaterialEntityControlCodec::serialize(const ControlMessage &message, char *output, size_t capacity,
                                           size_t &length) {
  if (this->busy_)
    return false;
  this->busy_ = true;
  this->used_ = 0;
  const bool accepted = control_serialize_json_allocated(message, output, capacity, length, this);
  this->used_ = 0;
  this->busy_ = false;
  return accepted;
}

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
bool MaterialEntityControlCodec::parse_entity(std::string_view payload, ControlMessage &output, bool allow_empty_choice,
                                              bool allow_cover_panel
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                                              ,
                                              ControlTextRx *text_rx
#endif
) {
  if (this->busy_)
    return false;
  this->busy_ = true;
  this->used_ = 0;
  this->generic_transaction_ = true;
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  if (text_rx != nullptr)
    text_rx->clear();
#endif
  const bool accepted = control_parse_entity_json_allocated(payload, output, this, allow_empty_choice, allow_cover_panel
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                                                            ,
                                                            text_rx
#endif
  );
  this->generic_transaction_ = false;
  this->used_ = 0;
  this->busy_ = false;
  return accepted;
}

bool MaterialEntityControlCodec::serialize_entity(const ControlMessage &message, char *output, size_t capacity,
                                                  size_t &length, bool allow_empty_choice, bool allow_cover_panel
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                                                  ,
                                                  ControlTextValueView text_value
#endif
) {
  if (this->busy_)
    return false;
  this->busy_ = true;
  this->used_ = 0;
  this->generic_transaction_ = true;
  const bool accepted = control_serialize_entity_json_allocated(message, output, capacity, length, this,
                                                                allow_empty_choice, allow_cover_panel
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                                                                ,
                                                                text_value
#endif
  );
  this->generic_transaction_ = false;
  this->used_ = 0;
  this->busy_ = false;
  return accepted;
}
#endif

}  // namespace esphome::lvgl_material

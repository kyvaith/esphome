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
bool MaterialEntityControlCodec::parse_entity(std::string_view payload, ControlMessage &output) {
  if (this->busy_)
    return false;
  this->busy_ = true;
  this->used_ = 0;
  this->generic_transaction_ = true;
  const bool accepted = control_parse_entity_json_allocated(payload, output, this);
  this->generic_transaction_ = false;
  this->used_ = 0;
  this->busy_ = false;
  return accepted;
}

bool MaterialEntityControlCodec::serialize_entity(const ControlMessage &message, char *output, size_t capacity,
                                                  size_t &length) {
  if (this->busy_)
    return false;
  this->busy_ = true;
  this->used_ = 0;
  this->generic_transaction_ = true;
  const bool accepted = control_serialize_entity_json_allocated(message, output, capacity, length, this);
  this->generic_transaction_ = false;
  this->used_ = 0;
  this->busy_ = false;
  return accepted;
}
#endif

}  // namespace esphome::lvgl_material

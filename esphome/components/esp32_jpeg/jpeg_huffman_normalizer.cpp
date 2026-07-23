#include "jpeg_huffman_normalizer.h"

#ifdef USE_ESP32_JPEG

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#include "esp_heap_caps.h"

namespace esphome::esp32_jpeg {
namespace {

constexpr size_t MAX_COMPONENTS = 4;
constexpr size_t HUFFMAN_TABLE_CLASSES = 2;
constexpr size_t HUFFMAN_TABLE_IDS = 4;

constexpr std::array<uint8_t, 16> DC_LUMA_COUNTS = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
constexpr std::array<uint8_t, 12> DC_LUMA_SYMBOLS = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr std::array<uint8_t, 16> DC_CHROMA_COUNTS = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
constexpr std::array<uint8_t, 12> DC_CHROMA_SYMBOLS = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr std::array<uint8_t, 16> AC_LUMA_COUNTS = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D};
constexpr std::array<uint8_t, 162> AC_LUMA_SYMBOLS = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
    0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3,
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};
constexpr std::array<uint8_t, 16> AC_CHROMA_COUNTS = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
constexpr std::array<uint8_t, 162> AC_CHROMA_SYMBOLS = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
    0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1,
    0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A,
    0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA,
    0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};

struct EncodedCode {
  uint16_t code{0};
  uint8_t length{0};
};

using HuffmanEncoder = std::array<EncodedCode, 256>;

template<size_t N>
constexpr HuffmanEncoder make_encoder(const std::array<uint8_t, 16> &counts, const std::array<uint8_t, N> &symbols) {
  HuffmanEncoder encoder{};
  uint16_t code = 0;
  size_t symbol_index = 0;
  for (uint8_t length = 1; length <= 16; length++) {
    for (uint8_t index = 0; index < counts[length - 1]; index++) {
      encoder[symbols[symbol_index++]] = EncodedCode{code, length};
      code++;
    }
    code <<= 1;
  }
  return encoder;
}

constexpr HuffmanEncoder DC_LUMA_ENCODER = make_encoder(DC_LUMA_COUNTS, DC_LUMA_SYMBOLS);
constexpr HuffmanEncoder DC_CHROMA_ENCODER = make_encoder(DC_CHROMA_COUNTS, DC_CHROMA_SYMBOLS);
constexpr HuffmanEncoder AC_LUMA_ENCODER = make_encoder(AC_LUMA_COUNTS, AC_LUMA_SYMBOLS);
constexpr HuffmanEncoder AC_CHROMA_ENCODER = make_encoder(AC_CHROMA_COUNTS, AC_CHROMA_SYMBOLS);

constexpr size_t STANDARD_DHT_PAYLOAD_SIZE = 4 * (1 + 16) + DC_LUMA_SYMBOLS.size() + DC_CHROMA_SYMBOLS.size() +
                                             AC_LUMA_SYMBOLS.size() + AC_CHROMA_SYMBOLS.size();
constexpr size_t STANDARD_DHT_SEGMENT_SIZE = 2 + 2 + STANDARD_DHT_PAYLOAD_SIZE;

uint16_t read_u16_be(const uint8_t *data) { return (static_cast<uint16_t>(data[0]) << 8) | data[1]; }

void write_u16_be(uint8_t *data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value >> 8);
  data[1] = static_cast<uint8_t>(value);
}

struct HeaderSegment {
  size_t start{0};
  size_t marker_offset{0};
  size_t end{0};
  uint8_t marker{0};
};

bool next_header_segment(const uint8_t *jpeg, size_t jpeg_size, size_t *position, HeaderSegment *segment) {
  if (*position >= jpeg_size || jpeg[*position] != 0xFF)
    return false;
  segment->start = *position;
  size_t pos = *position;
  while (pos < jpeg_size && jpeg[pos] == 0xFF)
    pos++;
  if (pos >= jpeg_size)
    return false;
  segment->marker_offset = pos;
  segment->marker = jpeg[pos++];
  if (segment->marker == 0x01 || (segment->marker >= 0xD0 && segment->marker <= 0xD9))
    return false;
  if (pos + 2 > jpeg_size)
    return false;
  const uint16_t length = read_u16_be(jpeg + pos);
  if (length < 2 || length > jpeg_size - pos)
    return false;
  segment->end = pos + length;
  *position = segment->end;
  return true;
}

struct HuffmanDecoder {
  std::array<uint8_t, 16> counts{};
  std::array<uint16_t, 16> first_codes{};
  std::array<uint16_t, 16> first_symbols{};
  const uint8_t *symbols{nullptr};
  uint16_t symbol_count{0};
  bool valid{false};

  bool configure(const uint8_t *table_counts, const uint8_t *table_symbols, size_t available_symbols) {
    uint32_t code = 0;
    uint16_t symbol_index = 0;
    for (size_t length_index = 0; length_index < this->counts.size(); length_index++) {
      const uint8_t count = table_counts[length_index];
      this->counts[length_index] = count;
      this->first_codes[length_index] = static_cast<uint16_t>(code);
      this->first_symbols[length_index] = symbol_index;
      if (symbol_index + count > available_symbols || code + count > (1UL << (length_index + 1)))
        return false;
      symbol_index += count;
      code = (code + count) << 1;
    }
    this->symbols = table_symbols;
    this->symbol_count = symbol_index;
    this->valid = true;
    return true;
  }
};

struct FrameComponent {
  uint8_t id{0};
  uint8_t horizontal_sampling{0};
  uint8_t vertical_sampling{0};
};

struct ScanComponent {
  uint8_t id{0};
  uint8_t dc_table{0};
  uint8_t ac_table{0};
  uint8_t output_table{0};
};

struct ParsedJpeg {
  uint16_t width{0};
  uint16_t height{0};
  std::array<FrameComponent, MAX_COMPONENTS> frame_components{};
  std::array<ScanComponent, MAX_COMPONENTS> scan_components{};
  uint8_t component_count{0};
  uint16_t restart_interval{0};
  size_t scan_data_offset{0};
  std::array<std::array<HuffmanDecoder, HUFFMAN_TABLE_IDS>, HUFFMAN_TABLE_CLASSES> huffman_tables{};
  bool needs_normalization{false};
};

bool is_start_of_frame(uint8_t marker) {
  switch (marker) {
    case 0xC0:
    case 0xC1:
    case 0xC2:
    case 0xC3:
    case 0xC5:
    case 0xC6:
    case 0xC7:
    case 0xC9:
    case 0xCA:
    case 0xCB:
    case 0xCD:
    case 0xCE:
    case 0xCF:
      return true;
    default:
      return false;
  }
}

int find_frame_component(const ParsedJpeg &parsed, uint8_t component_id) {
  for (uint8_t index = 0; index < parsed.component_count; index++) {
    if (parsed.frame_components[index].id == component_id)
      return index;
  }
  return -1;
}

esp_err_t parse_jpeg(const uint8_t *jpeg, size_t jpeg_size, ParsedJpeg *parsed) {
  if (jpeg == nullptr || parsed == nullptr || jpeg_size < 4 || jpeg[0] != 0xFF || jpeg[1] != 0xD8)
    return ESP_ERR_INVALID_ARG;

  size_t position = 2;
  bool found_frame = false;
  while (position < jpeg_size) {
    HeaderSegment segment;
    if (!next_header_segment(jpeg, jpeg_size, &position, &segment))
      return ESP_ERR_INVALID_ARG;
    const uint8_t *payload = jpeg + segment.marker_offset + 3;
    const size_t payload_size = segment.end - (segment.marker_offset + 3);

    if (segment.marker == 0xC4) {
      size_t table_position = 0;
      while (table_position < payload_size) {
        if (payload_size - table_position < 17)
          return ESP_ERR_INVALID_ARG;
        const uint8_t table_spec = payload[table_position++];
        const uint8_t table_class = table_spec >> 4;
        const uint8_t table_id = table_spec & 0x0F;
        if (table_class >= HUFFMAN_TABLE_CLASSES || table_id >= HUFFMAN_TABLE_IDS)
          return ESP_ERR_NOT_SUPPORTED;
        const uint8_t *counts = payload + table_position;
        table_position += 16;
        size_t symbol_count = 0;
        for (size_t index = 0; index < 16; index++)
          symbol_count += counts[index];
        if (symbol_count > payload_size - table_position)
          return ESP_ERR_INVALID_ARG;
        if (!parsed->huffman_tables[table_class][table_id].configure(counts, payload + table_position,
                                                                     payload_size - table_position))
          return ESP_ERR_INVALID_ARG;
        table_position += symbol_count;
      }
    } else if (is_start_of_frame(segment.marker)) {
      if (segment.marker != 0xC0 && segment.marker != 0xC1)
        return ESP_ERR_NOT_SUPPORTED;
      if (payload_size < 6 || payload[0] != 8)
        return ESP_ERR_NOT_SUPPORTED;
      parsed->height = read_u16_be(payload + 1);
      parsed->width = read_u16_be(payload + 3);
      parsed->component_count = payload[5];
      if (parsed->width == 0 || parsed->height == 0 || parsed->component_count == 0 ||
          parsed->component_count > MAX_COMPONENTS || payload_size != 6 + parsed->component_count * 3)
        return ESP_ERR_NOT_SUPPORTED;
      for (uint8_t index = 0; index < parsed->component_count; index++) {
        const size_t offset = 6 + index * 3;
        const uint8_t sampling = payload[offset + 1];
        parsed->frame_components[index] = {
            .id = payload[offset],
            .horizontal_sampling = static_cast<uint8_t>(sampling >> 4),
            .vertical_sampling = static_cast<uint8_t>(sampling & 0x0F),
        };
        if (parsed->frame_components[index].horizontal_sampling == 0 ||
            parsed->frame_components[index].vertical_sampling == 0)
          return ESP_ERR_INVALID_ARG;
      }
      parsed->needs_normalization = segment.marker == 0xC1;
      found_frame = true;
    } else if (segment.marker == 0xDD) {
      if (payload_size != 2)
        return ESP_ERR_INVALID_ARG;
      parsed->restart_interval = read_u16_be(payload);
    } else if (segment.marker == 0xDA) {
      if (!found_frame || payload_size < 4 || payload[0] != parsed->component_count ||
          payload_size != 1 + parsed->component_count * 2 + 3)
        return ESP_ERR_NOT_SUPPORTED;
      for (uint8_t index = 0; index < parsed->component_count; index++) {
        const uint8_t component_id = payload[1 + index * 2];
        const uint8_t selectors = payload[2 + index * 2];
        const int frame_index = find_frame_component(*parsed, component_id);
        if (frame_index != index)
          return ESP_ERR_NOT_SUPPORTED;
        parsed->scan_components[index] = {
            .id = component_id,
            .dc_table = static_cast<uint8_t>(selectors >> 4),
            .ac_table = static_cast<uint8_t>(selectors & 0x0F),
            .output_table = static_cast<uint8_t>(index == 0 ? 0 : 1),
        };
        if (parsed->scan_components[index].dc_table >= HUFFMAN_TABLE_IDS ||
            parsed->scan_components[index].ac_table >= HUFFMAN_TABLE_IDS)
          return ESP_ERR_NOT_SUPPORTED;
        parsed->needs_normalization |=
            parsed->scan_components[index].dc_table > 1 || parsed->scan_components[index].ac_table > 1;
      }
      if (payload[payload_size - 3] != 0 || payload[payload_size - 2] != 63 || payload[payload_size - 1] != 0)
        return ESP_ERR_NOT_SUPPORTED;
      parsed->scan_data_offset = segment.end;
      if (!parsed->needs_normalization)
        return ESP_OK;
      for (uint8_t index = 0; index < parsed->component_count; index++) {
        const auto &scan = parsed->scan_components[index];
        if (!parsed->huffman_tables[0][scan.dc_table].valid || !parsed->huffman_tables[1][scan.ac_table].valid)
          return ESP_ERR_INVALID_ARG;
      }
      return ESP_OK;
    }
  }
  return ESP_ERR_INVALID_ARG;
}

class BitReader {
 public:
  BitReader(const uint8_t *data, size_t size, size_t position) : data_(data), size_(size), position_(position) {}

  bool read_bits(uint8_t count, uint32_t *value) {
    uint32_t result = 0;
    for (uint8_t index = 0; index < count; index++) {
      uint8_t bit;
      if (!this->read_bit_(&bit))
        return false;
      result = (result << 1) | bit;
    }
    *value = result;
    return true;
  }

  bool read_symbol(const HuffmanDecoder &decoder, uint8_t *symbol) {
    uint16_t code = 0;
    for (uint8_t length = 1; length <= 16; length++) {
      uint8_t bit;
      if (!this->read_bit_(&bit))
        return false;
      code = static_cast<uint16_t>((code << 1) | bit);
      const uint8_t count = decoder.counts[length - 1];
      const uint16_t first_code = decoder.first_codes[length - 1];
      if (code >= first_code && code - first_code < count) {
        const size_t symbol_index = decoder.first_symbols[length - 1] + code - first_code;
        if (symbol_index >= decoder.symbol_count)
          return false;
        *symbol = decoder.symbols[symbol_index];
        return true;
      }
    }
    return false;
  }

  bool consume_restart(uint8_t expected) {
    this->bits_left_ = 0;
    if (this->position_ >= this->size_ || this->data_[this->position_] != 0xFF)
      return false;
    while (this->position_ < this->size_ && this->data_[this->position_] == 0xFF)
      this->position_++;
    if (this->position_ >= this->size_ || this->data_[this->position_] != 0xD0 + expected)
      return false;
    this->position_++;
    return true;
  }

  bool finish() {
    this->bits_left_ = 0;
    while (this->position_ < this->size_ && this->data_[this->position_] == 0xFF)
      this->position_++;
    return this->position_ < this->size_ && this->data_[this->position_] == 0xD9;
  }

 protected:
  bool read_bit_(uint8_t *bit) {
    if (this->bits_left_ == 0) {
      if (this->position_ >= this->size_)
        return false;
      this->current_byte_ = this->data_[this->position_++];
      if (this->current_byte_ == 0xFF) {
        if (this->position_ >= this->size_ || this->data_[this->position_++] != 0)
          return false;
      }
      this->bits_left_ = 8;
    }
    this->bits_left_--;
    *bit = (this->current_byte_ >> this->bits_left_) & 1;
    return true;
  }

  const uint8_t *data_;
  size_t size_;
  size_t position_;
  uint8_t current_byte_{0};
  uint8_t bits_left_{0};
};

class CountingBitWriter {
 public:
  bool write_bits(uint32_t value, uint8_t count) {
    for (int shift = count - 1; shift >= 0; shift--) {
      this->current_byte_ = static_cast<uint8_t>((this->current_byte_ << 1) | ((value >> shift) & 1));
      this->bit_count_++;
      if (this->bit_count_ == 8) {
        this->emit_(this->current_byte_);
        this->current_byte_ = 0;
        this->bit_count_ = 0;
      }
    }
    return true;
  }

  bool write_symbol(const HuffmanEncoder &encoder, uint8_t symbol) {
    const EncodedCode encoded = encoder[symbol];
    return encoded.length != 0 && this->write_bits(encoded.code, encoded.length);
  }

  bool restart(uint8_t) {
    this->flush();
    this->size_ += 2;
    return true;
  }

  bool flush() {
    if (this->bit_count_ == 0)
      return true;
    const uint8_t padding = 8 - this->bit_count_;
    this->current_byte_ = static_cast<uint8_t>((this->current_byte_ << padding) | ((1U << padding) - 1));
    this->emit_(this->current_byte_);
    this->current_byte_ = 0;
    this->bit_count_ = 0;
    return true;
  }

  size_t size() const { return this->size_; }

 protected:
  void emit_(uint8_t value) { this->size_ += value == 0xFF ? 2 : 1; }

  size_t size_{0};
  uint8_t current_byte_{0};
  uint8_t bit_count_{0};
};

class BufferBitWriter {
 public:
  BufferBitWriter(uint8_t *output, size_t capacity) : output_(output), capacity_(capacity) {}

  bool write_bits(uint32_t value, uint8_t count) {
    for (int shift = count - 1; shift >= 0; shift--) {
      this->current_byte_ = static_cast<uint8_t>((this->current_byte_ << 1) | ((value >> shift) & 1));
      this->bit_count_++;
      if (this->bit_count_ == 8) {
        if (!this->emit_(this->current_byte_))
          return false;
        this->current_byte_ = 0;
        this->bit_count_ = 0;
      }
    }
    return true;
  }

  bool write_symbol(const HuffmanEncoder &encoder, uint8_t symbol) {
    const EncodedCode encoded = encoder[symbol];
    return encoded.length != 0 && this->write_bits(encoded.code, encoded.length);
  }

  bool restart(uint8_t number) {
    if (!this->flush() || this->size_ + 2 > this->capacity_)
      return false;
    this->output_[this->size_++] = 0xFF;
    this->output_[this->size_++] = 0xD0 + number;
    return true;
  }

  bool flush() {
    if (this->bit_count_ == 0)
      return true;
    const uint8_t padding = 8 - this->bit_count_;
    this->current_byte_ = static_cast<uint8_t>((this->current_byte_ << padding) | ((1U << padding) - 1));
    const bool result = this->emit_(this->current_byte_);
    this->current_byte_ = 0;
    this->bit_count_ = 0;
    return result;
  }

  size_t size() const { return this->size_; }

 protected:
  bool emit_(uint8_t value) {
    const size_t required = value == 0xFF ? 2 : 1;
    if (this->size_ + required > this->capacity_)
      return false;
    this->output_[this->size_++] = value;
    if (value == 0xFF)
      this->output_[this->size_++] = 0;
    return true;
  }

  uint8_t *output_;
  size_t capacity_;
  size_t size_{0};
  uint8_t current_byte_{0};
  uint8_t bit_count_{0};
};

const HuffmanEncoder &destination_encoder(uint8_t table_class, uint8_t table_id) {
  if (table_class == 0)
    return table_id == 0 ? DC_LUMA_ENCODER : DC_CHROMA_ENCODER;
  return table_id == 0 ? AC_LUMA_ENCODER : AC_CHROMA_ENCODER;
}

template<typename Writer>
bool transcode_entropy(const uint8_t *jpeg, size_t jpeg_size, const ParsedJpeg &parsed, Writer *writer) {
  uint8_t max_horizontal = 0;
  uint8_t max_vertical = 0;
  for (uint8_t index = 0; index < parsed.component_count; index++) {
    max_horizontal = std::max(max_horizontal, parsed.frame_components[index].horizontal_sampling);
    max_vertical = std::max(max_vertical, parsed.frame_components[index].vertical_sampling);
  }
  const size_t mcu_columns = (parsed.width + max_horizontal * 8 - 1) / (max_horizontal * 8);
  const size_t mcu_rows = (parsed.height + max_vertical * 8 - 1) / (max_vertical * 8);
  if (mcu_columns == 0 || mcu_rows == 0 || mcu_columns > std::numeric_limits<size_t>::max() / mcu_rows)
    return false;
  const size_t mcu_count = mcu_columns * mcu_rows;

  BitReader reader(jpeg, jpeg_size, parsed.scan_data_offset);
  uint8_t restart_number = 0;
  for (size_t mcu_index = 0; mcu_index < mcu_count; mcu_index++) {
    for (uint8_t component_index = 0; component_index < parsed.component_count; component_index++) {
      const FrameComponent &frame_component = parsed.frame_components[component_index];
      const ScanComponent &scan_component = parsed.scan_components[component_index];
      const HuffmanDecoder &source_dc = parsed.huffman_tables[0][scan_component.dc_table];
      const HuffmanDecoder &source_ac = parsed.huffman_tables[1][scan_component.ac_table];
      const HuffmanEncoder &output_dc = destination_encoder(0, scan_component.output_table);
      const HuffmanEncoder &output_ac = destination_encoder(1, scan_component.output_table);
      const size_t block_count = frame_component.horizontal_sampling * frame_component.vertical_sampling;
      for (size_t block = 0; block < block_count; block++) {
        uint8_t category;
        if (!reader.read_symbol(source_dc, &category) || category > 11 || !writer->write_symbol(output_dc, category))
          return false;
        if (category != 0) {
          uint32_t amplitude;
          if (!reader.read_bits(category, &amplitude) || !writer->write_bits(amplitude, category))
            return false;
        }

        uint8_t coefficient = 1;
        while (coefficient < 64) {
          uint8_t run_size;
          if (!reader.read_symbol(source_ac, &run_size) || !writer->write_symbol(output_ac, run_size))
            return false;
          if (run_size == 0)
            break;
          if (run_size == 0xF0) {
            coefficient += 16;
            if (coefficient > 64)
              return false;
            continue;
          }
          const uint8_t run = run_size >> 4;
          const uint8_t amplitude_size = run_size & 0x0F;
          if (amplitude_size == 0 || coefficient + run >= 64)
            return false;
          coefficient += run + 1;
          uint32_t amplitude;
          if (!reader.read_bits(amplitude_size, &amplitude) || !writer->write_bits(amplitude, amplitude_size))
            return false;
        }
      }
    }

    if (parsed.restart_interval != 0 && (mcu_index + 1) % parsed.restart_interval == 0 && mcu_index + 1 < mcu_count) {
      if (!reader.consume_restart(restart_number) || !writer->restart(restart_number))
        return false;
      restart_number = (restart_number + 1) & 7;
    }
  }
  return writer->flush() && reader.finish();
}

template<size_t N>
void write_dht_table(uint8_t **output, uint8_t table_class, uint8_t table_id, const std::array<uint8_t, 16> &counts,
                     const std::array<uint8_t, N> &symbols) {
  *(*output)++ = static_cast<uint8_t>((table_class << 4) | table_id);
  std::memcpy(*output, counts.data(), counts.size());
  *output += counts.size();
  std::memcpy(*output, symbols.data(), symbols.size());
  *output += symbols.size();
}

void write_standard_dht(uint8_t **output) {
  *(*output)++ = 0xFF;
  *(*output)++ = 0xC4;
  write_u16_be(*output, STANDARD_DHT_PAYLOAD_SIZE + 2);
  *output += 2;
  write_dht_table(output, 0, 0, DC_LUMA_COUNTS, DC_LUMA_SYMBOLS);
  write_dht_table(output, 1, 0, AC_LUMA_COUNTS, AC_LUMA_SYMBOLS);
  write_dht_table(output, 0, 1, DC_CHROMA_COUNTS, DC_CHROMA_SYMBOLS);
  write_dht_table(output, 1, 1, AC_CHROMA_COUNTS, AC_CHROMA_SYMBOLS);
}

esp_err_t calculate_header_size(const uint8_t *jpeg, size_t jpeg_size, size_t *header_size) {
  size_t size = 2 + STANDARD_DHT_SEGMENT_SIZE;
  size_t position = 2;
  while (position < jpeg_size) {
    HeaderSegment segment;
    if (!next_header_segment(jpeg, jpeg_size, &position, &segment))
      return ESP_ERR_INVALID_ARG;
    if (segment.marker != 0xC4) {
      if (segment.end - segment.start > std::numeric_limits<size_t>::max() - size)
        return ESP_ERR_INVALID_SIZE;
      size += segment.end - segment.start;
    }
    if (segment.marker == 0xDA) {
      *header_size = size;
      return ESP_OK;
    }
  }
  return ESP_ERR_INVALID_ARG;
}

esp_err_t write_normalized_header(const uint8_t *jpeg, size_t jpeg_size, const ParsedJpeg &parsed, uint8_t *output,
                                  size_t output_capacity, size_t *written) {
  if (output_capacity < 2)
    return ESP_ERR_INVALID_SIZE;
  output[0] = 0xFF;
  output[1] = 0xD8;
  size_t output_position = 2;
  size_t position = 2;
  while (position < jpeg_size) {
    HeaderSegment segment;
    if (!next_header_segment(jpeg, jpeg_size, &position, &segment))
      return ESP_ERR_INVALID_ARG;
    if (segment.marker == 0xC4)
      continue;
    if (segment.marker == 0xDA) {
      if (STANDARD_DHT_SEGMENT_SIZE > output_capacity - output_position)
        return ESP_ERR_INVALID_SIZE;
      uint8_t *dht_output = output + output_position;
      write_standard_dht(&dht_output);
      output_position += STANDARD_DHT_SEGMENT_SIZE;
    }
    const size_t segment_size = segment.end - segment.start;
    if (segment_size > output_capacity - output_position)
      return ESP_ERR_INVALID_SIZE;
    std::memcpy(output + output_position, jpeg + segment.start, segment_size);
    const size_t marker_in_segment = segment.marker_offset - segment.start;
    if (segment.marker == 0xC1)
      output[output_position + marker_in_segment] = 0xC0;
    if (segment.marker == 0xDA) {
      for (uint8_t index = 0; index < parsed.component_count; index++) {
        const size_t selector_in_segment = marker_in_segment + 5 + index * 2;
        output[output_position + selector_in_segment] = index == 0 ? 0x00 : 0x11;
      }
    }
    output_position += segment_size;
    if (segment.marker == 0xDA) {
      *written = output_position;
      return ESP_OK;
    }
  }
  return ESP_ERR_INVALID_ARG;
}

}  // namespace

esp_err_t normalize_huffman_for_hardware(const uint8_t *jpeg, size_t jpeg_size, JpegBuffer *output, bool *normalized) {
  if (output == nullptr || normalized == nullptr)
    return ESP_ERR_INVALID_ARG;
  *normalized = false;
  output->release();

  ParsedJpeg parsed;
  esp_err_t err = parse_jpeg(jpeg, jpeg_size, &parsed);
  if (err != ESP_OK || !parsed.needs_normalization)
    return err;

  size_t header_size = 0;
  err = calculate_header_size(jpeg, jpeg_size, &header_size);
  if (err != ESP_OK)
    return err;

  auto allocate_buffer = [](size_t capacity) -> uint8_t * {
    uint8_t *data = static_cast<uint8_t *>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (data == nullptr)
      data = static_cast<uint8_t *>(heap_caps_malloc(capacity, MALLOC_CAP_8BIT));
    return data;
  };

  /* The old path decoded the complete entropy stream twice: once merely to
   * count output bytes and once to write them. On an 800 px Immich thumbnail
   * that needlessly read compressed data from PSRAM bit-by-bit for hundreds
   * of milliseconds while DSI and PPA were animating the current photo.
   *
   * Standard JPEG Huffman output is normally close to the input size. Give a
   * single-pass writer a conservative 3x entropy allowance first, then retain
   * the exact-size counting path only as a compatibility fallback for an
   * unusually pathological table. This changes no DCT data or image quality. */
  const size_t source_entropy_size =
      jpeg_size > parsed.scan_data_offset + 2 ? jpeg_size - parsed.scan_data_offset - 2 : 0;
  constexpr size_t FAST_ENTROPY_EXPANSION = 3;
  if (source_entropy_size <= (std::numeric_limits<size_t>::max() - header_size - 2) / FAST_ENTROPY_EXPANSION) {
    const size_t fast_capacity = header_size + source_entropy_size * FAST_ENTROPY_EXPANSION + 2;
    uint8_t *fast_data = allocate_buffer(fast_capacity);
    if (fast_data != nullptr) {
      size_t header_written = 0;
      err = write_normalized_header(jpeg, jpeg_size, parsed, fast_data, fast_capacity, &header_written);
      if (err == ESP_OK && header_written + 2 <= fast_capacity) {
        BufferBitWriter writer(fast_data + header_written, fast_capacity - header_written - 2);
        if (transcode_entropy(jpeg, jpeg_size, parsed, &writer)) {
          const size_t entropy_end = header_written + writer.size();
          fast_data[entropy_end] = 0xFF;
          fast_data[entropy_end + 1] = 0xD9;
          output->reset(fast_data, entropy_end + 2, fast_capacity);
          *normalized = true;
          return ESP_OK;
        }
      }
      heap_caps_free(fast_data);
    }
  }

  CountingBitWriter counter;
  if (!transcode_entropy(jpeg, jpeg_size, parsed, &counter))
    return ESP_ERR_INVALID_ARG;
  if (counter.size() > std::numeric_limits<size_t>::max() - header_size - 2)
    return ESP_ERR_INVALID_SIZE;
  const size_t normalized_size = header_size + counter.size() + 2;

  uint8_t *normalized_data = allocate_buffer(normalized_size);
  if (normalized_data == nullptr)
    return ESP_ERR_NO_MEM;

  size_t header_written = 0;
  err = write_normalized_header(jpeg, jpeg_size, parsed, normalized_data, normalized_size, &header_written);
  if (err != ESP_OK) {
    heap_caps_free(normalized_data);
    return err;
  }
  BufferBitWriter writer(normalized_data + header_written, normalized_size - header_written - 2);
  if (!transcode_entropy(jpeg, jpeg_size, parsed, &writer) || writer.size() != counter.size()) {
    heap_caps_free(normalized_data);
    return ESP_ERR_INVALID_ARG;
  }
  const size_t entropy_end = header_written + writer.size();
  normalized_data[entropy_end] = 0xFF;
  normalized_data[entropy_end + 1] = 0xD9;

  output->reset(normalized_data, normalized_size, normalized_size);
  *normalized = true;
  return ESP_OK;
}

}  // namespace esphome::esp32_jpeg

#endif  // USE_ESP32_JPEG

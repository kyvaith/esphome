#pragma once

#include "esphome/components/json/json_util.h"
#include "esphome/core/component.h"

#include <string>
#include <vector>

namespace esphome::immich_gallery {

struct ImmichPhoto {
  std::string asset_id;
  std::string image_url;
  std::string title;
  std::string date;
  std::string location;
  std::string person;
  bool is_portrait{false};
  bool orientation_known{false};
};

class ImmichGallery : public Component {
 public:
  void dump_config() override;

  std::string trim_url(std::string url) const;
  bool valid_base_url(const std::string &url) const;
  std::string search_body(int size, const std::string &source, const std::string &album_ids,
                          const std::string &person_ids, const std::string &tag_ids, const std::string &date_from,
                          const std::string &date_to) const;
  bool source_ready(const std::string &source, const std::string &album_ids, const std::string &person_ids,
                    const std::string &tag_ids) const;
  bool parse_asset_response(const std::string &body, const std::string &base_url, const std::string &orientation_filter,
                            ImmichPhoto *out, const std::string &avoid_asset_ids = "") const;
  std::string push_recent_asset(const std::string &recent, const std::string &asset_id, size_t max_entries = 3) const;
  bool parse_memory_response(const std::string &body, const std::string &base_url, ImmichPhoto *out) const;
  std::string first_csv_value(const std::string &csv) const;
  bool parse_album_response(const std::string &body, std::string *name) const;

 protected:
  static std::vector<std::string> split_csv_(const std::string &csv);
  static std::string pick_csv_(const std::string &csv);
  static std::string json_array_from_csv_(const std::string &csv);
  static bool orientation_matches_(const ImmichPhoto &photo, const std::string &filter);
  static std::string parse_date_(const std::string &raw);
  static std::string parse_asset_object_(JsonObject asset, const std::string &base_url, ImmichPhoto *out);
};

}  // namespace esphome::immich_gallery

#include "immich_gallery.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <utility>

namespace esphome::immich_gallery {

static const char *const TAG = "immich_gallery";

void ImmichGallery::dump_config() { ESP_LOGCONFIG(TAG, "Immich Gallery"); }

std::string ImmichGallery::trim_url(std::string url) const {
  while (!url.empty() && (url.back() == '/' || url.back() == ' '))
    url.pop_back();
  while (!url.empty() && url.front() == ' ')
    url.erase(url.begin());
  return url;
}

bool ImmichGallery::valid_base_url(const std::string &url) const {
  return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

std::vector<std::string> ImmichGallery::split_csv_(const std::string &csv) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start < csv.size()) {
    size_t end = csv.find(',', start);
    if (end == std::string::npos)
      end = csv.size();
    size_t first = start;
    size_t last = end;
    while (first < last && csv[first] == ' ')
      first++;
    while (last > first && csv[last - 1] == ' ')
      last--;
    if (first < last)
      out.emplace_back(csv.substr(first, last - first));
    start = end + 1;
  }
  return out;
}

std::string ImmichGallery::pick_csv_(const std::string &csv) {
  auto values = split_csv_(csv);
  if (values.empty())
    return "";
  if (values.size() == 1)
    return values[0];
  return values[random_uint32() % values.size()];
}

std::string ImmichGallery::json_array_from_csv_(const std::string &csv) {
  auto values = split_csv_(csv);
  std::string body = "[";
  for (size_t i = 0; i < values.size(); i++) {
    if (i != 0)
      body += ",";
    body += "\"" + values[i] + "\"";
  }
  body += "]";
  return body;
}

std::string ImmichGallery::search_body(int size, const std::string &source, const std::string &album_ids,
                                       const std::string &person_ids, const std::string &tag_ids,
                                       const std::string &date_from, const std::string &date_to) const {
  std::string body = "{\"size\":" + std::to_string(size) +
                     ",\"type\":\"IMAGE\",\"visibility\":\"timeline\",\"withExif\":true,\"withPeople\":true";
  if (!date_from.empty())
    body += ",\"takenAfter\":\"" + date_from + "T00:00:00.000Z\"";
  if (!date_to.empty())
    body += ",\"takenBefore\":\"" + date_to + "T23:59:59.999Z\"";
  if (source == "Favorites") {
    body += ",\"isFavorite\":true";
  } else if (source == "Album") {
    body += ",\"albumIds\":" + json_array_from_csv_(album_ids);
  } else if (source == "Person") {
    const std::string person = pick_csv_(person_ids);
    if (!person.empty())
      body += ",\"personIds\":[\"" + person + "\"]";
  } else if (source == "Tag") {
    body += ",\"tagIds\":" + json_array_from_csv_(tag_ids);
  }
  body += "}";
  return body;
}

bool ImmichGallery::source_ready(const std::string &source, const std::string &album_ids, const std::string &person_ids,
                                 const std::string &tag_ids) const {
  if (source == "Album")
    return !split_csv_(album_ids).empty();
  if (source == "Person")
    return !split_csv_(person_ids).empty();
  if (source == "Tag")
    return !split_csv_(tag_ids).empty();
  return true;
}

bool ImmichGallery::orientation_matches_(const ImmichPhoto &photo, const std::string &filter) {
  if (filter == "Any" || filter.empty())
    return true;
  if (!photo.orientation_known)
    return false;
  if (filter == "Portrait Only")
    return photo.is_portrait;
  if (filter == "Landscape Only")
    return !photo.is_portrait;
  return true;
}

std::string ImmichGallery::parse_date_(const std::string &raw) {
  if (raw.size() < 10)
    return "";
  return raw.substr(0, 10);
}

std::string ImmichGallery::parse_asset_object_(JsonObject asset, const std::string &base_url, ImmichPhoto *out) {
  if (out == nullptr || asset.isNull() || !asset["id"].is<const char *>())
    return "";
  out->asset_id = asset["id"].as<std::string>();
  out->image_url = base_url + "/api/assets/" + out->asset_id + "/original";
  out->date = asset["localDateTime"].is<const char *>() ? parse_date_(asset["localDateTime"].as<std::string>()) : "";

  JsonObject exif = asset["exifInfo"].as<JsonObject>();
  if (!exif.isNull()) {
    std::string city = exif["city"].is<const char *>() ? exif["city"].as<std::string>() : "";
    std::string country = exif["country"].is<const char *>() ? exif["country"].as<std::string>() : "";
    if (!city.empty() && !country.empty())
      out->location = city + ", " + country;
    else
      out->location = !city.empty() ? city : country;

    int width = exif["exifImageWidth"].is<int>() ? exif["exifImageWidth"].as<int>() : 0;
    int height = exif["exifImageHeight"].is<int>() ? exif["exifImageHeight"].as<int>() : 0;
    std::string orientation = exif["orientation"].is<const char *>() ? exif["orientation"].as<std::string>() : "";
    if (orientation == "5" || orientation == "6" || orientation == "7" || orientation == "8")
      std::swap(width, height);
    if (width > 0 && height > 0) {
      out->orientation_known = true;
      out->is_portrait = height > width;
    }
  }

  JsonArray people = asset["people"].as<JsonArray>();
  if (!people.isNull() && people.size() > 0) {
    JsonObject person = people[0].as<JsonObject>();
    if (person["name"].is<const char *>())
      out->person = person["name"].as<std::string>();
  }
  return out->image_url;
}

bool ImmichGallery::parse_asset_response(const std::string &body, const std::string &base_url,
                                         const std::string &orientation_filter, ImmichPhoto *out,
                                         const std::string &avoid_asset_ids) const {
  if (out == nullptr)
    return false;
  auto doc = json::parse_json(body);
  if (doc.isNull())
    return false;

  std::vector<ImmichPhoto> candidates;
  auto collect_asset = [&](JsonObject object) {
    ImmichPhoto candidate;
    if (parse_asset_object_(object, base_url, &candidate).empty())
      return;
    if (!orientation_matches_(candidate, orientation_filter))
      return;
    candidates.push_back(std::move(candidate));
  };
  auto is_avoided = [&](const std::string &asset_id) {
    size_t start = 0;
    while (start <= avoid_asset_ids.size()) {
      const size_t end = avoid_asset_ids.find('|', start);
      const size_t length = end == std::string::npos ? avoid_asset_ids.size() - start : end - start;
      if (length == asset_id.size() && avoid_asset_ids.compare(start, length, asset_id) == 0)
        return true;
      if (end == std::string::npos)
        break;
      start = end + 1;
    }
    return false;
  };

  if (doc.is<JsonArray>()) {
    JsonArray array = doc.as<JsonArray>();
    for (size_t i = 0; i < array.size(); i++)
      collect_asset(array[i].as<JsonObject>());
  } else if (doc.is<JsonObject>()) {
    JsonObject root = doc.as<JsonObject>();
    JsonObject assets = root["assets"].as<JsonObject>();
    JsonArray items;
    if (!assets.isNull())
      items = assets["items"].as<JsonArray>();
    if (!items.isNull()) {
      for (size_t i = 0; i < items.size(); i++)
        collect_asset(items[i].as<JsonObject>());
    } else {
      collect_asset(root);
    }
  }

  if (candidates.empty())
    return false;
  std::vector<size_t> eligible;
  eligible.reserve(candidates.size());
  for (size_t i = 0; i < candidates.size(); i++) {
    if (!is_avoided(candidates[i].asset_id))
      eligible.push_back(i);
  }
  const size_t selected =
      eligible.empty() ? random_uint32() % candidates.size() : eligible[random_uint32() % eligible.size()];
  *out = std::move(candidates[selected]);
  return true;
}

std::string ImmichGallery::push_recent_asset(const std::string &recent, const std::string &asset_id,
                                             size_t max_entries) const {
  std::vector<std::string> entries;
  size_t start = 0;
  while (start <= recent.size()) {
    const size_t end = recent.find('|', start);
    const std::string entry = recent.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!entry.empty() && entry != asset_id)
      entries.push_back(entry);
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  entries.push_back(asset_id);
  if (entries.size() > max_entries)
    entries.erase(entries.begin(), entries.begin() + (entries.size() - max_entries));
  std::string result;
  for (const auto &entry : entries) {
    if (!result.empty())
      result += '|';
    result += entry;
  }
  return result;
}

bool ImmichGallery::parse_memory_response(const std::string &body, const std::string &base_url,
                                          ImmichPhoto *out) const {
  if (out == nullptr)
    return false;
  auto doc = json::parse_json(body);
  if (doc.isNull() || !doc.is<JsonArray>())
    return false;
  std::vector<JsonObject> candidates;
  JsonArray memories = doc.as<JsonArray>();
  for (size_t memory_index = 0; memory_index < memories.size(); memory_index++) {
    JsonObject memory = memories[memory_index].as<JsonObject>();
    JsonArray assets = memory["assets"].as<JsonArray>();
    if (assets.isNull())
      continue;
    for (size_t asset_index = 0; asset_index < assets.size(); asset_index++) {
      JsonObject asset = assets[asset_index].as<JsonObject>();
      if (asset["type"].is<const char *>() && asset["type"].as<std::string>() != "IMAGE")
        continue;
      candidates.push_back(asset);
    }
  }
  if (candidates.empty())
    return false;
  return !parse_asset_object_(candidates[random_uint32() % candidates.size()], base_url, out).empty();
}

}  // namespace esphome::immich_gallery

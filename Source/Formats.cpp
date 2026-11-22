/**
 * @file Formats.cpp
 * @brief Implements Dataset file serialization and directory traversal.
 *
 * JSON, TOML, and YAML use bundled parsers; CSV is parsed locally. Loading
 * constructs a replacement Dataset before modifying the caller's value.
 * YAML output uses JSON-compatible scalar encoding and adds comments in flow
 * style when needed. Directory traversal follows links while tracking the
 * canonical paths of ancestors to detect cycles.
 */

#include <yaml.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <locale>
#include <regex>
#include <set>
#include <sstream>
#include <utility>

#include "Dataset.h"
#include "nlohmann/json.hpp"
#include "toml.hpp"

namespace {

using Json = nlohmann::json;
using JsonAdapter = decltype(nlohmann::detail::input_adapter(
    std::declval<const std::string&>()));
using JsonDomReader = nlohmann::detail::json_sax_dom_parser<Json, JsonAdapter>;

/**
 * @brief Reject integer tokens that the JSON parser falls back to double.
 *
 * The parser otherwise converts integers beyond its integer range to
 * floating-point values, silently losing their original integer type.
 */
class JsonReader : public JsonDomReader {
 public:
  explicit JsonReader(Json& result) : JsonDomReader(result, true) {}

  bool number_float(double value, const std::string& text) {
    if (text.find_first_of(".eE") == std::string::npos)
      throw std::runtime_error("JSON integer outside int64 range");
    return JsonDomReader::number_float(value, text);
  }
};

std::string FileExtension(const std::string& path) {
  const auto dot = path.find_last_of('.');
  const auto slash = path.find_last_of("/\\");
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
    return "";
  std::string extension = path.substr(dot + 1);
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) {
                   return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
                 });
  return extension;
}

bool IsSupportedFormat(const std::string& extension) {
  return extension == "csv" || extension == "json" || extension == "yaml" ||
         extension == "yml" || extension == "toml";
}

std::string Format(const std::string& path) {
  const auto dot = path.find_last_of('.');
  const auto slash = path.find_last_of("/\\");
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
    throw std::runtime_error("File has no supported extension: " + path);
  const std::string extension = FileExtension(path);
  if (!IsSupportedFormat(extension)) {
    throw std::runtime_error("Unsupported file extension: " + path);
  }
  return extension;
}

void DirectoryWarning(const std::filesystem::path& path,
                      const std::string& reason) {
  std::cerr << "Dataset: skipping " << path.string() << ": " << reason << '\n';
}

/**
 * @brief Collect supported file paths while following directory symlinks.
 *
 * Canonical paths are tracked only for the current ancestry, preventing cycles
 * while allowing the same target to be reached through different paths.
 * Failure to open the root throws; failures below it are reported and skipped.
 *
 * @param directory Directory path whose spelling is retained in collected
 * paths.
 * @param recursive Whether to descend into subdirectories.
 * @param root Whether this is the caller-supplied starting directory.
 * @param ancestors Canonical directories in the current traversal path.
 * @param files Destination for supported regular-file paths.
 */
void CollectFiles(const std::filesystem::path& directory, bool recursive,
                  bool root, std::set<std::filesystem::path>& ancestors,
                  std::vector<std::filesystem::path>& files) {
  namespace fs = std::filesystem;
  std::error_code error;
  const auto canonical = fs::canonical(directory, error);
  if (error) {
    if (root)
      throw std::runtime_error("Cannot open directory " + directory.string() +
                               ": " + error.message());
    DirectoryWarning(directory, error.message());
    return;
  }
  if (!ancestors.insert(canonical).second) {
    DirectoryWarning(directory, "directory link cycle");
    return;
  }
  fs::directory_iterator iterator(directory, error);
  if (error) {
    if (root)
      throw std::runtime_error("Cannot open directory " + directory.string() +
                               ": " + error.message());
    DirectoryWarning(directory, error.message());
    ancestors.erase(canonical);
    return;
  }
  const fs::directory_iterator end;
  while (iterator != end) {
    const auto entry = *iterator;
    const auto status = entry.status(error);
    if (error) {
      DirectoryWarning(entry.path(), error.message());
      error.clear();
    } else if (fs::is_directory(status)) {
      if (recursive) CollectFiles(entry.path(), true, false, ancestors, files);
    } else if (fs::is_regular_file(status) &&
               IsSupportedFormat(FileExtension(entry.path().string()))) {
      files.push_back(entry.path());
    }
    iterator.increment(error);
    if (error) {
      DirectoryWarning(directory, error.message());
      break;
    }
  }
  ancestors.erase(canonical);
}

Dataset FromJson(const Json& value) {
  if (value.is_null()) return nullptr;
  if (value.is_boolean()) return value.get<bool>();
  if (value.is_number_unsigned()) {
    const auto number = value.get<std::uint64_t>();
    if (number >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      throw std::runtime_error("Integer outside int64 range");
    }
    return static_cast<std::int64_t>(number);
  }
  if (value.is_number_integer()) return value.get<std::int64_t>();
  if (value.is_number_float()) {
    const double number = value.get<double>();
    if (!std::isfinite(number)) throw std::runtime_error("Non-finite number");
    return number;
  }
  if (value.is_string()) return value.get<std::string>();
  if (value.is_array()) {
    Dataset::Array result;
    for (const auto& item : value) result.push_back(FromJson(item));
    return result;
  }
  if (value.is_object()) {
    Dataset::Map result;
    for (auto it = value.begin(); it != value.end(); ++it) {
      result.emplace(it.key(), FromJson(it.value()));
    }
    return result;
  }
  throw std::runtime_error("Unsupported JSON value");
}

Json ToJson(const Dataset& dataset) {
  const auto& value = dataset.value();
  if (std::holds_alternative<std::nullptr_t>(value)) return nullptr;
  if (const auto* item = std::get_if<bool>(&value)) return *item;
  if (const auto* item = std::get_if<std::int64_t>(&value)) return *item;
  if (const auto* item = std::get_if<double>(&value)) {
    if (!std::isfinite(*item)) throw std::runtime_error("Non-finite number");
    return *item;
  }
  if (const auto* item = std::get_if<std::string>(&value)) return *item;
  if (const auto* items = std::get_if<Dataset::Array>(&value)) {
    Json result = Json::array();
    for (const auto& item : *items) result.push_back(ToJson(item));
    return result;
  }
  Json result = Json::object();
  for (const auto& item : std::get<Dataset::Map>(value)) {
    result[item.first] = ToJson(item.second);
  }
  return result;
}

/**
 * @brief Validate and split comment text into safe YAML/TOML comment lines.
 *
 * Each output line has a comment marker, including empty lines and embedded
 * line breaks, so comment text cannot introduce document content.
 * @throws std::runtime_error for unsupported control or line separator bytes.
 * @throws Json::exception if the comment is not valid UTF-8.
 */
std::vector<std::string> CommentLines(const Dataset& dataset) {
  std::vector<std::string> lines;
  for (const auto& comment : dataset.comments()) {
    // Validate UTF-8 before emitting it outside a quoted string.
    Json(comment).dump();
    for (std::size_t i = 0; i < comment.size(); ++i) {
      const auto c = static_cast<unsigned char>(comment[i]);
      if ((c < 0x20 && c != '\t' && c != '\r' && c != '\n') || c == 0x7f ||
          (c == 0xc2 && i + 1 < comment.size() &&
           static_cast<unsigned char>(comment[i + 1]) >= 0x80 &&
           static_cast<unsigned char>(comment[i + 1]) <= 0x9f) ||
          (c == 0xe2 && comment.compare(i, 3, "\xe2\x80\xa8") == 0) ||
          (c == 0xe2 && comment.compare(i, 3, "\xe2\x80\xa9") == 0) ||
          (c == 0xef && comment.compare(i, 3, "\xef\xbf\xbe") == 0) ||
          (c == 0xef && comment.compare(i, 3, "\xef\xbf\xbf") == 0)) {
        throw std::runtime_error("Unsupported character in comment");
      }
    }
    std::size_t start = 0;
    for (std::size_t i = 0; i < comment.size(); ++i) {
      if (comment[i] == '\r' || comment[i] == '\n') {
        lines.push_back("# " + comment.substr(start, i - start));
        if (comment[i] == '\r' && i + 1 < comment.size() &&
            comment[i + 1] == '\n')
          ++i;
        start = i + 1;
      }
    }
    lines.push_back("# " + comment.substr(start));
  }
  return lines;
}

bool HasComments(const Dataset& dataset) {
  if (!dataset.comments().empty()) return true;
  if (const auto* array = std::get_if<Dataset::Array>(&dataset.value())) {
    for (const auto& item : *array)
      if (HasComments(item)) return true;
  }
  if (const auto* map = std::get_if<Dataset::Map>(&dataset.value())) {
    for (const auto& item : *map)
      if (HasComments(item.second)) return true;
  }
  return false;
}

void AppendYamlComments(const Dataset& dataset, std::size_t indent,
                        std::string& output) {
  for (const auto& line : CommentLines(dataset))
    output += std::string(indent, ' ') + line + "\n";
}

/**
 * @brief Serialize a value in YAML flow style with comments at child nodes.
 *
 * Scalars and keys use JSON encoding for unambiguous types and escaping.
 * The caller emits this value's own comments before writing the value.
 */
void AppendYamlValue(const Dataset& dataset, std::size_t indent,
                     std::string& output) {
  if (const auto* map = std::get_if<Dataset::Map>(&dataset.value())) {
    if (map->empty()) {
      output += "{}";
      return;
    }
    output += "{\n";
    for (auto it = map->begin(); it != map->end(); ++it) {
      AppendYamlComments(it->second, indent + 2, output);
      output += std::string(indent + 2, ' ') + Json(it->first).dump() + ": ";
      AppendYamlValue(it->second, indent + 2, output);
      if (std::next(it) != map->end()) output += ',';
      output += '\n';
    }
    output += std::string(indent, ' ') + "}";
  } else if (const auto* array =
                 std::get_if<Dataset::Array>(&dataset.value())) {
    if (array->empty()) {
      output += "[]";
      return;
    }
    output += "[\n";
    for (auto it = array->begin(); it != array->end(); ++it) {
      AppendYamlComments(*it, indent + 2, output);
      output += std::string(indent + 2, ' ');
      AppendYamlValue(*it, indent + 2, output);
      if (std::next(it) != array->end()) output += ',';
      output += '\n';
    }
    output += std::string(indent, ' ') + "]";
  } else {
    output += ToJson(dataset).dump();
  }
}

std::string SaveYaml(const Dataset& dataset) {
  if (!HasComments(dataset)) return ToJson(dataset).dump(2) + "\n";
  std::string output;
  AppendYamlComments(dataset, 0, output);
  AppendYamlValue(dataset, 0, output);
  return output + "\n";
}

Dataset FromToml(const toml::value& value) {
  if (value.is_boolean()) return value.as_boolean();
  if (value.is_integer()) return static_cast<std::int64_t>(value.as_integer());
  if (value.is_floating()) {
    const double number = value.as_floating();
    if (!std::isfinite(number)) throw std::runtime_error("Non-finite number");
    return number;
  }
  if (value.is_string()) return value.as_string();
  if (value.is_array()) {
    Dataset::Array result;
    for (const auto& item : value.as_array()) result.push_back(FromToml(item));
    return result;
  }
  if (value.is_table()) {
    Dataset::Map result;
    for (const auto& item : value.as_table()) {
      result.emplace(item.first, FromToml(item.second));
    }
    return result;
  }
  throw std::runtime_error("TOML date/time values are not supported");
}

/**
 * @brief Transfer validated comments to a TOML value.
 *
 * A commented array of tables must use multiline array syntax so the array's
 * own comment can be written. Comments on inline-table members may be omitted
 * by the TOML serializer.
 */
toml::value WithTomlComments(toml::value value, const Dataset& dataset) {
  for (const auto& line : CommentLines(dataset))
    value.comments().push_back(line);
  if (value.is_array() && !value.comments().empty() &&
      value.is_array_of_tables())
    value.as_array_fmt().fmt = toml::array_format::multiline;
  return value;
}

toml::value ToToml(const Dataset& dataset) {
  const auto& value = dataset.value();
  if (const auto* item = std::get_if<bool>(&value))
    return WithTomlComments(toml::value(*item), dataset);
  if (const auto* item = std::get_if<std::int64_t>(&value))
    return WithTomlComments(toml::value(*item), dataset);
  if (const auto* item = std::get_if<double>(&value)) {
    if (!std::isfinite(*item)) throw std::runtime_error("Non-finite number");
    return WithTomlComments(toml::value(*item), dataset);
  }
  if (const auto* item = std::get_if<std::string>(&value))
    return WithTomlComments(toml::value(*item), dataset);
  if (const auto* items = std::get_if<Dataset::Array>(&value)) {
    toml::array result;
    for (const auto& item : *items) result.push_back(ToToml(item));
    return WithTomlComments(toml::value(std::move(result)), dataset);
  }
  if (const auto* items = std::get_if<Dataset::Map>(&value)) {
    toml::table result;
    for (const auto& item : *items)
      result.emplace(item.first, ToToml(item.second));
    return WithTomlComments(toml::value(std::move(result)), dataset);
  }
  throw std::runtime_error("TOML does not support null");
}

/**
 * @brief Parse CSV rows, including quoted fields and embedded line breaks.
 * @throws std::runtime_error for malformed quoting or a bare carriage return
 *     outside a quoted field.
 */
std::vector<std::vector<std::string>> ParseCsv(const std::string& input) {
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> row;
  std::string field;
  bool quoted = false;
  bool closed = false;
  bool started = false;
  for (std::size_t i = 0; i < input.size(); ++i) {
    const char c = input[i];
    if (quoted) {
      if (c == '"') {
        if (i + 1 < input.size() && input[i + 1] == '"') {
          field += '"';
          ++i;
        } else {
          quoted = false;
          closed = true;
        }
      } else {
        field += c;
      }
      continue;
    }
    if (c == ',' || c == '\r' || c == '\n') {
      row.push_back(std::move(field));
      field.clear();
      closed = false;
      started = false;
      if (c != ',') {
        if (c == '\r') {
          if (i + 1 >= input.size() || input[i + 1] != '\n')
            throw std::runtime_error("CSV contains a bare carriage return");
          ++i;
        }
        rows.push_back(std::move(row));
        row.clear();
      }
    } else if (c == '"' && !started && !closed) {
      quoted = true;
      started = true;
    } else {
      if (closed || c == '"') throw std::runtime_error("Invalid CSV quoting");
      field += c;
      started = true;
    }
  }
  if (quoted) throw std::runtime_error("Unterminated CSV quoted field");
  if (started || closed || !row.empty()) {
    row.push_back(std::move(field));
    rows.push_back(std::move(row));
  }
  return rows;
}

Dataset LoadCsv(const std::string& input) {
  auto rows = ParseCsv(input);
  Dataset::Array records;
  if (rows.empty()) return records;
  const auto& header = rows.front();
  Dataset::Map keys;
  for (const auto& key : header) {
    if (!keys.emplace(key, nullptr).second)
      throw std::runtime_error("Duplicate CSV header: " + key);
  }
  for (std::size_t i = 1; i < rows.size(); ++i) {
    if (rows[i].size() != header.size())
      throw std::runtime_error("CSV row has a different number of fields");
    Dataset::Map record;
    for (std::size_t j = 0; j < header.size(); ++j)
      record.emplace(header[j], std::move(rows[i][j]));
    records.emplace_back(std::move(record));
  }
  return records;
}

std::string CsvField(const std::string& value) {
  if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
  std::string result = "\"";
  for (char c : value) {
    if (c == '"') result += '"';
    result += c;
  }
  return result + '"';
}

std::string SaveCsv(const Dataset& data) {
  const auto* rows = std::get_if<Dataset::Array>(&data.value());
  if (!rows) throw std::runtime_error("CSV requires an array of records");
  if (rows->empty()) return "";
  const auto* header = std::get_if<Dataset::Map>(&rows->front().value());
  if (!header || header->empty())
    throw std::runtime_error("CSV requires nonempty record maps");
  std::string result;
  bool first = true;
  for (const auto& entry : *header) {
    if (!first) result += ',';
    result += CsvField(entry.first);
    first = false;
  }
  result += "\r\n";
  for (const auto& row : *rows) {
    const auto* record = std::get_if<Dataset::Map>(&row.value());
    if (!record || record->size() != header->size())
      throw std::runtime_error("CSV records must have identical keys");
    first = true;
    for (const auto& entry : *header) {
      const auto it = record->find(entry.first);
      if (it == record->end())
        throw std::runtime_error("CSV records must have identical keys");
      const auto* value = std::get_if<std::string>(&it->second.value());
      if (!value) throw std::runtime_error("CSV fields must be strings");
      if (!first) result += ',';
      result += CsvField(*value);
      first = false;
    }
    result += "\r\n";
  }
  return result;
}

std::string YamlString(const yaml_char_t* text) {
  return text ? reinterpret_cast<const char*>(text) : "";
}

/**
 * @brief Resolve a YAML scalar into a Dataset value.
 *
 * Plain, untagged scalars use the supported YAML core types; quoted scalars
 * remain strings unless an explicit tag specifies a supported type.
 * @throws std::runtime_error for unsupported tags or out-of-range values.
 */
Dataset YamlScalar(const std::string& text, const std::string& tag,
                   bool plain) {
  const std::string prefix = "tag:yaml.org,2002:";
  if (!tag.empty() && tag != "!" && tag != prefix + "str" &&
      tag != prefix + "null" && tag != prefix + "bool" &&
      tag != prefix + "int" && tag != prefix + "float") {
    throw std::runtime_error("Unsupported YAML scalar tag: " + tag);
  }
  if (tag == prefix + "str" || tag == "!" || (tag.empty() && !plain))
    return text;
  const bool implicit = tag.empty();
  static const std::regex null_pattern("^(null|Null|NULL|~|)$");
  static const std::regex bool_pattern("^(true|True|TRUE|false|False|FALSE)$");
  static const std::regex int_pattern(
      "^[-+]?(0o[0-7]+|0x[0-9a-fA-F]+|[0-9]+)$");
  static const std::regex float_pattern(
      "^[-+]?(([0-9]+\\.[0-9]*|\\.[0-9]+)([eE][-+]?[0-9]+)?|"
      "[0-9]+[eE][-+]?[0-9]+|\\.(inf|Inf|INF|nan|NaN|NAN))$");
  if ((implicit || tag == prefix + "null") &&
      std::regex_match(text, null_pattern))
    return nullptr;
  if ((implicit || tag == prefix + "bool") &&
      std::regex_match(text, bool_pattern))
    return text[0] == 't' || text[0] == 'T';
  if ((implicit || tag == prefix + "int") &&
      std::regex_match(text, int_pattern)) {
    std::size_t offset = (text[0] == '-' || text[0] == '+') ? 1 : 0;
    const bool negative = text[0] == '-';
    int base = 10;
    if (text.compare(offset, 2, "0x") == 0) {
      base = 16;
      offset += 2;
    } else if (text.compare(offset, 2, "0o") == 0) {
      base = 8;
      offset += 2;
    }
    std::uint64_t magnitude = 0;
    const auto parsed = std::from_chars(
        text.data() + offset, text.data() + text.size(), magnitude, base);
    const auto limit =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (parsed.ec != std::errc{} || magnitude > limit + (negative ? 1 : 0))
      throw std::runtime_error("YAML integer outside int64 range");
    if (negative && magnitude == limit + 1)
      return std::numeric_limits<std::int64_t>::min();
    const auto number = static_cast<std::int64_t>(magnitude);
    return negative ? -number : number;
  }
  if ((implicit || tag == prefix + "float") &&
      (std::regex_match(text, float_pattern) ||
       (tag == prefix + "float" && std::regex_match(text, int_pattern)))) {
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double number;
    if (!(stream >> number) || !stream.eof() || !std::isfinite(number))
      throw std::runtime_error("Unsupported YAML floating-point value");
    return number;
  }
  if (!implicit) throw std::runtime_error("Invalid YAML value for tag: " + tag);
  return text;
}

/**
 * @brief Parse one YAML document using libyaml events.
 *
 * Completed anchors are stored for aliases. Recursive aliases, duplicate
 * anchors, non-scalar map keys, and multiple documents are rejected.
 */
class YamlReader {
 public:
  explicit YamlReader(const std::string& input) {
    if (!yaml_parser_initialize(&parser_))
      throw std::runtime_error("Unable to initialize YAML parser");
    yaml_parser_set_input_string(
        &parser_, reinterpret_cast<const unsigned char*>(input.data()),
        input.size());
  }
  ~YamlReader() {
    if (has_event_) yaml_event_delete(&event_);
    yaml_parser_delete(&parser_);
  }
  YamlReader(const YamlReader&) = delete;
  YamlReader& operator=(const YamlReader&) = delete;

  Dataset Read() {
    Next();
    Expect(YAML_STREAM_START_EVENT);
    Next();
    if (event_.type == YAML_STREAM_END_EVENT) return nullptr;
    Expect(YAML_DOCUMENT_START_EVENT);
    Next();
    Dataset result = ReadNode();
    Expect(YAML_DOCUMENT_END_EVENT);
    Next();
    Expect(YAML_STREAM_END_EVENT);
    return result;
  }

 private:
  void Next() {
    if (has_event_) {
      yaml_event_delete(&event_);
      has_event_ = false;
    }
    if (!yaml_parser_parse(&parser_, &event_)) {
      throw std::runtime_error(std::string("Invalid YAML: ") +
                               (parser_.problem ? parser_.problem : "error"));
    }
    has_event_ = true;
  }
  void Expect(yaml_event_type_t type) const {
    if (event_.type != type)
      throw std::runtime_error("Unexpected YAML event or multiple documents");
  }
  void CheckTag(const yaml_char_t* tag, const std::string& expected) {
    const std::string name = YamlString(tag);
    if (!name.empty() && name != "!" && name != "tag:yaml.org,2002:" + expected)
      throw std::runtime_error("Unsupported YAML collection tag: " + name);
  }
  void ReserveAnchor(const std::string& anchor) {
    if (!anchor.empty() && !anchor_names_.insert(anchor).second)
      throw std::runtime_error("Duplicate YAML anchor: " + anchor);
  }
  Dataset ReadNode() {
    if (event_.type == YAML_ALIAS_EVENT) {
      const auto it = anchors_.find(YamlString(event_.data.alias.anchor));
      if (it == anchors_.end())
        throw std::runtime_error("Unknown or recursive YAML alias");
      Dataset result = it->second;
      Next();
      return result;
    }
    std::string anchor;
    Dataset result;
    if (event_.type == YAML_SCALAR_EVENT) {
      const auto& scalar = event_.data.scalar;
      anchor = YamlString(scalar.anchor);
      ReserveAnchor(anchor);
      result = YamlScalar(
          std::string(reinterpret_cast<const char*>(scalar.value),
                      scalar.length),
          YamlString(scalar.tag), scalar.style == YAML_PLAIN_SCALAR_STYLE);
      Next();
    } else if (event_.type == YAML_SEQUENCE_START_EVENT) {
      anchor = YamlString(event_.data.sequence_start.anchor);
      ReserveAnchor(anchor);
      CheckTag(event_.data.sequence_start.tag, "seq");
      Dataset::Array items;
      Next();
      while (event_.type != YAML_SEQUENCE_END_EVENT)
        items.push_back(ReadNode());
      Next();
      result = std::move(items);
    } else if (event_.type == YAML_MAPPING_START_EVENT) {
      anchor = YamlString(event_.data.mapping_start.anchor);
      ReserveAnchor(anchor);
      CheckTag(event_.data.mapping_start.tag, "map");
      Dataset::Map items;
      Next();
      while (event_.type != YAML_MAPPING_END_EVENT) {
        if (event_.type != YAML_SCALAR_EVENT)
          throw std::runtime_error("YAML map keys must be scalar strings");
        const auto& key = event_.data.scalar;
        const auto tag = YamlString(key.tag);
        if (!tag.empty() && tag != "!" && tag != "tag:yaml.org,2002:str")
          throw std::runtime_error("YAML map keys must be strings");
        std::string name(reinterpret_cast<const char*>(key.value), key.length);
        const std::string key_anchor = YamlString(key.anchor);
        ReserveAnchor(key_anchor);
        if (!key_anchor.empty()) anchors_.emplace(key_anchor, Dataset(name));
        Next();
        Dataset value = ReadNode();
        if (!items.emplace(name, std::move(value)).second)
          throw std::runtime_error("Duplicate YAML map key: " + name);
      }
      Next();
      result = std::move(items);
    } else {
      throw std::runtime_error("Unsupported YAML node");
    }
    if (!anchor.empty()) anchors_.insert_or_assign(anchor, result);
    return result;
  }

  yaml_parser_t parser_{};
  yaml_event_t event_{};
  bool has_event_ = false;
  std::map<std::string, Dataset> anchors_;
  std::set<std::string> anchor_names_;
};

}  // namespace

void Dataset::Load(const std::string& path) {
  const std::string format = Format(path);
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot open file for reading: " + path);
  std::string content;
  char buffer[8192];
  while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0) {
    content.append(buffer, static_cast<std::size_t>(input.gcount()));
  }
  if (input.bad() || !input.eof())
    throw std::runtime_error("Cannot read file: " + path);
  Dataset result;
  try {
    if (format == "csv") {
      result = LoadCsv(content);
    } else if (format == "json") {
      Json parsed;
      JsonReader reader(parsed);
      if (!Json::sax_parse(content, &reader))
        throw std::runtime_error("Invalid JSON in " + path);
      result = FromJson(parsed);
    } else if (format == "yaml" || format == "yml") {
      result = YamlReader(content).Read();
    } else {
      std::istringstream stream(content);
      result = FromToml(toml::parse(stream, path));
    }
  } catch (const Json::exception& error) {
    throw std::runtime_error("Invalid JSON in " + path + ": " + error.what());
  } catch (const toml::exception& error) {
    throw std::runtime_error("Invalid TOML in " + path + ": " + error.what());
  }
  *this = std::move(result);
}

std::list<std::pair<std::string, Dataset>> Dataset::LoadDirectory(
    const std::string& path, bool recursive) {
  std::set<std::filesystem::path> ancestors;
  std::vector<std::filesystem::path> files;
  CollectFiles(path, recursive, true, ancestors, files);
  std::sort(files.begin(), files.end());
  std::list<std::pair<std::string, Dataset>> result;
  for (const auto& file : files) {
    const std::string name = file.string();
    Dataset dataset;
    try {
      dataset.Load(name);
    } catch (const std::runtime_error& error) {
      DirectoryWarning(file, error.what());
      continue;
    }
    result.emplace_back(name, std::move(dataset));
  }
  return result;
}

void Dataset::Save(const std::string& path) const {
  const std::string format = Format(path);
  std::string content;
  try {
    if (format == "csv") {
      content = SaveCsv(*this);
    } else if (format == "json") {
      content = ToJson(*this).dump(2) + "\n";
    } else if (format == "yaml" || format == "yml") {
      content = SaveYaml(*this);
    } else {
      if (!std::holds_alternative<Map>(value_))
        throw std::runtime_error("TOML requires a root table");
      content = toml::format(ToToml(*this));
    }
  } catch (const Json::exception& error) {
    throw std::runtime_error("Cannot serialize " + path + ": " + error.what());
  } catch (const toml::exception& error) {
    throw std::runtime_error("Cannot serialize " + path + ": " + error.what());
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("Cannot open file for writing: " + path);
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  output.close();
  if (!output) throw std::runtime_error("Cannot write file: " + path);
}

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
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <locale>
#include <random>
#include <set>
#include <sstream>
#include <utility>

#include "cppdatasetutil.h"
#if CPPDATASETUTIL_CPLUSPLUS_ >= 201703L
#include <filesystem>
#else
#include "ghc/filesystem.hpp"
#endif
#include "nlohmann/json.hpp"
#include "toml.hpp"

namespace {
#if CPPDATASETUTIL_CPLUSPLUS_ >= 201703L
namespace fs = std::filesystem;
#else
namespace fs = ghc::filesystem;
#endif

using Json = nlohmann::json;
constexpr std::size_t kMaxDepth = Dataset::kMaxNestingDepth;
// Total nodes that YAML aliases may expand to, guarding against alias bombs.
constexpr std::size_t kMaxAliasNodes = 1000000;
// TOML is parsed recursively by toml11, so deeper input is rejected up front.
constexpr std::size_t kMaxTomlDepth = 2 * Dataset::kMaxNestingDepth;

/**
 * @brief Build a Dataset from JSON parser events without recursion.
 *
 * Containers nested deeper than kMaxDepth are not built; their compact JSON
 * text is kept as a string value instead. Integer tokens that the parser would
 * otherwise fall back to double are rejected to preserve the integer type.
 */
class JsonBuilder {
 public:
  explicit JsonBuilder(Dataset& result) : result_(result) {}

  bool null() { return Add(nullptr, "null"); }
  bool boolean(bool value) { return Add(value, value ? "true" : "false"); }
  bool number_integer(Json::number_integer_t value) {
    return Add(static_cast<std::int64_t>(value), std::to_string(value));
  }
  bool number_unsigned(Json::number_unsigned_t value) {
    if (raw_depth_ == 0 &&
        value > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()))
      throw std::runtime_error("Integer outside int64 range");
    return Add(static_cast<std::int64_t>(value), std::to_string(value));
  }
  bool number_float(Json::number_float_t value, const std::string& text) {
    if (text.find_first_of(".eE") == std::string::npos)
      throw std::runtime_error("JSON integer outside int64 range");
    if (raw_depth_ == 0 && !std::isfinite(value))
      throw std::runtime_error("Non-finite number");
    return Add(value, text);
  }
  bool string(std::string& value) { return Add(value, Json(value).dump()); }
  bool binary(Json::binary_t&) { throw std::runtime_error("Unsupported JSON"); }
  bool start_object(std::size_t) { return Start(true); }
  bool key(std::string& value) {
    if (raw_depth_ > 0) {
      Separate();
      raw_ += Json(value).dump() + ':';
      after_key_ = true;
    } else {
      stack_.back().key = value;
    }
    return true;
  }
  bool end_object() { return End('}'); }
  bool start_array(std::size_t) { return Start(false); }
  bool end_array() { return End(']'); }
  template <typename Error>
  bool parse_error(std::size_t, const std::string&, const Error& error) {
    throw error;
  }

 private:
  struct Frame {
    bool is_map;
    Dataset::Map map;
    Dataset::Array array;
    std::string key;
  };

  void Separate() {
    if (after_key_) {
      after_key_ = false;
    } else {
      if (!first_.back()) raw_ += ',';
      first_.back() = false;
    }
  }

  bool Add(Dataset value, const std::string& text) {
    if (raw_depth_ > 0) {
      Separate();
      raw_ += text;
    } else if (stack_.empty()) {
      result_ = std::move(value);
    } else if (stack_.back().is_map) {
      stack_.back().map[stack_.back().key] = std::move(value);
    } else {
      stack_.back().array.push_back(std::move(value));
    }
    return true;
  }

  bool Start(bool is_map) {
    if (raw_depth_ == 0 && stack_.size() < kMaxDepth) {
      stack_.push_back({is_map, {}, {}, {}});
      return true;
    }
    if (raw_depth_ > 0) Separate();
    ++raw_depth_;
    first_.push_back(true);
    raw_ += is_map ? '{' : '[';
    return true;
  }

  bool End(char close) {
    if (raw_depth_ > 0) {
      raw_ += close;
      first_.pop_back();
      if (--raw_depth_ > 0) return true;
      after_key_ = false;
      std::string text = std::move(raw_);
      raw_.clear();
      return Add(std::move(text), "");
    }
    Frame frame = std::move(stack_.back());
    stack_.pop_back();
    return Add(frame.is_map ? Dataset(std::move(frame.map))
                            : Dataset(std::move(frame.array)),
               "");
  }

  Dataset& result_;
  std::vector<Frame> stack_;
  std::size_t raw_depth_ = 0;
  std::string raw_;
  std::vector<bool> first_;
  bool after_key_ = false;
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

/// Interpret a std::string path as UTF-8 on every platform. Without this,
/// Windows would use the ANSI code page and fail on non-ASCII names.
fs::path PathFromUtf8(const std::string& path) { return fs::u8path(path); }

std::string PathToUtf8(const fs::path& path) { return path.u8string(); }

void DirectoryWarning(const fs::path& path, const std::string& reason) {
  std::cerr << "Dataset: skipping " << PathToUtf8(path) << ": " << reason
            << '\n';
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
void CollectFiles(const fs::path& directory, bool recursive, bool root,
                  std::set<fs::path>& ancestors, std::vector<fs::path>& files) {
  std::error_code error;
  const auto canonical = fs::canonical(directory, error);
  if (error) {
    if (root)
      throw std::runtime_error("Cannot open directory " +
                               PathToUtf8(directory) + ": " + error.message());
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
      throw std::runtime_error("Cannot open directory " +
                               PathToUtf8(directory) + ": " + error.message());
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
               IsSupportedFormat(FileExtension(PathToUtf8(entry.path())))) {
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

Json ToJson(const Dataset& dataset) {
  const auto& value = dataset.value();
  if (cppdatasetutil::holds_alternative<std::nullptr_t>(value)) return nullptr;
  if (const auto* item = cppdatasetutil::get_if<bool>(&value)) return *item;
  if (const auto* item = cppdatasetutil::get_if<std::int64_t>(&value))
    return *item;
  if (const auto* item = cppdatasetutil::get_if<double>(&value)) {
    if (!std::isfinite(*item)) throw std::runtime_error("Non-finite number");
    return *item;
  }
  if (const auto* item = cppdatasetutil::get_if<std::string>(&value))
    return *item;
  if (const auto* items = cppdatasetutil::get_if<Dataset::Array>(&value)) {
    Json result = Json::array();
    for (const auto& item : *items) result.push_back(ToJson(item));
    return result;
  }
  Json result = Json::object();
  for (const auto& item : cppdatasetutil::get<Dataset::Map>(value)) {
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
  if (const auto* array =
          cppdatasetutil::get_if<Dataset::Array>(&dataset.value())) {
    for (const auto& item : *array)
      if (HasComments(item)) return true;
  }
  if (const auto* map =
          cppdatasetutil::get_if<Dataset::Map>(&dataset.value())) {
    for (const auto& item : *map)
      if (HasComments(item.second)) return true;
  }
  return false;
}

/// libyaml rejects implicit (single-line) keys longer than 1024 characters;
/// longer encoded keys are written with the explicit "? key" syntax.
constexpr std::size_t kMaxSimpleKeyLength = 512;

bool HasLongKey(const Dataset& dataset) {
  if (const auto* array =
          cppdatasetutil::get_if<Dataset::Array>(&dataset.value())) {
    for (const auto& item : *array)
      if (HasLongKey(item)) return true;
  }
  if (const auto* map =
          cppdatasetutil::get_if<Dataset::Map>(&dataset.value())) {
    for (const auto& item : *map) {
      if (Json(item.first).dump().size() > kMaxSimpleKeyLength) return true;
      if (HasLongKey(item.second)) return true;
    }
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
  if (const auto* map =
          cppdatasetutil::get_if<Dataset::Map>(&dataset.value())) {
    if (map->empty()) {
      output += "{}";
      return;
    }
    output += "{\n";
    for (auto it = map->begin(); it != map->end(); ++it) {
      AppendYamlComments(it->second, indent + 2, output);
      const std::string key = Json(it->first).dump();
      if (key.size() > kMaxSimpleKeyLength) {
        output += std::string(indent + 2, ' ') + "? " + key + "\n" +
                  std::string(indent + 2, ' ') + ": ";
      } else {
        output += std::string(indent + 2, ' ') + key + ": ";
      }
      AppendYamlValue(it->second, indent + 2, output);
      if (std::next(it) != map->end()) output += ',';
      output += '\n';
    }
    output += std::string(indent, ' ') + "}";
  } else if (const auto* array =
                 cppdatasetutil::get_if<Dataset::Array>(&dataset.value())) {
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
  if (!HasComments(dataset) && !HasLongKey(dataset))
    return ToJson(dataset).dump(2) + "\n";
  std::string output;
  AppendYamlComments(dataset, 0, output);
  AppendYamlValue(dataset, 0, output);
  return output + "\n";
}

/**
 * @brief Convert a TOML value, keeping containers beyond the nesting limit as
 * compact JSON text.
 *
 * Recursion is bounded because CheckTomlDepth() rejects deeper input.
 */
Dataset FromToml(const toml::value& value, std::size_t depth, bool collapse) {
  if (value.is_boolean()) return value.as_boolean();
  if (value.is_integer()) return static_cast<std::int64_t>(value.as_integer());
  if (value.is_floating()) {
    const double number = value.as_floating();
    if (!std::isfinite(number)) throw std::runtime_error("Non-finite number");
    return number;
  }
  if (value.is_string()) return value.as_string();
  if (value.is_array() || value.is_table()) {
    if (collapse && depth >= kMaxDepth)
      return ToJson(FromToml(value, 0, false)).dump();
    if (value.is_array()) {
      Dataset::Array result;
      for (const auto& item : value.as_array())
        result.push_back(FromToml(item, depth + 1, collapse));
      return result;
    }
    Dataset::Map result;
    for (const auto& item : value.as_table())
      result.emplace(item.first, FromToml(item.second, depth + 1, collapse));
    return result;
  }
  throw std::runtime_error("TOML date/time values are not supported");
}

/**
 * @brief Reject TOML text whose nesting could exhaust the parser's stack.
 *
 * This approximates nesting from inline brackets, table header keys, and
 * dotted keys while skipping strings and comments. Valid documents within
 * kMaxTomlDepth are never rejected.
 */
void CheckTomlDepth(const std::string& text) {
  struct Frame {
    char type;
    std::size_t carried;
  };
  std::vector<Frame> frames;
  std::size_t carried_sum = 0;
  std::size_t key_dots = 0;
  bool in_key = true;
  bool header = false;
  bool line_start = true;
  const auto check = [&](std::size_t extra) {
    if (frames.size() + carried_sum + extra + 1 > kMaxTomlDepth)
      throw std::runtime_error("TOML nesting is too deep");
  };
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '#') {
      while (i < text.size() && text[i] != '\n') ++i;
      --i;
    } else if (c == '"' || c == '\'') {
      const bool multiline = text.compare(i, 3, std::string(3, c)) == 0;
      i += multiline ? 3 : 1;
      for (; i < text.size(); ++i) {
        if (c == '"' && text[i] == '\\') {
          ++i;
        } else if (text[i] == c &&
                   (!multiline || text.compare(i, 3, std::string(3, c)) == 0)) {
          if (multiline) {
            // Quotes directly before the closing delimiter belong to the text.
            while (i + 3 < text.size() && text[i + 3] == c) ++i;
            i += 2;
          }
          break;
        } else if (!multiline && text[i] == '\n') {
          break;
        }
      }
      line_start = false;
    } else if (c == '\n') {
      header = false;
      in_key = frames.empty() || frames.back().type == '{';
      if (frames.empty()) key_dots = 0;
      line_start = true;
    } else if (c == '[' && line_start && frames.empty()) {
      header = true;
      in_key = true;
      key_dots = 0;
      line_start = false;
    } else if (c == ']' && header) {
      check(key_dots);
      header = false;
      in_key = false;
    } else if (c == '[' || c == '{') {
      check(key_dots);
      frames.push_back({c, key_dots});
      carried_sum += key_dots;
      key_dots = 0;
      in_key = c == '{';
      line_start = false;
    } else if ((c == ']' || c == '}') && !frames.empty()) {
      carried_sum -= frames.back().carried;
      frames.pop_back();
      in_key = !frames.empty() && frames.back().type == '{';
      key_dots = 0;
    } else if (c == '.' && in_key) {
      ++key_dots;
    } else if (c == '=' && in_key && !header) {
      check(key_dots);
      in_key = false;
    } else if (c == ',' && !frames.empty()) {
      in_key = frames.back().type == '{';
      key_dots = 0;
    } else if (c != ' ' && c != '\t' && c != '\r') {
      line_start = false;
    }
  }
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
  if (const auto* item = cppdatasetutil::get_if<bool>(&value))
    return WithTomlComments(toml::value(*item), dataset);
  if (const auto* item = cppdatasetutil::get_if<std::int64_t>(&value))
    return WithTomlComments(toml::value(*item), dataset);
  if (const auto* item = cppdatasetutil::get_if<double>(&value)) {
    if (!std::isfinite(*item)) throw std::runtime_error("Non-finite number");
    return WithTomlComments(toml::value(*item), dataset);
  }
  if (const auto* item = cppdatasetutil::get_if<std::string>(&value))
    return WithTomlComments(toml::value(*item), dataset);
  if (const auto* items = cppdatasetutil::get_if<Dataset::Array>(&value)) {
    toml::array result;
    for (const auto& item : *items) result.push_back(ToToml(item));
    return WithTomlComments(toml::value(std::move(result)), dataset);
  }
  if (const auto* items = cppdatasetutil::get_if<Dataset::Map>(&value)) {
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
    if ((c == '\r' || c == '\n') && row.empty() && field.empty() && !started &&
        !closed) {
      // A blank line carries no record.
      if (c == '\r') {
        if (i + 1 >= input.size() || input[i + 1] != '\n')
          throw std::runtime_error("CSV contains a bare carriage return");
        ++i;
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
  const bool bom = input.compare(0, 3, "\xEF\xBB\xBF") == 0;
  auto rows = ParseCsv(bom ? input.substr(3) : input);
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

std::string CsvField(const std::string& value, bool quote_empty = false) {
  if (value.empty() && quote_empty) return "\"\"";
  if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
  std::string result = "\"";
  for (char c : value) {
    if (c == '"') result += '"';
    result += c;
  }
  return result + '"';
}

std::string SaveCsv(const Dataset& data) {
  const auto* rows = cppdatasetutil::get_if<Dataset::Array>(&data.value());
  if (!rows) throw std::runtime_error("CSV requires an array of records");
  if (rows->empty()) return "";
  const auto* header =
      cppdatasetutil::get_if<Dataset::Map>(&rows->front().value());
  if (!header || header->empty())
    throw std::runtime_error("CSV requires nonempty record maps");
  std::string result;
  bool first = true;
  for (const auto& entry : *header) {
    if (!first) result += ',';
    result += CsvField(entry.first, header->size() == 1);
    first = false;
  }
  result += "\r\n";
  for (const auto& row : *rows) {
    const auto* record = cppdatasetutil::get_if<Dataset::Map>(&row.value());
    if (!record || record->size() != header->size())
      throw std::runtime_error("CSV records must have identical keys");
    first = true;
    for (const auto& entry : *header) {
      const auto it = record->find(entry.first);
      if (it == record->end())
        throw std::runtime_error("CSV records must have identical keys");
      const auto* value =
          cppdatasetutil::get_if<std::string>(&it->second.value());
      if (!value) throw std::runtime_error("CSV fields must be strings");
      if (!first) result += ',';
      result += CsvField(*value, header->size() == 1);
      first = false;
    }
    result += "\r\n";
  }
  return result;
}

/// Count the container levels of a value, using recursion bounded by the
/// nesting limit.
std::size_t Depth(const Dataset& dataset) {
  std::size_t deepest = 0;
  if (const auto* array =
          cppdatasetutil::get_if<Dataset::Array>(&dataset.value())) {
    for (const auto& item : *array) deepest = std::max(deepest, Depth(item));
    return deepest + 1;
  }
  if (const auto* map =
          cppdatasetutil::get_if<Dataset::Map>(&dataset.value())) {
    for (const auto& item : *map)
      deepest = std::max(deepest, Depth(item.second));
    return deepest + 1;
  }
  return 0;
}

/// Count a value and everything nested in it, using recursion bounded by the
/// nesting limit.
std::size_t CountNodes(const Dataset& dataset) {
  std::size_t count = 1;
  if (const auto* array =
          cppdatasetutil::get_if<Dataset::Array>(&dataset.value())) {
    for (const auto& item : *array) count += CountNodes(item);
  } else if (const auto* map =
                 cppdatasetutil::get_if<Dataset::Map>(&dataset.value())) {
    for (const auto& item : *map) count += CountNodes(item.second);
  }
  return count;
}

/// Check iteratively whether a value nests containers deeper than the limit.
bool ExceedsDepth(const Dataset& root, std::size_t limit) {
  std::vector<std::pair<const Dataset*, std::size_t>> pending{{&root, 0}};
  while (!pending.empty()) {
    const Dataset* node = pending.back().first;
    const std::size_t level = pending.back().second;
    pending.pop_back();
    if (const auto* array =
            cppdatasetutil::get_if<Dataset::Array>(&node->value())) {
      if (level + 1 > limit) return true;
      for (const auto& item : *array) pending.emplace_back(&item, level + 1);
    } else if (const auto* map =
                   cppdatasetutil::get_if<Dataset::Map>(&node->value())) {
      if (level + 1 > limit) return true;
      for (const auto& item : *map)
        pending.emplace_back(&item.second, level + 1);
    }
  }
  return false;
}

std::string YamlString(const yaml_char_t* text) {
  return text ? reinterpret_cast<const char*>(text) : "";
}

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// Advance past a run of characters matching the predicate.
template <typename Predicate>
std::size_t SkipWhile(const std::string& text, std::size_t i,
                      Predicate matches) {
  while (i < text.size() && matches(text[i])) ++i;
  return i;
}

bool IsYamlNull(const std::string& text) {
  return text.empty() || text == "~" || text == "null" || text == "Null" ||
         text == "NULL";
}

bool IsYamlBool(const std::string& text) {
  return text == "true" || text == "True" || text == "TRUE" ||
         text == "false" || text == "False" || text == "FALSE";
}

// Match [-+]?(0o[0-7]+|0x[0-9a-fA-F]+|[0-9]+).
bool IsYamlInt(const std::string& text) {
  std::size_t i = (!text.empty() && (text[0] == '-' || text[0] == '+')) ? 1 : 0;
  std::size_t end;
  if (text.compare(i, 2, "0o") == 0) {
    end = SkipWhile(text, i + 2, [](char c) { return c >= '0' && c <= '7'; });
    i += 2;
  } else if (text.compare(i, 2, "0x") == 0) {
    end = SkipWhile(text, i + 2, [](char c) {
      return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
    i += 2;
  } else {
    end = SkipWhile(text, i, IsDigit);
  }
  return end > i && end == text.size();
}

// Match [-+]?(digits.digits?|.digits)(exponent)?, digits exponent, or
// .inf/.nan in the YAML core spellings.
bool IsYamlFloat(const std::string& text) {
  std::size_t i = (!text.empty() && (text[0] == '-' || text[0] == '+')) ? 1 : 0;
  const std::string rest = text.substr(i);
  if (rest == ".inf" || rest == ".Inf" || rest == ".INF" || rest == ".nan" ||
      rest == ".NaN" || rest == ".NAN")
    return true;
  const std::size_t whole_end = SkipWhile(text, i, IsDigit);
  const bool has_whole = whole_end > i;
  i = whole_end;
  bool has_point = false;
  if (i < text.size() && text[i] == '.') {
    has_point = true;
    const std::size_t fraction_end = SkipWhile(text, i + 1, IsDigit);
    if (!has_whole && fraction_end == i + 1) return false;
    i = fraction_end;
  } else if (!has_whole) {
    return false;
  }
  if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
    ++i;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) ++i;
    const std::size_t exponent_end = SkipWhile(text, i, IsDigit);
    if (exponent_end == i) return false;
    i = exponent_end;
  } else if (!has_point) {
    return false;
  }
  return i == text.size();
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
  if ((implicit || tag == prefix + "null") && IsYamlNull(text)) return nullptr;
  if ((implicit || tag == prefix + "bool") && IsYamlBool(text))
    return text[0] == 't' || text[0] == 'T';
  if ((implicit || tag == prefix + "int") && IsYamlInt(text)) {
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
    bool overflow = text.size() == offset;
    for (std::size_t i = offset; i < text.size() && !overflow; ++i) {
      const char c = text[i];
      int digit = -1;
      if (c >= '0' && c <= '9')
        digit = c - '0';
      else if (c >= 'a' && c <= 'f')
        digit = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        digit = c - 'A' + 10;
      if (digit < 0 || digit >= base) {
        overflow = true;
      } else if (magnitude > (std::numeric_limits<std::uint64_t>::max() -
                              static_cast<std::uint64_t>(digit)) /
                                 static_cast<std::uint64_t>(base)) {
        overflow = true;
      } else {
        magnitude = magnitude * static_cast<std::uint64_t>(base) +
                    static_cast<std::uint64_t>(digit);
      }
    }
    const auto limit =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (overflow || magnitude > limit + (negative ? 1 : 0))
      throw std::runtime_error("YAML integer outside int64 range");
    if (negative && magnitude == limit + 1)
      return std::numeric_limits<std::int64_t>::min();
    const auto number = static_cast<std::int64_t>(magnitude);
    return negative ? -number : number;
  }
  if ((implicit || tag == prefix + "float") &&
      (IsYamlFloat(text) || (tag == prefix + "float" && IsYamlInt(text)))) {
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
    Dataset result = ReadNode(0);
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

  // Count an alias expansion against the budget before it is copied.
  void ChargeAlias(const Dataset& target) {
    alias_nodes_ += CountNodes(target);
    if (alias_nodes_ > kMaxAliasNodes)
      throw std::runtime_error("YAML alias expansion is too large");
  }

  // Validate a map key event and return its name.
  std::string ReadKey() {
    if (event_.type != YAML_SCALAR_EVENT)
      throw std::runtime_error("YAML map keys must be scalar strings");
    const auto& key = event_.data.scalar;
    const auto tag = YamlString(key.tag);
    if (!tag.empty() && tag != "!" && tag != "tag:yaml.org,2002:str")
      throw std::runtime_error("YAML map keys must be strings");
    return std::string(reinterpret_cast<const char*>(key.value), key.length);
  }

  /**
   * @brief Convert the collection starting at the current event to JSON text.
   *
   * Used beyond the nesting limit. Events are consumed iteratively; anchors
   * cannot be defined inside the collection, but aliases to earlier anchors
   * are expanded. The current event is left after the collection's end.
   */
  std::string ReadRawCollection() {
    struct Level {
      bool is_map;
      bool expect_key;
      bool first;
      std::set<std::string> keys;
    };
    std::vector<Level> levels;
    std::string text;
    auto expecting_key = [&]() {
      return !levels.empty() && levels.back().is_map &&
             levels.back().expect_key;
    };
    // Emit the separator for a value and advance the enclosing level.
    auto before_value = [&]() {
      if (levels.empty()) return;
      Level& level = levels.back();
      if (level.is_map) {
        level.expect_key = true;
      } else {
        if (!level.first) text += ',';
        level.first = false;
      }
    };
    auto reject_anchor = [&](const yaml_char_t* anchor) {
      if (!YamlString(anchor).empty())
        throw std::runtime_error(
            "YAML anchors are unsupported in deeply nested values");
    };
    do {
      switch (event_.type) {
        case YAML_SEQUENCE_START_EVENT:
        case YAML_MAPPING_START_EVENT: {
          const bool is_map = event_.type == YAML_MAPPING_START_EVENT;
          if (expecting_key())
            throw std::runtime_error("YAML map keys must be scalar strings");
          reject_anchor(is_map ? event_.data.mapping_start.anchor
                               : event_.data.sequence_start.anchor);
          CheckTag(is_map ? event_.data.mapping_start.tag
                          : event_.data.sequence_start.tag,
                   is_map ? "map" : "seq");
          before_value();
          levels.push_back({is_map, true, true, {}});
          text += is_map ? '{' : '[';
          break;
        }
        case YAML_SEQUENCE_END_EVENT:
        case YAML_MAPPING_END_EVENT:
          text += levels.back().is_map ? '}' : ']';
          levels.pop_back();
          break;
        case YAML_SCALAR_EVENT: {
          const auto& scalar = event_.data.scalar;
          reject_anchor(scalar.anchor);
          if (expecting_key()) {
            Level& level = levels.back();
            const std::string name = ReadKey();
            if (!level.keys.insert(name).second)
              throw std::runtime_error("Duplicate YAML map key: " + name);
            if (!level.first) text += ',';
            level.first = false;
            text += Json(name).dump() + ':';
            level.expect_key = false;
            break;
          }
          before_value();
          text +=
              ToJson(YamlScalar(std::string(
                                    reinterpret_cast<const char*>(scalar.value),
                                    scalar.length),
                                YamlString(scalar.tag),
                                scalar.style == YAML_PLAIN_SCALAR_STYLE))
                  .dump();
          break;
        }
        case YAML_ALIAS_EVENT: {
          const auto it = anchors_.find(YamlString(event_.data.alias.anchor));
          if (it == anchors_.end())
            throw std::runtime_error("Unknown or recursive YAML alias");
          if (expecting_key())
            throw std::runtime_error("YAML map keys must be scalar strings");
          before_value();
          ChargeAlias(it->second);
          text += ToJson(it->second).dump();
          break;
        }
        default:
          throw std::runtime_error("Unsupported YAML node");
      }
      Next();
    } while (!levels.empty());
    return text;
  }

  Dataset ReadNode(std::size_t depth) {
    if (event_.type == YAML_ALIAS_EVENT) {
      const auto it = anchors_.find(YamlString(event_.data.alias.anchor));
      if (it == anchors_.end())
        throw std::runtime_error("Unknown or recursive YAML alias");
      ChargeAlias(it->second);
      Dataset result = it->second;
      if (depth + Depth(result) > kMaxDepth) result = ToJson(result).dump();
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
      if (depth >= kMaxDepth) {
        result = ReadRawCollection();
      } else {
        Dataset::Array items;
        Next();
        while (event_.type != YAML_SEQUENCE_END_EVENT)
          items.push_back(ReadNode(depth + 1));
        Next();
        result = std::move(items);
      }
    } else if (event_.type == YAML_MAPPING_START_EVENT) {
      anchor = YamlString(event_.data.mapping_start.anchor);
      ReserveAnchor(anchor);
      CheckTag(event_.data.mapping_start.tag, "map");
      if (depth >= kMaxDepth) {
        result = ReadRawCollection();
      } else {
        Dataset::Map items;
        Next();
        while (event_.type != YAML_MAPPING_END_EVENT) {
          std::string name = ReadKey();
          const std::string key_anchor = YamlString(event_.data.scalar.anchor);
          ReserveAnchor(key_anchor);
          if (!key_anchor.empty()) anchors_.emplace(key_anchor, Dataset(name));
          Next();
          Dataset value = ReadNode(depth + 1);
          if (!items.emplace(name, std::move(value)).second)
            throw std::runtime_error("Duplicate YAML map key: " + name);
        }
        Next();
        result = std::move(items);
      }
    } else {
      throw std::runtime_error("Unsupported YAML node");
    }
    if (!anchor.empty()) anchors_[anchor] = result;
    return result;
  }

  yaml_parser_t parser_{};
  yaml_event_t event_{};
  bool has_event_ = false;
  std::map<std::string, Dataset> anchors_;
  std::set<std::string> anchor_names_;
  std::size_t alias_nodes_ = 0;
};

Dataset ParseContent(const std::string& content, const std::string& format,
                     const std::string& name) {
  try {
    if (format == "csv") return LoadCsv(content);
    if (format == "json") {
      Dataset result;
      JsonBuilder builder(result);
      if (!Json::sax_parse(content, &builder))
        throw std::runtime_error("Invalid JSON in " + name);
      return result;
    }
    if (format == "yaml" || format == "yml") return YamlReader(content).Read();
    CheckTomlDepth(content);
    std::istringstream stream(content);
    return FromToml(toml::parse(stream, name), 0, true);
  } catch (const Json::exception& error) {
    throw std::runtime_error("Invalid JSON in " + name + ": " + error.what());
  } catch (const toml::exception& error) {
    throw std::runtime_error("Invalid TOML in " + name + ": " + error.what());
  } catch (const std::bad_alloc&) {
    throw std::runtime_error("Out of memory loading " + name);
  }
}

/**
 * @brief Replace a file's content without exposing a partial write.
 *
 * The content goes to a temporary file in the same directory, which is then
 * renamed over the target, so a failed save leaves the old file intact.
 * Symbolic links are followed and the existing permissions are kept.
 */
void WriteFileAtomically(const std::string& path, const std::string& content) {
  std::error_code ec;
  fs::path target = PathFromUtf8(path);
  if (fs::is_symlink(target, ec)) {
    const fs::path resolved = fs::canonical(target, ec);
    if (!ec) target = resolved;
  }
  std::random_device random;
  fs::path temporary;
  std::ofstream output;
  for (int attempt = 0; attempt < 16 && !output.is_open(); ++attempt) {
    temporary = target;
    temporary += ".tmp" + std::to_string(random());
    if (fs::exists(temporary, ec)) continue;
    output.open(temporary, std::ios::binary | std::ios::trunc);
  }
  if (!output.is_open())
    throw std::runtime_error("Cannot open file for writing: " + path);
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  output.close();
  if (!output) {
    fs::remove(temporary, ec);
    throw std::runtime_error("Cannot write file: " + path);
  }
  const auto status = fs::status(target, ec);
  if (!ec && fs::is_regular_file(status))
    fs::permissions(temporary, status.permissions(), ec);
  fs::rename(temporary, target, ec);
  if (ec) {
    const std::string message = ec.message();
    fs::remove(temporary, ec);
    throw std::runtime_error("Cannot write file: " + path + ": " + message);
  }
}

}  // namespace

Dataset Dataset::Parse(const std::string& text, const std::string& format) {
  std::string extension = format;
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) {
                   return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
                 });
  if (!IsSupportedFormat(extension))
    throw std::runtime_error("Unsupported format: " + format);
  return ParseContent(text, extension, "text");
}

void Dataset::Load(const std::string& path) {
  const std::string format = Format(path);
  std::ifstream input(PathFromUtf8(path), std::ios::binary);
  if (!input) throw std::runtime_error("Cannot open file for reading: " + path);
  std::string content;
  char buffer[8192];
  while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0) {
    content.append(buffer, static_cast<std::size_t>(input.gcount()));
  }
  if (input.bad() || !input.eof())
    throw std::runtime_error("Cannot read file: " + path);
  *this = ParseContent(content, format, path);
}

std::list<std::pair<std::string, Dataset>> Dataset::LoadDirectory(
    const std::string& path, bool recursive) {
  std::set<fs::path> ancestors;
  std::vector<fs::path> files;
  CollectFiles(PathFromUtf8(path), recursive, true, ancestors, files);
  std::sort(files.begin(), files.end());
  std::list<std::pair<std::string, Dataset>> result;
  for (const auto& file : files) {
    const std::string name = PathToUtf8(file);
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
  if (ExceedsDepth(*this, kMaxDepth))
    throw std::runtime_error("Dataset nesting is too deep to save: " + path);
  std::string content;
  try {
    if (format == "csv") {
      content = SaveCsv(*this);
    } else if (format == "json") {
      content = ToJson(*this).dump(2) + "\n";
    } else if (format == "yaml" || format == "yml") {
      content = SaveYaml(*this);
    } else {
      if (!cppdatasetutil::holds_alternative<Map>(value_))
        throw std::runtime_error("TOML requires a root table");
      content = toml::format(ToToml(*this));
    }
  } catch (const Json::exception& error) {
    throw std::runtime_error("Cannot serialize " + path + ": " + error.what());
  } catch (const toml::exception& error) {
    throw std::runtime_error("Cannot serialize " + path + ": " + error.what());
  }
  WriteFileAtomically(path, content);
}

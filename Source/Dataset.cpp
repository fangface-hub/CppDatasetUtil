/**
 * @file Dataset.cpp
 * @brief Implements Dataset value construction, access, and comments.
 *
 * Serialization and filesystem operations are implemented in Formats.cpp.
 */

#include <utility>

#include "cppdatasetutil.h"

#if defined(_MSC_VER)
extern "C" CPPDATASETUTIL_API const int CPPDATASETUTIL_ABI_SYMBOL = 1;
#endif

#if CPPDATASETUTIL_CPLUSPLUS_ < 201703L
constexpr std::size_t Dataset::kMaxNestingDepth;
#endif

Dataset::Dataset(std::nullptr_t) : value_(nullptr) {}
Dataset::Dataset(bool value) : value_(value) {}
Dataset::Dataset(int value) : value_(static_cast<std::int64_t>(value)) {}
Dataset::Dataset(std::int64_t value) : value_(value) {}
Dataset::Dataset(double value) : value_(value) {}
Dataset::Dataset(const char* value)
    : value_(value ? Value(std::string(value))
                   : throw std::invalid_argument("null string")) {}
Dataset::Dataset(std::string value) : value_(std::move(value)) {}
Dataset::Dataset(Map value) : value_(std::move(value)) {}
Dataset::Dataset(Array value) : value_(std::move(value)) {}

Dataset& Dataset::operator[](const std::string& key) {
  if (cppdatasetutil::holds_alternative<std::nullptr_t>(value_)) {
    value_ = Map{};
  }
  auto* map = cppdatasetutil::get_if<Map>(&value_);
  if (!map) {
    throw std::logic_error("Dataset is not a map");
  }
  return (*map)[key];
}

const Dataset& Dataset::operator[](const std::string& key) const {
  const auto* map = cppdatasetutil::get_if<Map>(&value_);
  if (!map) {
    throw std::logic_error("Dataset is not a map");
  }
  return map->at(key);
}

Dataset& Dataset::operator[](std::size_t index) {
  auto* array = cppdatasetutil::get_if<Array>(&value_);
  if (!array) {
    throw std::logic_error("Dataset is not an array");
  }
  return array->at(index);
}

const Dataset& Dataset::operator[](std::size_t index) const {
  const auto* array = cppdatasetutil::get_if<Array>(&value_);
  if (!array) {
    throw std::logic_error("Dataset is not an array");
  }
  return array->at(index);
}

std::size_t Dataset::size() const noexcept {
  if (const auto* map = cppdatasetutil::get_if<Map>(&value_))
    return map->size();
  if (const auto* array = cppdatasetutil::get_if<Array>(&value_))
    return array->size();
  return 0;
}

bool Dataset::contains(const std::string& key) const noexcept {
  const auto* map = cppdatasetutil::get_if<Map>(&value_);
  return map && map->find(key) != map->end();
}

void Dataset::push_back(Dataset value) {
  if (cppdatasetutil::holds_alternative<std::nullptr_t>(value_)) {
    value_ = Array{};
  }
  auto* array = cppdatasetutil::get_if<Array>(&value_);
  if (!array) {
    throw std::logic_error("Dataset is not an array");
  }
  array->push_back(std::move(value));
}

bool Dataset::erase(const std::string& key) {
  auto* map = cppdatasetutil::get_if<Map>(&value_);
  if (!map) {
    throw std::logic_error("Dataset is not a map");
  }
  return map->erase(key) > 0;
}

void Dataset::erase(std::size_t index) {
  auto* array = cppdatasetutil::get_if<Array>(&value_);
  if (!array) {
    throw std::logic_error("Dataset is not an array");
  }
  if (index >= array->size()) {
    throw std::out_of_range("Dataset array index out of range");
  }
  array->erase(array->begin() + static_cast<std::ptrdiff_t>(index));
}

bool Dataset::operator==(const Dataset& other) const {
  return value_ == other.value_ && comments_ == other.comments_;
}

bool Dataset::operator!=(const Dataset& other) const {
  return !(*this == other);
}

const Dataset::Value& Dataset::value() const noexcept { return value_; }

Dataset::Value& Dataset::value() noexcept { return value_; }

std::int64_t Dataset::AsInt64() const {
  if (const auto* item = cppdatasetutil::get_if<std::int64_t>(&value_))
    return *item;
  if (cppdatasetutil::holds_alternative<std::uint64_t>(value_))
    throw std::out_of_range("Integer outside int64 range");
  throw std::logic_error("Dataset is not an integer");
}

std::uint64_t Dataset::AsUint64() const {
  if (const auto* item = cppdatasetutil::get_if<std::uint64_t>(&value_))
    return *item;
  if (const auto* item = cppdatasetutil::get_if<std::int64_t>(&value_)) {
    if (*item < 0) throw std::out_of_range("Integer is negative");
    return static_cast<std::uint64_t>(*item);
  }
  throw std::logic_error("Dataset is not an integer");
}

const std::vector<std::string>& Dataset::comments() const noexcept {
  return comments_;
}

void Dataset::set_comments(std::vector<std::string> comments) {
  comments_ = std::move(comments);
}

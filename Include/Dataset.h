/**
 * @file Dataset.h
 * @brief Recursive data model and file format serialization.
 */

#ifndef CPPDATASETUTIL_INCLUDE_DATASET_H_
#define CPPDATASETUTIL_INCLUDE_DATASET_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if defined(_WIN32)
#if defined(CPPDATASETUTIL_BUILD)
#define CPPDATASETUTIL_API __declspec(dllexport)
#else
#define CPPDATASETUTIL_API __declspec(dllimport)
#endif
#else
#define CPPDATASETUTIL_API
#endif

/**
 * @brief A recursively nested data value with optional comments.
 *
 * Values can contain maps or arrays of other Dataset objects. Each value may
 * also carry comments independently of its type. A default-constructed value
 * is null. Mutable key access converts a null value to a map and inserts a
 * null entry when the key is absent; array access never inserts elements.
 * Maps are ordered by key and arrays preserve their element order. Values and
 * their comments are copied or moved together when a Dataset is assigned.
 *
 * @code
 * Dataset data;
 * data["server"]["host"] = "localhost";
 * data["server"]["port"] = 8080;
 * data["server"]["host"].set_comments({"Server address"});
 * data.Save("config.yaml");
 *
 * Dataset loaded;
 * loaded.Load("config.yaml");
 * const auto& host = std::get<std::string>(loaded["server"]["host"].value());
 * @endcode
 *
 * Loading replaces the complete value and discards comments. Saving comments
 * is supported only for YAML and TOML, subject to format restrictions.
 * See README.md for the full file format contract.
 */
class CPPDATASETUTIL_API Dataset {
 public:
  /// String-keyed collection of values, ordered by key.
  using Map = std::map<std::string, Dataset>;
  /// Indexable, ordered collection of values.
  using Array = std::vector<Dataset>;
  /**
   * @brief Variant holding the supported scalar and container types.
   *
   * Integers use std::int64_t; floating-point values use double. The model
   * accepts non-finite doubles, but the file formats reject them on save.
   */
  using Value = std::variant<std::nullptr_t, bool, std::int64_t, double,
                             std::string, Map, Array>;

  /**
   * @brief Maximum number of nested maps and arrays loaded or saved.
   *
   * Loading keeps any map or array nested deeper than this as a string holding
   * its compact JSON text, which Parse() converts back to a Dataset.
   */
  static constexpr std::size_t kMaxNestingDepth = 256;

  /// Construct a null value with no comments.
  Dataset() = default;
  /// Construct a null value.
  Dataset(std::nullptr_t);
  /// Construct a boolean value from @p value.
  Dataset(bool value);
  /// Construct a signed 64-bit integer from @p value.
  Dataset(int value);
  /// Construct a signed 64-bit integer from @p value.
  Dataset(std::int64_t value);
  /**
   * @brief Construct a signed 64-bit integer from any other integer type.
   * @param value Integer to store, such as `long long`, `std::size_t`, or
   *     `unsigned`.
   * @throws std::out_of_range if an unsigned @p value exceeds the int64 range.
   */
  template <typename T,
            std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>,
                             int> = 0>
  Dataset(T value) {
    if constexpr (std::is_unsigned_v<T>) {
      if (value >
          static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        throw std::out_of_range("Integer outside int64 range");
    }
    value_ = static_cast<std::int64_t>(value);
  }
  /// Pointers other than strings are rejected rather than converted to bool.
  Dataset(const void*) = delete;
  /// Construct a floating-point value from @p value; it is not range-checked.
  Dataset(double value);
  /**
   * @brief Construct a string value from a null-terminated string.
   * @param value Non-null pointer to the string.
   * @throws std::invalid_argument if @p value is null.
   */
  Dataset(const char* value);
  /// Construct a string value from @p value.
  Dataset(std::string value);
  /// Construct a map value from @p value, including its entries' comments.
  Dataset(Map value);
  /// Construct an array value from @p value, including its elements' comments.
  Dataset(Array value);

  /**
   * @brief Access a map entry, inserting a null value if it is absent.
   * @param key Entry name.
   * @return Mutable reference to the entry.
   *
   * If this value is null, it is first converted to an empty map. Comments
   * associated with this value are retained.
   * Replacing the enclosing Dataset invalidates references to its entries.
   * @throws std::logic_error if this value is neither null nor a map.
   */
  Dataset& operator[](const std::string& key);
  /**
   * @brief Access an existing map entry.
   * @param key Entry name.
   * @return Const reference to the entry.
   *
   * Unlike mutable key access, this overload never converts null or inserts.
   * @throws std::logic_error if this value is not a map.
   * @throws std::out_of_range if the key does not exist.
   */
  const Dataset& operator[](const std::string& key) const;
  /**
   * @brief Access an array element.
   * @param index Zero-based element index.
   * @return Mutable reference to the element.
   * @throws std::logic_error if this value is not an array.
   * @throws std::out_of_range if the index is outside the array.
   */
  Dataset& operator[](std::size_t index);
  /**
   * @brief Access an array element without modifying it.
   * @param index Zero-based element index.
   * @return Const reference to the element.
   * @throws std::logic_error if this value is not an array.
   * @throws std::out_of_range if @p index is outside the array.
   */
  const Dataset& operator[](std::size_t index) const;

  /**
   * @brief Count the entries of a map or array.
   * @return Number of entries, or 0 if this value is neither.
   */
  std::size_t size() const noexcept;
  /**
   * @brief Check whether a map contains a key.
   * @param key Entry name.
   * @return True if this value is a map with @p key; false otherwise.
   */
  bool contains(const std::string& key) const noexcept;
  /**
   * @brief Append an element to an array.
   * @param value Element to add, including its comments.
   *
   * If this value is null, it is first converted to an empty array. Comments
   * associated with this value are retained. Growing the array invalidates
   * references to its elements.
   * @throws std::logic_error if this value is neither null nor an array.
   */
  void push_back(Dataset value);
  /**
   * @brief Remove a map entry.
   * @param key Entry name.
   * @return True if the entry existed and was removed.
   * @throws std::logic_error if this value is not a map.
   */
  bool erase(const std::string& key);
  /**
   * @brief Remove an array element, shifting later elements down.
   * @param index Zero-based element index.
   * @throws std::logic_error if this value is not an array.
   * @throws std::out_of_range if @p index is outside the array.
   */
  void erase(std::size_t index);
  /**
   * @brief Compare values and comments, recursively.
   *
   * Doubles follow IEEE comparison, so a NaN is never equal to itself.
   */
  bool operator==(const Dataset& other) const;
  /// Negation of operator==.
  bool operator!=(const Dataset& other) const;

  /**
   * @brief Inspect the underlying variant.
   * @return Const reference to the value; use std::get or std::get_if to
   *     inspect the selected type.
   *
   * std::get throws std::bad_variant_access for a mismatched type; std::get_if
   * returns null instead. Replacing the Dataset changes the referenced value.
   */
  const Value& value() const noexcept;
  /**
   * @brief Modify the underlying variant.
   * @return Mutable reference to the value; assigning a new alternative
   *     replaces the value but keeps the comments.
   */
  Value& value() noexcept;

  /**
   * @brief Inspect comments associated with this value.
   * @return Const reference to the comment strings, in their stored order.
   */
  const std::vector<std::string>& comments() const noexcept;
  /**
   * @brief Replace comments associated with this value.
   * @param comments Comment text without a leading comment marker. Embedded
   *     line breaks are written as separate comment lines.
   *
   * YAML and TOML saving requires valid UTF-8 and rejects unsupported control
   * characters and Unicode line separators. Some TOML comments cannot be
   * represented and are omitted. CSV and JSON do not save comments.
   * Replacing a Dataset by assignment or a successful Load() also replaces its
   * comments.
   */
  void set_comments(std::vector<std::string> comments);

  /**
   * @brief Load a CSV, JSON, YAML, or TOML file, selected by its extension.
   * @param path File path with a case-insensitive supported extension
   *     (`.csv`, `.json`, `.yaml`, `.yml`, or `.toml`).
   *
   * CSV is loaded as an array of string-valued maps using the first row as
   * column names; duplicate headers and inconsistent row lengths are errors.
   * YAML resolves supported core scalar types and rejects multiple documents,
   * duplicate anchors, recursive aliases, aliases that expand to more than
   * 1,000,000 values in total, and unsupported tags. TOML
   * date/time values and non-finite numbers are unsupported.
   * Maps and arrays nested deeper than kMaxNestingDepth are kept as strings
   * holding their compact JSON text (see Parse()); TOML nested more than
   * twice that deep is rejected. YAML anchors cannot be defined inside such
   * deeply nested values.
   * Comments are not loaded; a successful load replaces the value and its
   * existing comments. On failure this Dataset is left unchanged.
   *
   * @throws std::runtime_error on I/O errors, invalid input, an unsupported
   *     extension, or values that the data model cannot represent.
   * @see README.md for further format restrictions.
   */
  void Load(const std::string& path);

  /**
   * @brief Parse CSV, JSON, YAML, or TOML text.
   * @param text Complete document text.
   * @param format Case-insensitive format name: `csv`, `json`, `yaml`, `yml`,
   *     or `toml`. Defaults to `json`.
   * @return The parsed value, without comments.
   *
   * Follows the same rules as Load(), including the nesting limit. Use this
   * to expand a string holding the JSON text of a subtree that Load() kept
   * because it was nested deeper than kMaxNestingDepth.
   *
   * @throws std::runtime_error on invalid input, an unsupported format, or
   *     values that the data model cannot represent.
   */
  static Dataset Parse(const std::string& text,
                       const std::string& format = "json");

  /**
   * @brief Load supported files from a directory.
   * @param path Directory to scan.
   * @param recursive Also scan subdirectories when true; defaults to false.
   * @return Pairs of file paths and loaded values, sorted by path.
   *
   * File and directory symlinks are followed. Cycles, broken links, unreadable
   * entries and files that fail to load are skipped with a warning on standard
   * error. Unsupported file extensions are ignored. Directory symlinks are
   * only traversed when @p recursive is true; paths retain the supplied
   * directory and link names. A target reachable by multiple paths is loaded
   * once per path. A file symlink's own extension selects the format.
   * Comments are not loaded, as with Load().
   *
   * @throws std::runtime_error if the specified directory cannot be opened.
   */
  static std::list<std::pair<std::string, Dataset>> LoadDirectory(
      const std::string& path, bool recursive = false);

  /**
   * @brief Save this value to a file, selecting the format by its extension.
   * @param path Output path with a case-insensitive supported extension
   *     (`.csv`, `.json`, `.yaml`, `.yml`, or `.toml`).
   *
   * CSV requires an array of nonempty string-valued maps with identical keys;
   * an empty array produces an empty file. TOML requires a root map and does
   * not support null. Non-finite doubles cannot be saved. YAML and TOML save
   * comments where supported; CSV and JSON do not. Values nested deeper than
   * kMaxNestingDepth cannot be saved. The content is written to a
   * temporary file in the same directory and renamed over the target, so an
   * I/O failure leaves any existing file unchanged. Symbolic links are
   * followed and existing permissions are kept; the directory must be
   * writable.
   *
   * @throws std::runtime_error on I/O errors, an unsupported extension, or
   *     values or comments that the format cannot represent.
   * @see README.md for further format restrictions.
   */
  void Save(const std::string& path) const;

 private:
  Value value_ = nullptr;
  std::vector<std::string> comments_;
};

#endif  // CPPDATASETUTIL_INCLUDE_DATASET_H_

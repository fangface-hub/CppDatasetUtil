/**
 * @file DatasetTest.cpp
 * @brief GoogleTest coverage for Dataset values, formats, and directory
 * loading.
 *
 * Data-driven cases are read from Tests/data relative to the project root.
 * FormatTest creates isolated temporary files for I/O and failure scenarios.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

#include "Dataset.h"

namespace {

/**
 * @brief Isolate filesystem tests in a temporary directory.
 *
 * Paths returned by Path() are removed in reverse order during teardown so
 * registered child files and directories are removed before their parents.
 */
class FormatTest : public ::testing::Test {
 protected:
  void SetUp() override {
    char name[] = "/tmp/cppdatasetutil-test-XXXXXX";
    char* directory = mkdtemp(name);
    ASSERT_NE(directory, nullptr);
    directory_ = directory;
  }

  void TearDown() override {
    for (auto it = paths_.rbegin(); it != paths_.rend(); ++it)
      std::remove(it->c_str());
    if (!directory_.empty()) std::remove(directory_.c_str());
  }

  /// Register a path relative to the temporary directory for later cleanup.
  std::string Path(const std::string& name) {
    paths_.push_back(directory_ + "/" + name);
    return paths_.back();
  }

  void Write(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out);
    out << content;
    ASSERT_TRUE(out);
  }

  std::string Read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream result;
    result << in.rdbuf();
    return result.str();
  }

 private:
  std::string directory_;
  std::vector<std::string> paths_;
};

}  // namespace

/**
 * @brief Verify nested map construction using data-driven cases.
 * @test Load each case's input and expected values, including false and zero,
 *     then compare them with values read through const map access.
 */
TEST(DatasetTest, SupportsNestedValues) {
  testing::internal::CaptureStderr();
  std::list<std::pair<std::string, Dataset>> files;
  EXPECT_NO_THROW(files = Dataset::LoadDirectory("Tests/data/nested-values"));
  const auto warnings = testing::internal::GetCapturedStderr();
  ASSERT_TRUE(warnings.empty()) << warnings;
  ASSERT_FALSE(files.empty());
  for (const auto& [path, dataset] : files) {
    SCOPED_TRACE(path);
    const auto& cases = std::get<Dataset::Array>(dataset["cases"].value());
    ASSERT_FALSE(cases.empty());
    for (std::size_t i = 0; i < cases.size(); ++i) {
      SCOPED_TRACE(i);
      const Dataset& input = cases[i]["input"];
      const Dataset& expected = cases[i]["expected"];
      Dataset data;
      data["server"]["host"] = std::get<std::string>(input["host"].value());
      data["server"]["port"] = std::get<std::int64_t>(input["port"].value());
      data["enabled"] = std::get<bool>(input["enabled"].value());
      const Dataset& result = data;
      EXPECT_EQ(std::get<std::string>(result["server"]["host"].value()),
                std::get<std::string>(expected["host"].value()));
      EXPECT_EQ(std::get<std::int64_t>(result["server"]["port"].value()),
                std::get<std::int64_t>(expected["port"].value()));
      EXPECT_EQ(std::get<bool>(result["enabled"].value()),
                std::get<bool>(expected["enabled"].value()));
    }
  }
}

/**
 * @brief Verify const array indexing using data-driven cases.
 * @test Compare the first and last elements against expected values for
 *     multi-element and single-element arrays.
 */
TEST(DatasetTest, AccessesArrayValuesFromCases) {
  testing::internal::CaptureStderr();
  std::list<std::pair<std::string, Dataset>> files;
  EXPECT_NO_THROW(files = Dataset::LoadDirectory("Tests/data/array-values"));
  const auto warnings = testing::internal::GetCapturedStderr();
  ASSERT_TRUE(warnings.empty()) << warnings;
  ASSERT_FALSE(files.empty());
  for (const auto& [path, dataset] : files) {
    SCOPED_TRACE(path);
    const auto& cases = std::get<Dataset::Array>(dataset["cases"].value());
    ASSERT_FALSE(cases.empty());
    for (std::size_t i = 0; i < cases.size(); ++i) {
      SCOPED_TRACE(i);
      const Dataset& input = cases[i]["input"];
      const Dataset& expected = cases[i]["expected"];
      const auto& items = std::get<Dataset::Array>(input["items"].value());
      ASSERT_FALSE(items.empty());
      const Dataset result = items;
      EXPECT_EQ(std::get<std::string>(result[std::size_t{0}].value()),
                std::get<std::string>(expected["first"].value()));
      EXPECT_EQ(std::get<std::string>(result[items.size() - 1].value()),
                std::get<std::string>(expected["last"].value()));
    }
  }
}

/**
 * @brief Verify array elements, per-value comments, and index bounds.
 * @test A string and null retain their types, comments attach to the chosen
 *     element, and an out-of-range index throws.
 */
TEST(DatasetTest, SupportsArraysAndComments) {
  Dataset data = Dataset::Array{Dataset("first"), Dataset(nullptr)};
  data[std::size_t{0}].set_comments({"comment"});
  EXPECT_EQ(std::get<std::string>(data[std::size_t{0}].value()), "first");
  EXPECT_EQ(data[std::size_t{0}].comments().at(0), "comment");
  EXPECT_TRUE(
      std::holds_alternative<std::nullptr_t>(data[std::size_t{1}].value()));
  EXPECT_THROW(data[std::size_t{2}], std::out_of_range);
}

/**
 * @brief Verify errors for invalid value access and construction.
 * @test Non-container indexing, a missing const map key, and a null C string
 *     produce their documented exceptions.
 */
TEST(DatasetTest, RejectsInvalidAccess) {
  Dataset scalar = 42;
  EXPECT_THROW(scalar["key"], std::logic_error);
  EXPECT_THROW(scalar[std::size_t{0}], std::logic_error);
  Dataset map = Dataset::Map{};
  const Dataset& result = map;
  EXPECT_THROW(result["missing"], std::out_of_range);
  EXPECT_THROW(Dataset(static_cast<const char*>(nullptr)),
               std::invalid_argument);
}

/**
 * @brief Verify in-place container editing and comparison.
 * @test Append, erase, query, replace values, and compare nested datasets;
 *     reject operations on the wrong value type.
 */
TEST(DatasetTest, EditsContainersInPlace) {
  Dataset list;
  list.push_back(1);
  list.push_back("two");
  list.push_back(Dataset::Map{{"k", 3}});
  EXPECT_EQ(list.size(), 3u);
  list.erase(std::size_t{0});
  EXPECT_EQ(list.size(), 2u);
  EXPECT_EQ(std::get<std::string>(list[0].value()), "two");
  EXPECT_THROW(list.erase(std::size_t{2}), std::out_of_range);
  EXPECT_THROW(list.erase("k"), std::logic_error);

  Dataset map;
  map["a"] = 1;
  map["b"]["c"] = 2;
  EXPECT_TRUE(map.contains("a"));
  EXPECT_FALSE(map.contains("missing"));
  EXPECT_TRUE(map.erase("a"));
  EXPECT_FALSE(map.erase("a"));
  EXPECT_EQ(map.size(), 1u);
  EXPECT_THROW(map.push_back(1), std::logic_error);
  EXPECT_FALSE(Dataset(5).contains("a"));
  EXPECT_EQ(Dataset("text").size(), 0u);

  Dataset value = 1;
  value.set_comments({"note"});
  value.value() = std::string("changed");
  EXPECT_EQ(value.comments().at(0), "note");
  EXPECT_EQ(std::get<std::string>(value.value()), "changed");

  Dataset copy = map;
  EXPECT_EQ(copy, map);
  copy["b"]["c"] = 3;
  EXPECT_NE(copy, map);
  copy = map;
  copy.set_comments({"x"});
  EXPECT_NE(copy, map);
}

/**
 * @brief Verify construction from every integer type.
 * @test Store `long long`, unsigned, and narrow integers as int64 and reject
 *     unsigned values beyond the int64 range.
 */
TEST(DatasetTest, ConstructsFromAnyIntegerType) {
  const auto get = [](const Dataset& data) {
    return std::get<std::int64_t>(data.value());
  };
  EXPECT_EQ(get(Dataset(-5LL)), -5);
  EXPECT_EQ(get(Dataset(std::size_t{7})), 7);
  EXPECT_EQ(get(Dataset(8u)), 8);
  EXPECT_EQ(get(Dataset(static_cast<short>(-3))), -3);
  EXPECT_EQ(get(Dataset(std::numeric_limits<std::uint64_t>::max() / 2)),
            std::numeric_limits<std::int64_t>::max());
  EXPECT_THROW(Dataset(std::numeric_limits<std::uint64_t>::max()),
               std::out_of_range);
  EXPECT_TRUE(std::holds_alternative<bool>(Dataset(true).value()));
}

/**
 * @brief Verify JSON and YAML round trips for nested values.
 * @test Exercise null, booleans, integer limits, floating-point extremes,
 *     negative zero, escaped strings, nested containers, and extension casing.
 */
TEST_F(FormatTest, JsonAndYamlRoundTripNestedValues) {
  Dataset data;
  data["null"] = nullptr;
  data["bool"] = true;
  data["int"] = std::numeric_limits<std::int64_t>::min();
  data["float"] = 1.0;
  data["tiny"] = std::numeric_limits<double>::denorm_min();
  data["largest"] = std::numeric_limits<double>::max();
  data["negative_zero"] = -0.0;
  data["string"] = "true\n\"hello\"";
  data["array"] = Dataset::Array{Dataset(3), Dataset::Map{{"key", "value"}}};
  for (const auto& extension : {"json", "yaml", "YML"}) {
    SCOPED_TRACE(extension);
    const auto path = Path(std::string("data.") + extension);
    data.Save(path);
    Dataset loaded;
    loaded.Load(path);
    EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(loaded["null"].value()));
    EXPECT_TRUE(std::get<bool>(loaded["bool"].value()));
    EXPECT_EQ(std::get<std::int64_t>(loaded["int"].value()),
              std::numeric_limits<std::int64_t>::min());
    EXPECT_EQ(std::get<double>(loaded["float"].value()), 1.0);
    EXPECT_EQ(std::get<double>(loaded["tiny"].value()),
              std::numeric_limits<double>::denorm_min());
    EXPECT_EQ(std::get<double>(loaded["largest"].value()),
              std::numeric_limits<double>::max());
    EXPECT_TRUE(
        std::signbit(std::get<double>(loaded["negative_zero"].value())));
    EXPECT_EQ(std::get<std::string>(loaded["string"].value()),
              "true\n\"hello\"");
    EXPECT_EQ(
        std::get<std::string>(loaded["array"][std::size_t{1}]["key"].value()),
        "value");
  }
}

/**
 * @brief Verify YAML scalar resolution, explicit tags, and aliases.
 * @test Check null, boolean, numeric, and quoted string values through a
 *     sequence alias, plus an alias referring to an anchored map key.
 */
TEST_F(FormatTest, LoadsYamlScalarsTagsAndAliases) {
  const auto path = Path("input.yaml");
  Write(path,
        "first: &x [null, true, 0x10, -0o10, 1.5, 'false', !!str 123]\n"
        "second: *x\n"
        "&key label: *key\n");
  Dataset data;
  data.Load(path);
  const auto& array = std::get<Dataset::Array>(data["second"].value());
  ASSERT_EQ(array.size(), 7u);
  EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(array[0].value()));
  EXPECT_TRUE(std::get<bool>(array[1].value()));
  EXPECT_EQ(std::get<std::int64_t>(array[2].value()), 16);
  EXPECT_EQ(std::get<std::int64_t>(array[3].value()), -8);
  EXPECT_EQ(std::get<double>(array[4].value()), 1.5);
  EXPECT_EQ(std::get<std::string>(array[5].value()), "false");
  EXPECT_EQ(std::get<std::string>(array[6].value()), "123");
  EXPECT_EQ(std::get<std::string>(data["label"].value()), "label");
}

/**
 * @brief Verify TOML round trips and unsupported-value errors.
 * @test Check nested values, reject null and non-table roots without
 *     overwriting the file, and reject dates and non-finite numbers on load.
 */
TEST_F(FormatTest, TomlRoundTripAndUnsupportedValues) {
  const auto path = Path("data.toml");
  Dataset data;
  data["server"]["host"] = "localhost";
  data["server"]["port"] = 8080;
  data["items"] = Dataset::Array{Dataset(true), Dataset(2.5), Dataset("x")};
  data.Save(path);
  Dataset loaded;
  loaded.Load(path);
  EXPECT_EQ(std::get<std::string>(loaded["server"]["host"].value()),
            "localhost");
  EXPECT_EQ(std::get<std::int64_t>(loaded["server"]["port"].value()), 8080);
  EXPECT_EQ(std::get<double>(loaded["items"][std::size_t{1}].value()), 2.5);
  const auto original = Read(path);
  data["unsupported"] = nullptr;
  EXPECT_THROW(data.Save(path), std::runtime_error);
  EXPECT_EQ(Read(path), original);
  Dataset non_table = Dataset::Array{};
  EXPECT_THROW(non_table.Save(path), std::runtime_error);
  EXPECT_EQ(Read(path), original);
  Write(path, "date = 1979-05-27\n");
  EXPECT_THROW(loaded.Load(path), std::runtime_error);
  EXPECT_EQ(std::get<std::int64_t>(loaded["server"]["port"].value()), 8080);
  Write(path, "number = inf\n");
  EXPECT_THROW(loaded.Load(path), std::runtime_error);
}

/**
 * @brief Verify CSV headers and escaped, multiline fields.
 * @test Load quoted commas, quotes, and line breaks, then save and reload the
 *     resulting string-valued records.
 */
TEST_F(FormatTest, CsvHeaderAndQuotedFieldsRoundTrip) {
  const auto path = Path("input.csv");
  Write(path, "name,note\r\nAlice,\"one, \"\"two\"\"\r\nthree\"\r\nBob,\r\n");
  Dataset data;
  data.Load(path);
  EXPECT_EQ(std::get<std::string>(data[std::size_t{0}]["note"].value()),
            "one, \"two\"\r\nthree");
  EXPECT_EQ(std::get<std::string>(data[std::size_t{1}]["note"].value()), "");
  const auto output = Path("output.csv");
  data.Save(output);
  Dataset loaded;
  loaded.Load(output);
  EXPECT_EQ(std::get<std::string>(loaded[std::size_t{0}]["name"].value()),
            "Alice");
  EXPECT_EQ(std::get<std::string>(loaded[std::size_t{0}]["note"].value()),
            "one, \"two\"\r\nthree");
}

/**
 * @brief Verify malformed files do not modify an existing Dataset.
 * @test Exercise invalid CSV, JSON, YAML, and TOML, as well as missing files
 *     and unsupported extensions; retain the original value and comments.
 */
TEST_F(FormatTest, RejectsInvalidInputWithoutChangingDataset) {
  Dataset data = 42;
  data.set_comments({"keep"});
  const std::vector<std::pair<std::string, std::string>> inputs = {
      {"bad.csv", "a,a\n1,2\n"},
      {"short.csv", "a,b\n1\n"},
      {"quote.csv", "a\n\"unterminated"},
      {"bad.json", "{\"x\":"},
      {"large.json", "9223372036854775808"},
      {"huge.json", "18446744073709551616"},
      {"small.json", "-9223372036854775809"},
      {"bad.yaml", "a: [1\n"},
      {"duplicate.yaml", "a: 1\na: 2\n"},
      {"overflow.yaml", "a: 9223372036854775808\n"},
      {"nonfinite.yaml", "a: .inf\n"},
      {"recursive.yaml", "a: &x [*x]\n"},
      {"duplicate-anchor.yaml", "a: &x 1\nb: &x [*x]\n"},
      {"documents.yaml", "---\na\n---\nb\n"},
      {"bad.toml", "a = [\n"},
  };
  for (const auto& input : inputs) {
    SCOPED_TRACE(input.first);
    const auto path = Path(input.first);
    Write(path, input.second);
    EXPECT_THROW(data.Load(path), std::runtime_error);
    EXPECT_EQ(std::get<std::int64_t>(data.value()), 42);
    EXPECT_EQ(data.comments().at(0), "keep");
  }
  EXPECT_THROW(data.Load(Path("missing.json")), std::runtime_error);
  EXPECT_THROW(data.Load(Path("unknown.txt")), std::runtime_error);
}

/**
 * @brief Verify very long YAML scalars are resolved without recursion.
 * @test A multi-megabyte plain scalar loads as a string and long numerals are
 *     still resolved.
 */
TEST_F(FormatTest, ResolvesLongYamlScalars) {
  const auto path = Path("long.yaml");
  Write(path, "a: " + std::string(4000000, '1') + "x\nb: 1.5e3\nc: -0x1F\n");
  Dataset data;
  data.Load(path);
  EXPECT_EQ(std::get<std::string>(data["a"].value()).size(), 4000001u);
  EXPECT_DOUBLE_EQ(std::get<double>(data["b"].value()), 1500.0);
  EXPECT_EQ(std::get<std::int64_t>(data["c"].value()), -31);
}

/**
 * @brief Verify very long map keys survive a YAML round trip.
 * @test Save keys beyond libyaml's simple-key limit, with and without comments,
 *     and load them back unchanged.
 */
TEST_F(FormatTest, RoundTripsLongYamlKeys) {
  const std::string long_key(5000, 'k');
  for (bool comment : {false, true}) {
    Dataset data;
    data[long_key]["inner"] = 1;
    data["short"] = "v";
    if (comment) data["short"].set_comments({"note"});
    const auto path = Path(comment ? "longkey_c.yaml" : "longkey.yaml");
    data.Save(path);
    Dataset loaded;
    loaded.Load(path);
    EXPECT_EQ(std::get<std::int64_t>(loaded[long_key]["inner"].value()), 1);
    EXPECT_EQ(std::get<std::string>(loaded["short"].value()), "v");
  }
}

/**
 * @brief Verify CSV loading tolerates a BOM and blank lines.
 * @test Load a BOM-prefixed file with blank lines and a CRLF trailing blank
 *     line, and round-trip an empty single-column value.
 */
TEST_F(FormatTest, LoadsCsvWithBomAndBlankLines) {
  const auto path = Path("bom.csv");
  Write(path,
        "\xEF\xBB\xBF"
        "a,b\r\n1,2\r\n\r\n3,4\n\n");
  Dataset data;
  data.Load(path);
  ASSERT_EQ(data.size(), 2u);
  EXPECT_EQ(std::get<std::string>(data[0]["a"].value()), "1");
  EXPECT_EQ(std::get<std::string>(data[1]["b"].value()), "4");

  Dataset single =
      Dataset::Array{Dataset::Map{{"x", ""}}, Dataset::Map{{"x", "y"}}};
  const auto single_path = Path("single.csv");
  single.Save(single_path);
  Dataset loaded;
  loaded.Load(single_path);
  ASSERT_EQ(loaded.size(), 2u);
  EXPECT_EQ(std::get<std::string>(loaded[0]["x"].value()), "");
}

/**
 * @brief Verify Save replaces files without leaving temporary files.
 * @test Overwrite an existing file, keep its permissions, and write through a
 *     symbolic link.
 */
TEST_F(FormatTest, SavesAtomically) {
  namespace fs = std::filesystem;
  const auto path = Path("atomic.json");
  Write(path, "old");
  fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write);
  Dataset data;
  data["k"] = 1;
  data.Save(path);
  Dataset loaded;
  loaded.Load(path);
  EXPECT_EQ(std::get<std::int64_t>(loaded["k"].value()), 1);
  EXPECT_EQ(fs::status(path).permissions(),
            fs::perms::owner_read | fs::perms::owner_write);

  const auto link = Path("atomic_link.json");
  std::error_code ec;
  fs::create_symlink(fs::path(path).filename(), link, ec);
  if (!ec) {
    data["k"] = 2;
    data.Save(link);
    EXPECT_TRUE(fs::is_symlink(link));
    loaded.Load(path);
    EXPECT_EQ(std::get<std::int64_t>(loaded["k"].value()), 2);
  }
  for (const auto& entry : fs::directory_iterator(fs::path(path).parent_path()))
    EXPECT_EQ(entry.path().string().find(".tmp"), std::string::npos);
}

/**
 * @brief Verify YAML alias expansion is bounded.
 * @test Reject an alias bomb with std::runtime_error while accepting ordinary
 *     alias reuse.
 */
TEST_F(FormatTest, RejectsYamlAliasBomb) {
  std::string bomb = "a: &a [x, x, x, x, x, x, x, x, x, x]\n";
  char previous = 'a';
  for (char name = 'b'; name <= 'i'; ++name) {
    bomb += std::string(1, name) + ": &" + name + " [";
    for (int i = 0; i < 10; ++i)
      bomb += std::string(i ? ", *" : "*") + previous;
    bomb += "]\n";
    previous = name;
  }
  const auto path = Path("bomb.yaml");
  Write(path, bomb);
  Dataset data;
  EXPECT_THROW(data.Load(path), std::runtime_error);

  Write(path, "a: &a [1, 2]\nb: *a\nc: *a\n");
  data.Load(path);
  EXPECT_EQ(std::get<Dataset::Array>(data["c"].value()).size(), 2u);
}

/**
 * @brief Verify nesting beyond the limit is kept as JSON text.
 * @test Load JSON, YAML, and TOML nested deeper than the limit without
 *     crashing, expand the retained text with Parse(), and reject unsafe TOML
 *     and unsaveable depth.
 */
TEST_F(FormatTest, KeepsDeepNestingAsText) {
  const auto nested = [](std::size_t levels) {
    return std::string(levels, '[') + "1" + std::string(levels, ']');
  };
  const auto leaf = [](const Dataset& root) {
    const Dataset* node = &root;
    std::size_t levels = 0;
    while (const auto* array = std::get_if<Dataset::Array>(&node->value())) {
      node = &(*array)[0];
      ++levels;
    }
    return std::make_pair(levels, node);
  };
  constexpr std::size_t kLevels = 5000;
  for (const std::string name : {"deep.json", "deep.yaml"}) {
    SCOPED_TRACE(name);
    const auto path = Path(name);
    Write(path, nested(kLevels));
    Dataset data;
    data.Load(path);
    const auto [levels, node] = leaf(data);
    EXPECT_EQ(levels, Dataset::kMaxNestingDepth);
    const auto* text = std::get_if<std::string>(&node->value());
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(*text, nested(kLevels - Dataset::kMaxNestingDepth));
    EXPECT_EQ(leaf(Dataset::Parse(*text)).first, Dataset::kMaxNestingDepth);
  }

  const auto toml = Path("deep.toml");
  Write(toml, "a = " + nested(300) + "\n");
  Dataset data;
  data.Load(toml);
  const auto [levels, node] = leaf(data["a"]);
  EXPECT_EQ(levels, Dataset::kMaxNestingDepth - 1);
  EXPECT_EQ(std::get<std::string>(node->value()), nested(300 - 255));
  Write(toml, "a = " + nested(5000) + "\n");
  EXPECT_THROW(data.Load(toml), std::runtime_error);
  EXPECT_EQ(std::get<std::string>(leaf(data["a"]).second->value()),
            nested(300 - 255));

  Dataset too_deep = 1;
  for (std::size_t i = 0; i <= Dataset::kMaxNestingDepth; ++i)
    too_deep = Dataset::Array{too_deep};
  EXPECT_THROW(too_deep.Save(Path("too-deep.json")), std::runtime_error);
}

/**
 * @brief Verify unsupported output values are rejected.
 * @test Reject non-string or mismatched CSV records and non-finite values;
 *     preserve an existing CSV file when serialization fails.
 */
TEST_F(FormatTest, RejectsUnrepresentableOutput) {
  const auto csv = Path("bad.csv");
  Write(csv, "original");
  Dataset data = Dataset::Array{Dataset::Map{{"a", 1}}};
  EXPECT_THROW(data.Save(csv), std::runtime_error);
  EXPECT_EQ(Read(csv), "original");
  data = Dataset::Array{Dataset::Map{{"a", "x"}}, Dataset::Map{{"b", "y"}}};
  EXPECT_THROW(data.Save(csv), std::runtime_error);
  data = std::numeric_limits<double>::infinity();
  EXPECT_THROW(data.Save(Path("bad.json")), std::runtime_error);
  EXPECT_THROW(data.Save(Path("bad.yaml")), std::runtime_error);
  EXPECT_THROW(data.Save(Path("bad.toml")), std::runtime_error);
  EXPECT_THROW(data.Save(Path("bad.txt")), std::runtime_error);
}

/**
 * @brief Verify empty CSV representations and comment removal on load.
 * @test An empty array saves as an empty file; empty and header-only files
 *     load as empty arrays, and comments are not retained.
 */
TEST_F(FormatTest, EmptyCsvAndComments) {
  const auto path = Path("empty.csv");
  Dataset data = Dataset::Array{};
  data.set_comments({"not saved"});
  data.Save(path);
  EXPECT_EQ(Read(path), "");
  data.Load(path);
  EXPECT_TRUE(std::get<Dataset::Array>(data.value()).empty());
  EXPECT_TRUE(data.comments().empty());
  Write(path, "header1,header2\n");
  data.Load(path);
  EXPECT_TRUE(std::get<Dataset::Array>(data.value()).empty());
}

/**
 * @brief Verify YAML comment placement at root and nested values.
 * @test Check empty, multiline, and UTF-8 comments, escaped keys, and typed
 *     values; reloading preserves values but does not restore comments.
 */
TEST_F(FormatTest, SavesYamlCommentsOnAllValues) {
  Dataset data;
  data.set_comments({"root", ""});
  data["empty map"] = Dataset::Map{};
  data["empty map"].set_comments({"empty"});
  data["items"] = Dataset::Array{Dataset(1), Dataset::Map{{"a\"b", "text"}}};
  data["items"].set_comments({"array"});
  data["items"][std::size_t{0}].set_comments({"first\ninjected: true\r\nlast"});
  data["items"][std::size_t{1}].set_comments({"element"});
  data["items"][std::size_t{1}]["a\"b"].set_comments(
      {u8"caf\u00e9", "# literal"});
  data["null"] = nullptr;
  data["null"].set_comments({"null value"});
  data["real"] = std::numeric_limits<double>::denorm_min();
  data["real"].set_comments({"real value"});
  data["literal"] = "true\n\"text\"";
  data["literal"].set_comments({"quoted string"});
  for (const auto& extension : {"yaml", "yml"}) {
    const auto path = Path(std::string("comments.") + extension);
    data.Save(path);
    const auto text = Read(path);
    EXPECT_EQ(text.find("# root\n# \n"), 0u);
    EXPECT_NE(text.find("# empty\n  \"empty map\": {}"), std::string::npos);
    EXPECT_NE(text.find("# array\n  \"items\": ["), std::string::npos);
    EXPECT_NE(text.find("# first\n    # injected: true\n    # last\n    1"),
              std::string::npos);
    EXPECT_NE(text.find("# element\n    {"), std::string::npos);
    EXPECT_NE(
        text.find(
            u8"# caf\u00e9\n      # # literal\n      \"a\\\"b\": \"text\""),
        std::string::npos);
    Dataset loaded;
    loaded.Load(path);
    EXPECT_EQ(std::get<std::int64_t>(loaded["items"][std::size_t{0}].value()),
              1);
    EXPECT_EQ(
        std::get<std::string>(loaded["items"][std::size_t{1}]["a\"b"].value()),
        "text");
    EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(loaded["null"].value()));
    EXPECT_EQ(std::get<double>(loaded["real"].value()),
              std::numeric_limits<double>::denorm_min());
    EXPECT_EQ(std::get<std::string>(loaded["literal"].value()),
              "true\n\"text\"");
    EXPECT_TRUE(loaded.comments().empty());
  }
  Dataset scalar = "hello";
  scalar.set_comments({"scalar"});
  const auto path = Path("scalar.yaml");
  scalar.Save(path);
  EXPECT_EQ(Read(path), "# scalar\n\"hello\"\n");
  Dataset loaded;
  loaded.Load(path);
  EXPECT_EQ(std::get<std::string>(loaded.value()), "hello");
}

/**
 * @brief Verify TOML comments and inline-table limitations.
 * @test Check comments on tables and arrays, omission of inline-table member
 *     comments, and value preservation after loading the saved document.
 */
TEST_F(FormatTest, SavesTomlCommentsAndSkipsInlineTableMembers) {
  Dataset data;
  data.set_comments({"root"});
  data["server"]["host"] = "localhost";
  data["server"].set_comments({"server"});
  data["server"]["host"].set_comments({"host\ninjected = true"});
  data["values"] = Dataset::Array{Dataset(1), Dataset(2)};
  data["values"].set_comments({"values"});
  data["values"][std::size_t{1}].set_comments({"second"});
  data["tables"] = Dataset::Array{Dataset::Map{{"name", "one"}}};
  data["tables"][std::size_t{0}].set_comments({"table element"});
  data["tables"][std::size_t{0}]["name"].set_comments({"table name"});
  const auto path = Path("comments.toml");
  data.Save(path);
  const auto text = Read(path);
  EXPECT_EQ(text.find("# root\n"), 0u);
  EXPECT_NE(text.find("# server\n[server]"), std::string::npos);
  EXPECT_NE(text.find("# host\n# injected = true\nhost = "), std::string::npos);
  EXPECT_NE(text.find("# values\nvalues = ["), std::string::npos);
  EXPECT_NE(text.find("# second\n"), std::string::npos);
  EXPECT_NE(text.find("# table element\n[[tables]]"), std::string::npos);
  EXPECT_NE(text.find("# table name\nname = "), std::string::npos);
  Dataset loaded;
  loaded.Load(path);
  EXPECT_EQ(std::get<std::string>(loaded["server"]["host"].value()),
            "localhost");
  EXPECT_EQ(std::get<std::int64_t>(loaded["values"][std::size_t{1}].value()),
            2);
  EXPECT_EQ(
      std::get<std::string>(loaded["tables"][std::size_t{0}]["name"].value()),
      "one");

  data["tables"].set_comments({"array comment"});
  data.Save(path);
  const auto inline_text = Read(path);
  EXPECT_NE(inline_text.find("# array comment\ntables = ["), std::string::npos);
  EXPECT_NE(inline_text.find("# table element\n"), std::string::npos);
  EXPECT_EQ(inline_text.find("# table name"), std::string::npos);
  loaded.Load(path);
  EXPECT_EQ(
      std::get<std::string>(loaded["tables"][std::size_t{0}]["name"].value()),
      "one");

  data["mixed"] = Dataset::Array{
      Dataset::Map{{"inner", Dataset::Array{Dataset(4)}}}, Dataset(5)};
  data["mixed"][std::size_t{0}]["inner"][std::size_t{0}].set_comments(
      {"nested array element"});
  data.Save(path);
  EXPECT_NE(Read(path).find("# nested array element"), std::string::npos);
  loaded.Load(path);
  EXPECT_EQ(
      std::get<std::int64_t>(
          loaded["mixed"][std::size_t{0}]["inner"][std::size_t{0}].value()),
      4);
}

/**
 * @brief Verify invalid YAML and TOML comment text is rejected before writing.
 * @test NUL, invalid UTF-8, and a Unicode line separator leave the existing
 *     output file unchanged.
 */
TEST_F(FormatTest, RejectsInvalidCommentCharactersBeforeWriting) {
  Dataset data;
  data["value"] = "safe";
  for (const auto& invalid :
       {std::string("bad\0comment", 11), std::string("bad\xff", 4),
        std::string("bad\xe2\x80\xa8injected")}) {
    data["value"].set_comments({invalid});
    for (const auto& extension : {"yaml", "toml"}) {
      const auto path = Path(std::string("invalid.") + extension);
      Write(path, "original");
      EXPECT_THROW(data.Save(path), std::runtime_error);
      EXPECT_EQ(Read(path), "original");
    }
  }
}

/**
 * @brief Verify directory loading, recursion, and symlink handling.
 * @test Compare sorted paths and loaded values with recursion disabled and
 *     enabled; follow file and directory links, detect cycles, skip invalid
 *     entries, and report warnings.
 */
TEST_F(FormatTest, LoadsDirectoryWithOptionalRecursionAndSymlinks) {
  namespace fs = std::filesystem;
  const auto root = Path("data");
  ASSERT_TRUE(fs::create_directory(root));
  const auto sub = Path("data/sub");
  ASSERT_TRUE(fs::create_directory(sub));
  const auto deep = Path("data/sub/deep");
  ASSERT_TRUE(fs::create_directory(deep));
  const auto external = Path("external");
  ASSERT_TRUE(fs::create_directory(external));
  const auto csv = Path("data/a.CSV");
  const auto json = Path("data/b.json");
  const auto yaml = Path("data/c.YAML");
  const auto toml = Path("data/d.toml");
  const auto nested = Path("data/sub/e.yml");
  const auto nested_deep = Path("data/sub/deep/f.json");
  Write(csv, "value\nfirst\n");
  Write(json, "2");
  Write(yaml, "3\n");
  Write(toml, "value = 4\n");
  Write(nested, "5\n");
  Write(nested_deep, "6");
  Write(Path("external/outside.json"), "7");
  Write(Path("data/ignored.txt"), "not a dataset");
  const auto bad = Path("data/bad.json");
  Write(bad, "{");
  const auto file_link = Path("data/alias.json");
  const auto directory_link = Path("data/link");
  const auto cycle = Path("data/sub/back");
  const auto broken = Path("data/dead.json");
  const auto outside_link = Path("data/external-link");
  const auto root_link = Path("root-link");
  fs::create_symlink("b.json", file_link);
  fs::create_directory_symlink("sub", directory_link);
  fs::create_directory_symlink("..", cycle);
  fs::create_symlink("missing.json", broken);
  fs::create_directory_symlink("../external", outside_link);
  fs::create_directory_symlink("data", root_link);

  testing::internal::CaptureStderr();
  const auto shallow = Dataset::LoadDirectory(root);
  const auto shallow_warnings = testing::internal::GetCapturedStderr();
  ASSERT_EQ(shallow.size(), 5u);
  std::vector<std::string> shallow_paths;
  for (const auto& entry : shallow) shallow_paths.push_back(entry.first);
  EXPECT_EQ(shallow_paths,
            (std::vector<std::string>{csv, file_link, json, yaml, toml}));
  EXPECT_NE(shallow_warnings.find(bad), std::string::npos);
  EXPECT_NE(shallow_warnings.find(broken), std::string::npos);
  EXPECT_EQ(shallow_warnings.find("ignored.txt"), std::string::npos);
  auto it = shallow.begin();
  EXPECT_EQ(std::get<std::string>(it->second[std::size_t{0}]["value"].value()),
            "first");
  ++it;
  EXPECT_EQ(std::get<std::int64_t>(it->second.value()), 2);
  ++it;
  EXPECT_EQ(std::get<std::int64_t>(it->second.value()), 2);
  ++it;
  EXPECT_EQ(std::get<std::int64_t>(it->second.value()), 3);
  ++it;
  EXPECT_EQ(std::get<std::int64_t>(it->second["value"].value()), 4);

  testing::internal::CaptureStderr();
  const auto linked_root = Dataset::LoadDirectory(root_link, false);
  testing::internal::GetCapturedStderr();
  ASSERT_EQ(linked_root.size(), 5u);
  EXPECT_EQ(linked_root.front().first, root_link + "/a.CSV");

  testing::internal::CaptureStderr();
  const auto recursive = Dataset::LoadDirectory(root, true);
  const auto recursive_warnings = testing::internal::GetCapturedStderr();
  ASSERT_EQ(recursive.size(), 10u);
  std::vector<std::string> recursive_paths;
  for (const auto& entry : recursive) recursive_paths.push_back(entry.first);
  EXPECT_EQ(recursive_paths,
            (std::vector<std::string>{
                csv, file_link, json, yaml, toml,
                outside_link + "/outside.json", directory_link + "/deep/f.json",
                directory_link + "/e.yml", nested_deep, nested}));
  EXPECT_NE(recursive_warnings.find("directory link cycle"), std::string::npos);
  EXPECT_NE(recursive_warnings.find(bad), std::string::npos);
  EXPECT_NE(recursive_warnings.find(broken), std::string::npos);
  const auto outside =
      std::find_if(recursive.begin(), recursive.end(), [&](const auto& entry) {
        return entry.first == outside_link + "/outside.json";
      });
  ASSERT_NE(outside, recursive.end());
  EXPECT_EQ(std::get<std::int64_t>(outside->second.value()), 7);
}

/**
 * @brief Verify empty and invalid starting directories.
 * @test An empty directory returns no datasets; a missing path or regular file
 *     as the starting directory throws.
 */
TEST_F(FormatTest, RejectsInvalidRootDirectory) {
  const auto empty = Path("empty");
  ASSERT_TRUE(std::filesystem::create_directory(empty));
  EXPECT_TRUE(Dataset::LoadDirectory(empty).empty());
  EXPECT_THROW(Dataset::LoadDirectory(Path("missing")), std::runtime_error);
  const auto file = Path("file.json");
  Write(file, "{}");
  EXPECT_THROW(Dataset::LoadDirectory(file, true), std::runtime_error);
}

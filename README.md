# CppDatasetUtil

English | [日本語](README_ja.md) | [Deutsch](README_de.md) | [Français](README_fr.md) | [Italiano](README_it.md) | [한국어](README_ko.md) | [简体中文](README_zh.md)

The C++17 `Dataset` holds null, boolean, 64-bit integer, double, string, map,
and array values, along with comments associated with each value.

```cpp
#include "cppdatasetutil.h"

Dataset data;
data["server"]["host"] = "localhost";
data["server"]["port"] = 8080;
data["server"]["host"].set_comments({"Server address"});
data.Save("config.toml");
Dataset loaded;
loaded.Load("config.toml");
```

`Dataset::LoadDirectory("configs")` returns a `std::list` of
`(path, Dataset)` pairs sorted by path. Pass `true` as the second argument to
include subdirectories; the default is `false`.

```cpp
for (const auto& [path, dataset] : Dataset::LoadDirectory("configs", true)) {
  // Use path and dataset.
}
```

Supported extensions are the same as for `Load`; other files are ignored.
File and directory symlinks are followed, including links outside the starting
directory. A target reachable by multiple paths is loaded once per path.
Broken links, cycles, inaccessible entries, and files that cannot be loaded
are skipped with a warning on standard error. An inaccessible or invalid
starting directory throws `std::runtime_error`. Returned paths retain the
specified directory and symlink names.

## File formats

`Load` and `Save` select CSV, JSON, YAML (`.yaml`, `.yml`), or TOML by
case-insensitive file extension. They throw `std::runtime_error` on errors;
a failed load leaves the original Dataset unchanged. Comments are saved to
YAML and TOML, but are not loaded, so a successful load removes existing
comments. CSV and JSON do not save comments. File paths are `std::string`s
encoded as UTF-8 on every platform, including Windows, and names returned by
`LoadDirectory` are UTF-8 as well.

Set comments without a leading `#`; they are written before the value with a
`# ` prefix. Strings containing line breaks are split into multiple comment
lines. Comments must be valid UTF-8; control characters other than tabs and
line breaks, and Unicode line separators, cause a save error. TOML comments
that cannot be placed due to syntax restrictions, such as comments on values
inside inline tables, are omitted.

- CSV: The first row contains column names; each subsequent row becomes a map
  of string values in an array. Duplicate column names and rows with different
  field counts are errors. To save, every row must be a nonempty map with the
  same keys and string values. Columns are ordered by key. Empty files and
  header-only files load as empty arrays, and empty arrays save as empty files
  (header-only column names are not retained). A leading UTF-8 BOM and blank
  lines are ignored when loading; an empty value in a single-column file is
  saved as `""` so the record is preserved.
- JSON: Supports null, booleans, signed 64-bit integers, finite floating-point
  numbers, strings, arrays, and objects.
- YAML: Supports the same values as JSON and resolves basic YAML 1.2 scalar
  types. Map keys are treated as scalar strings. Multiple documents, duplicate
  anchors, recursive aliases, aliases expanding to more than 1,000,000 values in
  total, and unsupported tags are errors. Output uses flow
  style; without comments, it uses the previous JSON-compatible syntax.
- TOML: The root must be a table. Saving null, loading date/time values, and
  using non-finite floating-point numbers are errors.

Maps and arrays nested deeper than `Dataset::kMaxNestingDepth` (256) are not
loaded as `Dataset` trees. Each such value is kept as a string holding its
compact JSON text; pass it to `Dataset::Parse(text)` to convert it. YAML
anchors cannot be defined inside these values, TOML nested more than twice
the limit is rejected, and `Save` rejects values nested beyond the limit.

Run `make` to build `build/libcppdatasetutil.so`, `make test` to run the
GoogleTest suite, and `make doc` to generate Doxygen HTML documentation.
The generated documentation includes a Test List describing each GoogleTest
case. The API man pages are maintained in [docs/](docs/).
Building requires a C compiler and a C++11 or later compiler; generating HTML
documentation requires Doxygen.

Under C++11 and C++14 the library uses the bundled `mpark/variant` and `ghc::filesystem`, and `Dataset::Value` is `cppdatasetutil::variant`. Read it with `cppdatasetutil::get`, `get_if` and `holds_alternative`, which are the `std::` functions under C++17 or later. Build the library and its users with the same language level (C++17 or later, or C++11/14). With Make, pass `make CXX_STD=c++11`.

## Windows DLL and help

With the Visual Studio 2022 C++ desktop tools and CMake installed, run from
the project root:

```powershell
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64
cmake --build build-windows --config Release
ctest --test-dir build-windows -C Release --output-on-failure
cmake --build build-windows --config Release --target windows_help
```

The DLL and import library are `build-windows\Release\cppdatasetutil.dll` and
`cppdatasetutil.lib`. Include `Include\cppdatasetutil.h` and link the import
library using the same architecture and a compatible MSVC toolset and runtime
configuration. Put the DLL next to the executable or on the Windows DLL
search path at runtime. `CPPDATASETUTIL_BUILD` is for building the library
only; consumers must not define it. `Dataset` exposes standard library members,
so the DLL and its consumers must use the same CRT flavor (`/MD` or `/MT`,
Debug or Release) and `_ITERATOR_DEBUG_LEVEL`; a mismatch fails at link time
with an unresolved `CppDatasetUtilAbi1_*` symbol instead of crashing at run
time. The test runs a separate DLL-linked consumer executable. The Release DLL
also requires the compatible Microsoft Visual C++ Redistributable on the
target machine.

The `windows_help` target uses Microsoft HTML Help Workshop's `hhc.exe` to
produce `build-windows\help\CppDatasetUtil.chm`. Choose English, Japanese,
German, French, Italian, Korean, or Simplified Chinese on its language page;
each topic links back to that page.
If the compiler is installed elsewhere, configure CMake with
`-DHTML_HELP_COMPILER="C:\path\to\hhc.exe"`. Without the workshop the DLL
can still be built, but the help target is not available. The help sources
are in [docs/windows/](docs/windows/), based on the corresponding man pages.
Keep both sets of documentation in sync when changing the API reference.

## Data-driven testing

The tests use one subdirectory per test item under `Tests/data/`:

```text
Tests/data/
  nested-values/   JSON and TOML cases for map access
  array-values/    YAML and JSON cases for array access
  server-config/   JSON and TOML cases for the example model
```

Each file contains a `cases` array. Each case has an `input` and an `expected`
value. The two files used by the example contain:

`Tests/data/server-config/basic.json`:

```json
{
  "cases": [
    {
      "input": {"host": "localhost", "port": 8080, "enabled": true},
      "expected": {"endpoint": "localhost:8080", "enabled": true}
    },
    {
      "input": {"host": "127.0.0.1", "port": 0, "enabled": false},
      "expected": {"endpoint": "127.0.0.1:0", "enabled": false}
    }
  ]
}
```

`Tests/data/server-config/alternate.toml`:

```toml
[[cases]]
[cases.input]
host = "example.net"
port = 443
enabled = false

[cases.expected]
endpoint = "example.net:443"
enabled = false
```

The following GoogleTest example reads the existing two files from the
project root. `Dataset` reads the cases; the independent `ServerConfig` model
is the test subject. The test compares its endpoint and enabled state with
the expected values:

```cpp
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include <gtest/gtest.h>
#include "cppdatasetutil.h"

class ServerConfig {
 public:
  ServerConfig(std::string host, std::int64_t port, bool enabled)
      : host_(std::move(host)), port_(port), enabled_(enabled) {}

  std::string endpoint() const { return host_ + ":" + std::to_string(port_); }
  bool enabled() const { return enabled_; }

 private:
  std::string host_;
  std::int64_t port_;
  bool enabled_;
};

TEST(ServerConfigTest, BuildsEndpointAndStoresEnabledState) {
  const auto files = Dataset::LoadDirectory("Tests/data/server-config");
  ASSERT_EQ(files.size(), 2u);
  for (const auto& [path, dataset] : files) {
    SCOPED_TRACE(path);
    const auto& cases = std::get<Dataset::Array>(dataset["cases"].value());
    ASSERT_FALSE(cases.empty());
    for (std::size_t i = 0; i < cases.size(); ++i) {
      SCOPED_TRACE(i);
      const Dataset& input = cases[i]["input"];
      const Dataset& expected = cases[i]["expected"];
      const ServerConfig server(std::get<std::string>(input["host"].value()),
                                std::get<std::int64_t>(input["port"].value()),
                                std::get<bool>(input["enabled"].value()));
      EXPECT_EQ(server.endpoint(),
                std::get<std::string>(expected["endpoint"].value()));
      EXPECT_EQ(server.enabled(), std::get<bool>(expected["enabled"].value()));
    }
  }
}
```

[Tests/DatasetTest.cpp](Tests/DatasetTest.cpp) contains more examples.
`LoadDirectory` skips unreadable files, so verify the expected file count or
check its warnings to avoid silently missing cases. The project's tests check
for warnings and empty files. Run them with `make test` from the project root.
The example above is separate: save it as a source file, link it with
`cppdatasetutil` and `gtest_main`, and run it from the project root.

## Linux packages

Build on the target distribution and architecture to ensure that the package
dependencies and binary ABI match the target system:

```sh
make rpm   # RPM-based distribution: rpmbuild, RPM build tools, tar, C/C++ tools
make deb   # Debian-based distribution: dpkg-dev, dpkg-deb, make, C/C++ tools
```

Packages are written to `dist/`. Each contains the shared library, development
header, English, Japanese, German, French, Italian, Korean and Simplified
Chinese API man pages, and project and bundled dependency
licenses. After installation, use `man 3 CppDataSet` to read the API reference.
With a supported UTF-8 locale, the corresponding page is selected
automatically; `man -L de_DE.UTF-8 3 CppDataSet` explicitly selects German when
that locale is available. Translations are maintained in [docs/](docs/);
Chinese is provided for the `zh_CN` locale.
The library has SONAME
`libcppdatasetutil.so.0`. Runtime dependencies are derived from the build
system's libraries; a package built on one distribution or release is not
necessarily compatible with another.

The package version comes from the root [VERSION](VERSION) file. Set
`PACKAGE_VERSION` to override it, for example `PACKAGE_VERSION=1.2.3 make deb`.
Versions must be `X.Y.Z`, with no leading zeroes except for zero itself.
Run `make package-test-rpm` or
`make package-test-deb` on the corresponding distribution to build, inspect,
and link against an extracted package. RPM tests also require `rpm2cpio` and
`cpio`; both package tests require `readelf`.

The **Release packages** GitHub Actions workflow is started manually.
The workflow must exist on the default branch. Choose the branch or tag to
release; its `VERSION` determines the `vX.Y.Z` release tag. A selected tag
must match `VERSION`, and an existing release tag must point to the selected
commit. If the release tag does not exist, the workflow creates it after all
builds succeed, then publishes a GitHub Release with the DEB (Ubuntu 24.04,
x86-64), RPM (Fedora 44, x86-64), and Windows x64 DLL and CHM. A Windows SDK
ZIP also contains the DLL, import library, header, CHM, and licenses.
The Windows job downloads the archived Microsoft HTML Help Workshop installer
and verifies its SHA-256 before extracting the compiler.
Release notes contain Git commit subjects since the nearest version tag
reachable from the selected commit's first parent, or the full history if none
exists. An existing release is not replaced.

The project is distributed under the [MIT license](LICENSE).

## Dependencies

| Library | Purpose | Availability | License |
| --- | --- | --- | --- |
| [nlohmann/json 3.12.0](https://github.com/nlohmann/json) | JSON reading and writing, YAML output | Bundled, header-only | [MIT](third_party/json/LICENSE.MIT) |
| [libyaml 0.2.5](https://github.com/yaml/libyaml) | YAML reading | Bundled, compiled into the shared library | [MIT](third_party/libyaml/License) |
| [toml11 4.4.0](https://github.com/ToruNiina/toml11) | TOML reading and writing | Bundled, header-only | [MIT](third_party/toml11/LICENSE) |
| [GoogleTest](https://github.com/google/googletest) | Tests only | Must be installed separately | [BSD 3-Clause](licenses/googletest-LICENSE) |

See [third_party/README.md](third_party/README.md) for bundled dependency details.

## Coding style

C++ code follows Google C++ Style. With the VS Code C/C++ extension
(`ms-vscode.cpptools`), files are formatted on save using `.clang-format`.
Run `make format` to format code or `make style-check` to check formatting.
Both commands require `clang-format`.

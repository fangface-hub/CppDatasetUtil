# CppDatasetUtil

[English](README.md) | [日本語](README_ja.md) | [Deutsch](README_de.md) | [Français](README_fr.md) | [Italiano](README_it.md) | [한국어](README_ko.md) | 简体中文

C++17 `Dataset` 可保存 null、布尔值、64 位整数、double、字符串、映射、
数组以及与各个值关联的注释。

```cpp
#include "Dataset.h"
Dataset data;
data["server"]["host"] = "localhost";
data["server"]["port"] = 8080;
data["server"]["host"].set_comments({"Server address"});
data.Save("config.toml");
Dataset loaded;
loaded.Load("config.toml");
```

`Dataset::LoadDirectory("configs")` 返回按路径排序的 `(路径, Dataset)`
对组成的 `std::list`。将第二个参数设为 `true` 可遍历子目录，默认值为
`false`。不支持的扩展名会被忽略。程序会跟随文件和目录的符号链接，
包括指向起始目录之外的链接；同一目标通过多个路径到达时，每个路径都会
读取一次。失效链接、循环、无法访问的条目及无法读取的文件会被跳过，
并向标准错误输出警告。起始目录无效或无法访问时抛出 `std::runtime_error`。
返回路径保留指定的目录和链接名称。

```cpp
for (const auto& [path, dataset] : Dataset::LoadDirectory("configs", true)) {
  // Use path and dataset.
}
```

## 文件格式

`Load` 和 `Save` 根据不区分大小写的扩展名选择 CSV、JSON、YAML
（`.yaml`、`.yml`）或 TOML。出错时抛出 `std::runtime_error`；
读取失败不会更改原 Dataset。注释可保存到 YAML 和 TOML，但不会被读取，
因此成功读取时会清除原有注释。CSV 和 JSON 不保存注释。文件路径在所有平台（包括 Windows）上都按 UTF-8 的
`std::string` 处理，`LoadDirectory` 返回的名称也是 UTF-8。

设置注释时不要在开头添加 `#`；输出时会在值之前加上 `# ` 前缀。
包含换行的字符串会拆分为多个注释行。注释必须是有效的 UTF-8；
除制表符和换行符以外的控制字符以及 Unicode 行分隔符会导致保存错误。
因 TOML 语法限制无法放置的注释（例如内联表中值的注释）会被省略。

- CSV：首行为列名，其余各行作为字符串值映射存入数组。重复列名或字段数
  不一致均为错误。保存时每一行必须是非空映射，具有相同的键且值均为
  字符串；列按键排序。空文件和只有表头的文件读取为空数组，空数组
     保存为空文件，因此仅有表头的列名不会保留。读取时会忽略开头的 UTF-8 BOM 和
  空行；单列文件中的空值保存为 `""`。
- JSON：支持 null、布尔值、有符号 64 位整数、有限浮点数、字符串、数组
  和对象。
- YAML：支持与 JSON 相同的值，并解析 YAML 1.2 基本标量类型。映射键
  被视为标量字符串。多文档、重复锚点、递归别名、总共展开为超过 1,000,000 个值的别名及不支持的
  标签均为错误。输出使用流式格式；没有注释时使用兼容 JSON 的语法。
- TOML：根节点必须是表。不支持保存 null、读取日期时间值或使用非有限
  浮点数。

嵌套深度超过 `Dataset::kMaxNestingDepth`（256）的映射和数组不会作为
`Dataset` 树读取，而是以保存其紧凑 JSON 文本的字符串保留，可用
`Dataset::Parse(text)` 转换。这些值内部不能定义 YAML 锚点，嵌套超过限制两倍的
TOML 会被拒绝，`Save` 也会拒绝嵌套超过限制的值。

`make` 构建 `build/libcppdatasetutil.so`，`make test` 运行 GoogleTest，
`make doc` 生成包含测试列表的 Doxygen HTML 文档。API man 页面在
[docs/](docs/) 中维护。构建需要 C 和 C++17 编译器；生成 HTML 文档需要
Doxygen。

## 数据驱动测试

`Tests/data/` 为每个测试项目提供子目录：`nested-values/` 包含映射访问的
JSON 和 TOML 测试用例，`array-values/` 包含数组访问的 YAML 和 JSON
测试用例，`server-config/` 包含模型示例的用例。每个文件的 `cases`
数组包含 `input` 和 `expected`。示例使用的两个文件如下：

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

下面的 GoogleTest 示例从项目根目录运行：`Dataset` 读取用例，独立的
`ServerConfig` 模型是测试对象，测试其端点和启用状态：

```cpp
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include <gtest/gtest.h>
#include "Dataset.h"

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

[Tests/DatasetTest.cpp](Tests/DatasetTest.cpp) 包含更多示例。
由于 `LoadDirectory` 会跳过无法读取的文件，请检查预期文件数或警告。
项目测试会检查警告和空用例。请在项目根目录运行 `make test`。
上面的示例需单独保存为源文件，链接 `cppdatasetutil` 和 `gtest_main`，
然后从项目根目录运行。

## Linux 软件包

请在目标发行版和架构上构建，以确保依赖关系和二进制 ABI 与目标系统匹配：

```sh
make rpm   # rpmbuild、RPM 构建工具、tar、C/C++ 工具
make deb   # dpkg-dev、dpkg-deb、make、C/C++ 工具
```

`dist/` 中的软件包包含共享库、开发头文件、man 页面和许可证。安装后可通过
`man 3 CppDataSet` 查看 API；在支持的 UTF-8 区域设置下会自动选择对应翻译，
例如区域设置可用时可使用 `man -L zh_CN.UTF-8 3 CppDataSet`。翻译在
[docs/](docs/) 中维护；中文针对 `zh_CN`。SONAME 为
`libcppdatasetutil.so.0`。运行时依赖由构建环境决定，软件包不一定兼容
其他发行版或版本。

版本取自 [VERSION](VERSION)，可通过 `PACKAGE_VERSION=1.2.3 make deb`
覆盖。格式必须为 `X.Y.Z`，除 `0` 本身外不允许前导零。
`make package-test-rpm` 或 `make package-test-deb` 会构建、检查软件包并
测试链接解压后的库。两种测试都需要 `readelf`，RPM 测试还需要
`rpm2cpio` 和 `cpio`。

GitHub Actions 的 **Release packages** 工作流需要手动运行，且必须
位于默认分支。所选分支或标签中的 `VERSION` 决定 `vX.Y.Z` 标签。
所选标签必须与版本匹配；已有发布标签必须指向所选提交。如果标签不存在，
将在所有构建成功后创建。GitHub Release 包含 DEB（Ubuntu 24.04、
x86-64）、RPM（Fedora 44、x86-64）及 Windows x64 DLL 和 CHM。
Windows SDK ZIP 还包含导入库、头文件和许可证。发布说明包含从所选提交的第一父
提交可到达的最近版本标签之后的提交标题；如果没有该标签则包含完整历史。
已有 Release 不会被替换。

本项目依据 [MIT 许可证](LICENSE)发布。

## 依赖

| 库 | 用途 | 提供方式 | 许可证 |
| --- | --- | --- | --- |
| [nlohmann/json 3.12.0](https://github.com/nlohmann/json) | JSON 读写、YAML 输出 | 已包含，仅头文件 | [MIT](third_party/json/LICENSE.MIT) |
| [libyaml 0.2.5](https://github.com/yaml/libyaml) | YAML 读取 | 已包含，编译进共享库 | [MIT](third_party/libyaml/License) |
| [toml11 4.4.0](https://github.com/ToruNiina/toml11) | TOML 读写 | 已包含，仅头文件 | [MIT](third_party/toml11/LICENSE) |
| [GoogleTest](https://github.com/google/googletest) | 仅测试 | 需单独安装 | [BSD 3-Clause](licenses/googletest-LICENSE) |

有关内置依赖的详细信息，请参阅 [third_party/README.md](third_party/README.md)。

## 代码风格

C++ 代码遵循 Google C++ Style。VS Code 的 C/C++ 扩展
（`ms-vscode.cpptools`）可在保存时根据 `.clang-format` 格式化代码。
`make format` 用于格式化，`make style-check` 用于检查；两个命令都需要
`clang-format`。

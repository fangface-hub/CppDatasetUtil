# CppDatasetUtil

[English](README.md) | 日本語 | [Deutsch](README_de.md) | [Français](README_fr.md) | [Italiano](README_it.md) | [한국어](README_ko.md) | [简体中文](README_zh.md)

C++17 の `Dataset` は null、真偽値、64 ビット整数、double、文字列、
マップ、配列と、値に関連付けられたコメントを保持します。

```cpp
#include "cppdatasetutil.h"

Dataset data;
data["server"]["host"] = "localhost";
data["server"]["port"] = 8080;
data["server"]["host"].set_comments({"接続先"});
data.Save("config.toml");
Dataset loaded;
loaded.Load("config.toml");
```

`Dataset::LoadDirectory("configs")` は `(パス, Dataset)` の組をパス順の
`std::list` で返します。第 2 引数に `true` を指定するとサブディレクトリも
読み込みます。デフォルトは `false` です。

```cpp
for (const auto& [path, dataset] : Dataset::LoadDirectory("configs", true)) {
  // path と dataset を使用する。
}
```

対象の拡張子は `Load` と同じで、それ以外のファイルは無視します。ファイルと
ディレクトリのシンボリックリンクをたどり、開始ディレクトリの外側も対象に
なります。同じリンク先に複数のパスで到達する場合はパスごとに読み込みます。
リンク切れ、循環、アクセスできないエントリー、読み込みに失敗したファイルは
標準エラーに警告を出してスキップします。開始ディレクトリが無効、または
アクセスできない場合は `std::runtime_error` を送出します。返されるパスには
指定されたディレクトリとリンクの名前がそのまま含まれます。

## ファイル形式

`Load` / `Save` は拡張子（大文字小文字を区別しない）で CSV、JSON、
YAML (`.yaml`, `.yml`)、TOML を選択します。エラー時は
`std::runtime_error` を送出し、読み込みに失敗した場合は元の Dataset を
変更しません。コメントは YAML / TOML に保存されます。読み込みには対応して
いないため、読み込み成功時には消去されます。CSV / JSON には保存されません。ファイルパスは Windows を含む全プラットフォームで
UTF-8 の `std::string` として扱われ、`LoadDirectory` が返す名前も UTF-8 です。

コメントは行頭に `#` を付けずに設定し、値の前に `# ` を付けて出力
します。改行を含む文字列は複数のコメント行に分割します。コメントには有効な
UTF-8 を使用し、タブ・改行以外の制御文字や Unicode の行区切り文字を含めると
保存時にエラーになります。TOML ではインラインテーブル内の値など、構文上
配置できないコメントは省略します。

- CSV: 先頭行を列名とし、各行を文字列値のマップとする配列です。重複する
  列名や列数の異なる行はエラーです。保存時は全行が同じキーを持つ、空でない
  マップで、値は文字列である必要があります。列順はキーの辞書順です。
  空ファイルとヘッダーのみのファイルは空配列になり、空配列は空ファイルに
  保存されます（ヘッダーのみの列名は保持されません）。読み込み時は先頭の
  UTF-8 BOM と空行を無視します。1 列のファイルの空値は、レコードが失われない
  よう `""` として保存されます。
- JSON: null、真偽値、符号付き 64 ビット整数、有限の浮動小数点数、
  文字列、配列、オブジェクトに対応します。
- YAML: JSON と同じ値に対応し、YAML 1.2 の基本的なスカラー型を解決します。
  マップのキーはスカラー文字列として扱います。複数文書、重複アンカー、
  再帰的な別名、合計 1,000,000 個を超える値に展開される別名、
  未対応のタグはエラーです。保存はフロー形式で行い、
  コメントがなければ従来どおり JSON 互換の構文を使用します。
- TOML: ルートはテーブルである必要があります。null の保存、日時型の
  読み込み、非有限の浮動小数点数はエラーです。

`Dataset::kMaxNestingDepth`（256）より深くネストしたマップと配列は
`Dataset` のツリーとしては読み込まれず、コンパクトな JSON テキストを保持する
文字列になります。`Dataset::Parse(text)` で `Dataset` に変換できます。
これらの値の内部で YAML のアンカーは定義できず、上限の 2 倍を超える TOML は
拒否され、`Save` は上限を超える値を拒否します。

`make` で `build/libcppdatasetutil.so` を生成し、`make test` で
GoogleTest を実行します。`make doc` で Doxygen の HTML を生成します。
生成されるドキュメントの Test List には各 GoogleTest ケースの説明も
含まれます。API の man ページは [docs/](docs/) で管理します。
ビルドには C コンパイラと C++17 コンパイラ、HTML 文書生成には
Doxygen が必要です。

## Windows DLL とヘルプ

Visual Studio 2022 の C++ デスクトップ開発ツールと CMake を使用し、
プロジェクトルートで次を実行します。

```powershell
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64
cmake --build build-windows --config Release
ctest --test-dir build-windows -C Release --output-on-failure
cmake --build build-windows --config Release --target windows_help
```

`build-windows\Release\cppdatasetutil.dll` とインポートライブラリ
`cppdatasetutil.lib` が生成されます。利用側は `Include\cppdatasetutil.h` を
インクルードし、同じアーキテクチャと互換性のある MSVC ツールセット・
ランタイム設定で `cppdatasetutil.lib` にリンクしてください。実行時は DLL を
実行ファイルと同じディレクトリなど Windows の DLL 検索パスに配置します。
`CPPDATASETUTIL_BUILD` はライブラリのビルド専用で、利用側では定義しません。
`Dataset` は標準ライブラリの型をメンバーに持つため、DLL と利用側で CRT の種類
（`/MD` か `/MT`、Debug か Release）と `_ITERATOR_DEBUG_LEVEL` を一致させる必要が
あります。不一致の場合は実行時にクラッシュせず、未解決の `CppDatasetUtilAbi1_*`
シンボルとしてリンク時にエラーになります。
テストは DLL をリンクした別の実行ファイルで実行します。Release DLL の
実行先には互換性のある Microsoft Visual C++ 再頒布可能パッケージも必要です。

`windows_help` は Microsoft HTML Help Workshop の `hhc.exe` を使用し、
`build-windows\help\CppDatasetUtil.chm` を生成します。標準のインストール先
以外にある場合は CMake 設定時に
`-DHTML_HELP_COMPILER="C:\path\to\hhc.exe"` を指定してください。
Workshop がない場合は DLL をビルドできますが、ヘルプターゲットは作成されません。
生成した CHM の言語選択画面から英語、日本語、ドイツ語、フランス語、
イタリア語、韓国語、簡体字中国語の API リファレンスを選択できます。
各ページから言語選択画面に戻れます。ヘルプのソースは対応する man ページを
基にした [docs/windows/](docs/windows/) です。API リファレンスを変更する際は
両方を更新してください。

## データ駆動テスト

`Tests/data/` 配下にテスト項目ごとのサブディレクトリを用意しています。

```text
Tests/data/
  nested-values/   マップアクセス用の JSON・TOML ケース
  array-values/    配列アクセス用の YAML・JSON ケース
  server-config/   モデルの例用の JSON・TOML ケース
```

各ファイルには `cases` 配列を置き、各ケースに入力値 `input` と規格値
`expected` を定義します。例で使用する 2 ファイルの内容は次のとおりです。

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

次の GoogleTest の例は、プロジェクトルートから既存の 2 ファイルを読み込みます。
`Dataset` はテストデータの読み込みに使用し、独立したモデル `ServerConfig` の
エンドポイントと有効状態を期待値と比較します。

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

`LoadDirectory` は読み込めないファイルをスキップするため、ケースの欠落を
見逃さないよう、想定ファイル数または警告を確認してください。本プロジェクトの
テストは警告や空のケースを検出します。プロジェクトルートで `make test` を
実行してください。上記の例は別途ソースファイルに保存し、
`cppdatasetutil` と `gtest_main` にリンクして、プロジェクトルートから
実行してください。

## Linux パッケージ

パッケージの依存関係とバイナリ ABI を対象環境に合わせるため、対象の
ディストリビューションとアーキテクチャ上でビルドします。

```sh
make rpm   # RPM 系: rpmbuild、RPM ビルドツール、tar、C/C++ コンパイラ
make deb   # Debian 系: dpkg-dev、dpkg-deb、make、C/C++ コンパイラ
```

パッケージは `dist/` に生成され、共有ライブラリ、開発用ヘッダー、API の
英語・日本語・ドイツ語・フランス語・イタリア語・韓国語・中国語（簡体字）の
man ページ、および本プロジェクトと同梱依存ライブラリの
ライセンスを含みます。インストール後は `man 3 CppDataSet` で API リファレンスを
参照できます。対応する UTF-8 ロケールでは各言語版が自動的に選択されます。
ロケールが利用可能な環境では `man -L ja_JP.UTF-8 3 CppDataSet` のように
指定できます。翻訳は [docs/](docs/) で管理し、中国語は `zh_CN` 向けです。
ライブラリの
SONAME は `libcppdatasetutil.so.0` です。実行時依存関係はビルド環境から
取得するため、別のディストリビューションやリリースでは動作するとは限りません。

パッケージのバージョンはルートの [VERSION](VERSION) ファイルから取得します。
`PACKAGE_VERSION` を設定すると上書きできます（例:
`PACKAGE_VERSION=1.2.3 make deb`）。バージョンは `X.Y.Z` 形式で、
各要素は 0 自体を除き先頭にゼロを付けられません。対象環境で
`make package-test-rpm` または `make package-test-deb` を実行すると、
パッケージの生成・内容・展開後のリンクを検証できます。RPM のテストには
`rpm2cpio` と `cpio`、両テストには `readelf` も必要です。

GitHub Actions の **Release packages** は手動で実行します。ワークフローを
デフォルトブランチに配置したうえで、対象のブランチまたはタグを選択します。
その `VERSION` に対応する `vX.Y.Z` タグを使用します。選択したタグは
`VERSION` と一致し、既存タグは選択したコミットを指している必要があります。タグが
なければ全ビルドの成功後に作成し、DEB（Ubuntu 24.04、x86-64）、
RPM（Fedora 44、x86-64）、Windows x64 の DLL と CHM を添付した
GitHub Release を公開します。Windows SDK の ZIP には DLL、インポート
ライブラリ、ヘッダー、CHM、ライセンスを収録します。Windows ジョブは
アーカイブされた Microsoft HTML Help Workshop インストーラーの SHA-256 を
検証してからコンパイラを展開します。
リリースノートには選択コミットの第一親から到達可能な最も近いバージョンタグ
以降のコミット件名を記載し、タグがない場合は全履歴を記載します。
既存のリリースは置き換えません。

本プロジェクトは [MIT ライセンス](LICENSE) で配布します。

## 依存ライブラリ

| ライブラリ | 用途 | 提供方法 | ライセンス |
| --- | --- | --- | --- |
| [nlohmann/json 3.12.0](https://github.com/nlohmann/json) | JSON の読み書き、YAML の出力 | 同梱、ヘッダーのみ | [MIT](third_party/json/LICENSE.MIT) |
| [libyaml 0.2.5](https://github.com/yaml/libyaml) | YAML の読み込み | 同梱、共有ライブラリに組み込み | [MIT](third_party/libyaml/License) |
| [toml11 4.4.0](https://github.com/ToruNiina/toml11) | TOML の読み書き | 同梱、ヘッダーのみ | [MIT](third_party/toml11/LICENSE) |
| [GoogleTest](https://github.com/google/googletest) | テストのみ | 別途インストールが必要 | [BSD 3-Clause](licenses/googletest-LICENSE) |

同梱ライブラリの詳細は [third_party/README.md](third_party/README.md) を
参照してください。

## コーディングスタイル

C++ コードには Google C++ Style を使用します。VS Code の C/C++
拡張機能（`ms-vscode.cpptools`）をインストールすると、保存時に
`.clang-format` の設定でフォーマットされます。コマンドラインでは
`make format` で整形し、`make style-check` で確認できます。
これらのコマンドには `clang-format` が必要です。

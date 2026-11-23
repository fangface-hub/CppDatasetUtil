# CppDatasetUtil

[English](README.md) | [日本語](README_ja.md) | [Deutsch](README_de.md) | [Français](README_fr.md) | [Italiano](README_it.md) | 한국어 | [简体中文](README_zh.md)

C++17 `Dataset`은 null, 불리언, 64비트 정수, double, 문자열, 맵,
배열 및 각 값에 연결된 주석을 보유합니다.

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

`Dataset::LoadDirectory("configs")`는 `(경로, Dataset)` 쌍을 경로순으로
정렬한 `std::list`를 반환합니다. 두 번째 인수로 `true`를 전달하면 하위
디렉터리도 탐색하며 기본값은 `false`입니다. 지원하지 않는 확장자는
무시합니다. 파일과 디렉터리의 심볼릭 링크를 따라 시작 디렉터리 밖도
탐색하며, 하나의 대상에 여러 경로로 도달하면 경로마다 읽습니다.
끊어진 링크, 순환, 접근할 수 없는 항목 및 읽기에 실패한 파일은 표준
오류에 경고하고 건너뜁니다. 시작 디렉터리가 잘못되었거나 접근할 수
없으면 `std::runtime_error`가 발생합니다. 반환된 경로는 지정된
디렉터리와 링크 이름을 유지합니다.

```cpp
for (const auto& [path, dataset] : Dataset::LoadDirectory("configs", true)) {
  // Use path and dataset.
}
```

## 파일 형식

`Load`와 `Save`는 대소문자를 구분하지 않는 확장자로 CSV, JSON, YAML
(`.yaml`, `.yml`), TOML을 선택합니다. 오류 시 `std::runtime_error`가
발생하며 읽기에 실패하면 Dataset이 변경되지 않습니다. 주석은 YAML과
TOML에 저장되지만 읽지는 않으므로 읽기 성공 시 기존 주석이 지워집니다.
CSV와 JSON은 주석을 저장하지 않습니다. 파일 경로는 Windows를 포함한 모든
플랫폼에서 UTF-8 `std::string`으로 처리되며 `LoadDirectory`가 반환하는 이름도
UTF-8입니다.

주석은 앞에 `#` 없이 설정하며 값 앞에 `# `를 붙여 출력합니다. 줄 바꿈이
있는 문자열은 여러 주석 줄로 나뉩니다. 유효한 UTF-8이 필요하며 탭과
줄 바꿈 이외의 제어 문자 및 Unicode 줄 구분자는 저장 오류를 일으킵니다.
인라인 테이블 내부의 값처럼 TOML 구문상 배치할 수 없는 주석은 생략됩니다.

- CSV: 첫 줄은 열 이름이고 이후 각 줄은 문자열 값을 가진 맵으로 배열에
  저장됩니다. 중복 열 이름과 다른 필드 수는 오류입니다. 저장할 때 모든
  맵은 비어 있지 않고 같은 키와 문자열 값을 가져야 하며 열은 키순으로
  정렬됩니다. 빈 파일과 헤더만 있는 파일은 빈 배열로 읽고 빈 배열은
  빈 파일로 저장하므로 헤더의 열 이름은 유지되지 않습니다. 읽을 때 맨 앞의
  UTF-8 BOM과 빈 줄은 무시하며, 한 열짜리 파일의 빈 값은 `""`로 저장됩니다.
- JSON: null, 불리언, 부호 있는 64비트 정수, 유한 실수, 문자열, 배열,
  객체를 지원합니다.
- YAML: JSON과 같은 값과 기본 YAML 1.2 스칼라 타입을 지원하며 맵 키를
  스칼라 문자열로 취급합니다. 여러 문서, 중복 앵커, 재귀 별칭, 전체 1,000,000개를 넘는 값으로 확장되는
  별칭 및 미지원 태그는 오류입니다. 출력은 flow 형식이며 주석이 없으면
  JSON 호환 구문을 사용합니다.
- TOML: 루트는 테이블이어야 합니다. null 저장, 날짜/시간 읽기 및
  유한하지 않은 실수는 지원하지 않습니다.

`Dataset::kMaxNestingDepth`(256)보다 깊게 중첩된 맵과 배열은 `Dataset`
트리로 읽지 않습니다. 해당 값은 간결한 JSON 텍스트를 담은 문자열로 유지되며
`Dataset::Parse(text)`로 변환할 수 있습니다. 이러한 값 내부에서는 YAML 앵커를
정의할 수 없고, 한도의 2배를 넘는 TOML은 거부되며, `Save`는 한도를 넘는
중첩 값을 거부합니다.

`make`는 `build/libcppdatasetutil.so`를 빌드하고 `make test`는
GoogleTest를 실행합니다. `make doc`은 테스트 목록이 포함된 Doxygen
HTML을 생성합니다. API man 페이지는 [docs/](docs/)에서 관리합니다.
빌드에는 C 및 C++11 이상 컴파일러가 필요하며 HTML 문서 생성에는 Doxygen이
필요합니다.

C++11/14에서는 포함된 `mpark/variant`와 `ghc::filesystem`을 사용하며 `Dataset::Value`는 `cppdatasetutil::variant`입니다. 값은 `cppdatasetutil::get`, `get_if`, `holds_alternative`로 읽으세요(C++17 이상에서는 `std::` 함수입니다). 라이브러리와 사용 코드는 같은 언어 수준(C++17 이상 또는 C++11/14)으로 빌드해야 하며 Make에서는 `make CXX_STD=c++11`을 지정합니다.

## Windows DLL 및 도움말

Visual Studio 2022 C++ 데스크톱 도구와 CMake를 설치한 뒤 프로젝트
루트에서 실행합니다.

```powershell
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64
cmake --build build-windows --config Release
ctest --test-dir build-windows -C Release --output-on-failure
cmake --build build-windows --config Release --target windows_help
```

DLL과 가져오기 라이브러리는 `build-windows\Release\cppdatasetutil.dll`과
`cppdatasetutil.lib`입니다. `Include\cppdatasetutil.h`를 포함하고 같은
아키텍처, 호환되는 MSVC 도구 집합 및 런타임 구성으로 가져오기 라이브러리에
링크합니다. 실행 시 DLL은 실행 파일과 같은 디렉터리나 Windows DLL 검색
경로에 두십시오. `CPPDATASETUTIL_BUILD`는 라이브러리를 빌드할 때만 쓰며
사용하는 쪽에서 정의하면 안 됩니다. `Dataset`은 표준 라이브러리 형식을
멤버로 가지므로 DLL과 사용하는 쪽은 같은 CRT 종류(`/MD` 또는 `/MT`, Debug
또는 Release)와 `_ITERATOR_DEBUG_LEVEL`을 사용해야 합니다. 다르면 실행 중
충돌하는 대신 확인되지 않은 `CppDatasetUtilAbi1_*` 심볼로 링크 단계에서
오류가 발생합니다. 테스트는 DLL에 링크된 별도의 실행 파일로 실행됩니다.
Release DLL을 쓰려면 대상 컴퓨터에 호환되는 Microsoft Visual C++
재배포 가능 패키지도 필요합니다.

`windows_help` 대상은 Microsoft HTML Help Workshop의 `hhc.exe`로
`build-windows\help\CppDatasetUtil.chm`을 생성합니다. 언어 선택 페이지에서
영어, 일본어, 독일어, 프랑스어, 이탈리아어, 한국어, 중국어 간체를 고를 수
있고 각 항목에서 이 페이지로 돌아올 수 있습니다. 컴파일러가 다른 위치에
있으면 CMake를 `-DHTML_HELP_COMPILER="C:\path\to\hhc.exe"`로 구성하십시오.
Workshop이 없어도 DLL은 빌드할 수 있지만 도움말 대상은 사용할 수 없습니다.
도움말 소스는 해당 man 페이지를 바탕으로 한
[docs/windows/](docs/windows/)에 있습니다. API 참조를 변경할 때는 두 문서를
함께 갱신하십시오.

## 데이터 기반 테스트

테스트는 `Tests/data/` 아래에 테스트 항목별 하위 디렉터리를 둡니다.

```text
Tests/data/
  nested-values/   맵 접근용 JSON·TOML 케이스
  array-values/    배열 접근용 YAML·JSON 케이스
  server-config/   모델 예제용 JSON·TOML 케이스
```

각 파일에는 `cases` 배열이 있으며 각 케이스는 `input`과 `expected` 값을
가집니다. 예제에서 사용하는 두 파일의 내용은 다음과 같습니다.

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

프로젝트 루트에서 실행하는 GoogleTest 예제입니다. `Dataset`은 케이스를
읽고, 독립적인 모델 `ServerConfig`의 엔드포인트와 활성 상태를 검증합니다.

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

[Tests/DatasetTest.cpp](Tests/DatasetTest.cpp)에 다른 예제도 있습니다.
`LoadDirectory`는 읽을 수 없는 파일을 건너뛰므로 예상 파일
수나 경고를 확인하십시오. 프로젝트 테스트는 경고와 빈 케이스를
검사합니다. 프로젝트 루트에서 `make test`를 실행합니다. 위 예제는 별도
소스 파일로 저장하고 `cppdatasetutil`과 `gtest_main`에 링크한 후
프로젝트 루트에서 실행합니다.

## Linux 패키지

대상 시스템의 의존성과 바이너리 ABI에 맞추려면 대상 배포판과
아키텍처에서 빌드합니다.

```sh
make rpm   # rpmbuild, RPM 빌드 도구, tar, C/C++ 도구
make deb   # dpkg-dev, dpkg-deb, make, C/C++ 도구
```

`dist/`의 패키지에는 공유 라이브러리, 개발용 헤더, 영어·일본어·독일어·
프랑스어·이탈리아어·한국어·중국어 간체 API man 페이지, 프로젝트와 포함된
의존성의 라이선스가 들어 있습니다. 설치 후 `man 3 CppDataSet`으로 API를 볼 수 있으며 지원되는
UTF-8 로케일에서는 번역이 자동 선택됩니다. 예를 들어 로케일을 사용할 수
있으면 `man -L ko_KR.UTF-8 3 CppDataSet`으로 지정할 수 있습니다. 번역은
[docs/](docs/)에서 관리하며 중국어는 `zh_CN`용입니다. SONAME은
`libcppdatasetutil.so.0`입니다. 실행 의존성은 빌드 환경에서 결정되므로
다른 배포판이나 릴리스와 호환된다는 보장은 없습니다.

버전은 [VERSION](VERSION)에서 읽으며 `PACKAGE_VERSION=1.2.3 make deb`로
덮어쓸 수 있습니다. 형식은 `X.Y.Z`이며 `0` 자체를 제외하고 앞에 0을
붙일 수 없습니다. `make package-test-rpm` 또는 `make package-test-deb`는
패키지 생성과 내용, 추출된 라이브러리 링크를 검사합니다. 두 테스트 모두
`readelf`가 필요하며 RPM 테스트에는 `rpm2cpio`와 `cpio`도 필요합니다.

GitHub Actions의 **Release packages**는 수동 실행하며 기본
브랜치에 있어야 합니다. 선택한 브랜치 또는 태그의 `VERSION`이
`vX.Y.Z` 태그를 결정합니다. 선택한 태그는 버전과 일치해야 하고 기존
릴리스 태그는 선택한 커밋을 가리켜야 합니다. 태그가 없으면 모든 빌드가
성공한 후 생성합니다. GitHub 릴리스에는 DEB(Ubuntu 24.04, x86-64),
RPM(Fedora 44, x86-64), Windows x64 DLL과 CHM이 첨부됩니다.
Windows SDK ZIP에는 DLL, 가져오기 라이브러리, 헤더, CHM, 라이선스도
포함됩니다. Windows 작업은 보관된 Microsoft HTML Help Workshop 설치 프로그램을
내려받아 SHA-256을 확인한 뒤 컴파일러를 추출합니다.
릴리스 노트에는 선택 커밋의 첫
번째 부모에서 도달 가능한 가장 가까운 버전 태그 이후의 커밋 제목을,
해당 태그가 없으면 전체 이력을 기록합니다. 기존 릴리스는 교체하지 않습니다.

프로젝트는 [MIT 라이선스](LICENSE)로 배포됩니다.

## 의존성

| 라이브러리 | 용도 | 제공 방식 | 라이선스 |
| --- | --- | --- | --- |
| [nlohmann/json 3.12.0](https://github.com/nlohmann/json) | JSON 읽기/쓰기, YAML 출력 | 포함, 헤더 전용 | [MIT](third_party/json/LICENSE.MIT) |
| [libyaml 0.2.5](https://github.com/yaml/libyaml) | YAML 읽기 | 포함, 공유 라이브러리에 컴파일 | [MIT](third_party/libyaml/License) |
| [toml11 4.4.0](https://github.com/ToruNiina/toml11) | TOML 읽기/쓰기 | 포함, 헤더 전용 | [MIT](third_party/toml11/LICENSE) |
| [GoogleTest](https://github.com/google/googletest) | 테스트 전용 | 별도 설치 필요 | [BSD 3-Clause](licenses/googletest-LICENSE) |

포함된 의존성의 자세한 내용은 [third_party/README.md](third_party/README.md)를
참조하십시오.

## 코드 스타일

C++ 코드는 Google C++ Style을 따릅니다. VS Code C/C++ 확장
(`ms-vscode.cpptools`)은 `.clang-format`에 따라 저장 시 서식을 지정합니다.
`make format`은 서식을 지정하고 `make style-check`는 검사합니다.
두 명령 모두 `clang-format`이 필요합니다.

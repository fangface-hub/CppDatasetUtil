# CppDatasetUtil

[English](README.md) | [日本語](README_ja.md) | Deutsch | [Français](README_fr.md) | [Italiano](README_it.md) | [한국어](README_ko.md) | [简体中文](README_zh.md)

`Dataset` für C++17 enthält null, boolesche Werte, 64-Bit-Ganzzahlen, double,
Zeichenketten, Maps und Arrays sowie Kommentare zu einzelnen Werten.

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

`Dataset::LoadDirectory("configs")` liefert eine nach Pfad sortierte
`std::list` von `(Pfad, Dataset)`-Paaren. Mit `true` als zweitem Argument
werden auch Unterverzeichnisse durchsucht; Standard ist `false`. Nicht
unterstützte Erweiterungen werden ignoriert. Symbolischen Links auf Dateien und
Verzeichnisse wird auch außerhalb des Startverzeichnisses gefolgt; ein Ziel
wird für jeden erreichbaren Pfad geladen. Defekte Links, Zyklen, unzugängliche
Einträge und nicht ladbare Dateien werden mit einer Warnung auf stderr
übersprungen. Ein ungültiges oder unzugängliches Startverzeichnis löst
`std::runtime_error` aus. Die Pfade behalten Verzeichnis- und Linknamen.

```cpp
for (const auto& [path, dataset] : Dataset::LoadDirectory("configs", true)) {
  // Use path and dataset.
}
```

## Dateiformate

`Load` und `Save` wählen CSV, JSON, YAML (`.yaml`, `.yml`) oder TOML anhand
der Erweiterung ohne Beachtung der Groß- und Kleinschreibung. Fehler lösen
`std::runtime_error` aus; ein fehlgeschlagenes Laden ändert das Dataset nicht.
Kommentare werden in YAML und TOML gespeichert, aber nicht geladen; bei
erfolgreichem Laden werden vorhandene Kommentare entfernt. CSV und JSON
speichern keine Kommentare.

Kommentare werden ohne führendes `#` gesetzt und mit Präfix `# ` vor dem Wert
ausgegeben; Zeilenumbrüche erzeugen mehrere Kommentarzeilen. Erforderlich ist
gültiges UTF-8 ohne Steuerzeichen außer Tabulator und Zeilenumbruch sowie ohne
Unicode-Zeilentrenner. TOML-Kommentare, die syntaktisch nicht platziert werden
können, etwa innerhalb von Inline-Tabellen, werden ausgelassen.

- CSV: Die erste Zeile enthält Spaltennamen, weitere Zeilen werden zu Maps mit
  Zeichenkettenwerten in einem Array. Doppelte Namen und unterschiedliche
  Feldzahlen sind Fehler. Beim Speichern müssen alle Maps nicht leer sein,
  dieselben Schlüssel und Zeichenkettenwerte haben; Spalten sind nach Schlüssel
  sortiert. Leere Dateien und Dateien nur mit Kopfzeile ergeben ein leeres
  Array; dieses wird als leere Datei gespeichert, ohne die Kopfzeile zu erhalten.
- JSON: null, bool, vorzeichenbehaftete 64-Bit-Ganzzahlen, endliche
  Gleitkommazahlen, Zeichenketten, Arrays und Objekte.
- YAML: dieselben Werte wie JSON und grundlegende YAML-1.2-Skalartypen. Schlüssel
  werden als skalare Zeichenketten behandelt. Mehrere Dokumente, doppelte
  Anker, rekursive Aliase und unbekannte Tags sind Fehler. Die Ausgabe verwendet
  Flow-Stil; ohne Kommentare ist sie JSON-kompatibel.
- TOML: Die Wurzel muss eine Tabelle sein. null beim Speichern, Datum/Uhrzeit
  beim Laden und nicht endliche Gleitkommazahlen sind nicht unterstützt.

`make` erstellt `build/libcppdatasetutil.so`, `make test` führt GoogleTest aus,
`make doc` erzeugt die Doxygen-HTML-Dokumentation einschließlich der Testliste.
Die API-man-Seiten werden in [docs/](docs/) gepflegt. Zum Kompilieren sind C-
und C++17-Compiler nötig; die HTML-Dokumentation benötigt Doxygen.

## Datengetriebene Tests

Unter `Tests/data/` gibt es ein Unterverzeichnis pro Test: `nested-values/`
enthält JSON- und TOML-Fälle für Maps, `array-values/` YAML- und JSON-Fälle
für Arrays. Jede Datei enthält ein `cases`-Array mit `input` und `expected`:

```json
{"cases":[{"input":{"host":"localhost","port":8080,"enabled":true},
           "expected":{"host":"localhost","port":8080,"enabled":true}}]}
```

Beispiel mit GoogleTest, ausgeführt im Projektverzeichnis:

```cpp
#include <gtest/gtest.h>
#include "Dataset.h"

TEST(Example, NestedValues) {
  const auto files = Dataset::LoadDirectory("Tests/data/nested-values");
  ASSERT_EQ(files.size(), 2u);
  for (const auto& [path, dataset] : files) {
    SCOPED_TRACE(path);
    const auto& cases = std::get<Dataset::Array>(dataset["cases"].value());
    ASSERT_FALSE(cases.empty());
    for (std::size_t i = 0; i < cases.size(); ++i) {
      SCOPED_TRACE(i);
      const Dataset& input = cases[i]["input"];
      const Dataset& expected = cases[i]["expected"];
      Dataset result;
      result["server"]["host"] = std::get<std::string>(input["host"].value());
      result["server"]["port"] = std::get<std::int64_t>(input["port"].value());
      result["enabled"] = std::get<bool>(input["enabled"].value());
      EXPECT_EQ(std::get<std::string>(result["server"]["host"].value()),
                std::get<std::string>(expected["host"].value()));
      EXPECT_EQ(std::get<std::int64_t>(result["server"]["port"].value()),
                std::get<std::int64_t>(expected["port"].value()));
      EXPECT_EQ(std::get<bool>(result["enabled"].value()),
                std::get<bool>(expected["enabled"].value()));
    }
  }
}
```

[Tests/DatasetTest.cpp](Tests/DatasetTest.cpp) zeigt weitere Beispiele.
Da `LoadDirectory` unlesbare Dateien überspringt, prüfen die Tests Warnungen
und leere Fälle; prüfen Sie auch die erwartete Dateianzahl. `make test` wird
im Projektverzeichnis ausgeführt.

## Linux-Pakete

Auf der Zieldistribution und -architektur bauen, damit ABI und Abhängigkeiten
passen:

```sh
make rpm   # rpmbuild, RPM-Werkzeuge, tar, C/C++-Werkzeuge
make deb   # dpkg-dev, dpkg-deb, make, C/C++-Werkzeuge
```

Die Pakete in `dist/` enthalten Bibliothek, Header, man-Seiten und Lizenzen.
Nach der Installation zeigt `man 3 CppDataSet` die API; bei passender UTF-8-Locale
wird die Übersetzung automatisch gewählt, z. B. mit
`man -L de_DE.UTF-8 3 CppDataSet`. Die separat gepflegten Übersetzungen liegen in
[docs/](docs/); Chinesisch ist für `zh_CN`. SONAME ist
`libcppdatasetutil.so.0`. Laufzeitabhängigkeiten stammen aus der Buildumgebung;
Pakete sind nicht unbedingt mit anderen Distributionen oder Releases kompatibel.

Die Version steht in [VERSION](VERSION); `PACKAGE_VERSION=1.2.3 make deb`
überschreibt sie. Sie muss `X.Y.Z` ohne führende Nullen außer bei `0` sein.
`make package-test-rpm` bzw. `make package-test-deb` baut und prüft das Paket
und das Linken gegen das extrahierte Paket. Beide Tests benötigen `readelf`,
der RPM-Test zusätzlich `rpm2cpio` und `cpio`.

Der GitHub-Actions-Workflow **Release Linux packages** wird manuell gestartet
und muss im Standardbranch liegen. Der gewählte Branch oder Tag bestimmt
`VERSION` und damit den Tag `vX.Y.Z`. Ein gewählter Tag muss zur Version
passen; ein vorhandener Release-Tag muss auf den gewählten Commit zeigen.
Ein fehlender Tag wird nach beiden erfolgreichen Builds erstellt. Der GitHub
Release enthält DEB (Ubuntu 24.04, x86-64) und RPM (Fedora 44, x86-64).
Die Release-Notizen enthalten Commit-Betreffzeilen seit dem nächsten vom
ersten Eltern-Commit erreichbaren Versionstag, andernfalls die gesamte Historie.
Ein vorhandener Release wird nicht ersetzt.

Das Projekt steht unter der [MIT-Lizenz](LICENSE).

## Abhängigkeiten

| Bibliothek | Zweck | Bereitstellung | Lizenz |
| --- | --- | --- | --- |
| [nlohmann/json 3.12.0](https://github.com/nlohmann/json) | JSON lesen/schreiben, YAML-Ausgabe | enthalten, Header-only | [MIT](third_party/json/LICENSE.MIT) |
| [libyaml 0.2.5](https://github.com/yaml/libyaml) | YAML lesen | enthalten, in Bibliothek kompiliert | [MIT](third_party/libyaml/License) |
| [toml11 4.4.0](https://github.com/ToruNiina/toml11) | TOML lesen/schreiben | enthalten, Header-only | [MIT](third_party/toml11/LICENSE) |
| [GoogleTest](https://github.com/google/googletest) | nur Tests | separat installieren | [BSD 3-Clause](licenses/googletest-LICENSE) |

Details zu enthaltenen Abhängigkeiten stehen in [third_party/README.md](third_party/README.md).

## Codestil

Der C++-Code folgt Google C++ Style. Die VS-Code-Erweiterung C/C++
(`ms-vscode.cpptools`) formatiert beim Speichern gemäß `.clang-format`.
`make format` formatiert, `make style-check` prüft; beide benötigen `clang-format`.

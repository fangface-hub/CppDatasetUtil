# CppDatasetUtil

[English](README.md) | [日本語](README_ja.md) | [Deutsch](README_de.md) | Français | [Italiano](README_it.md) | [한국어](README_ko.md) | [简体中文](README_zh.md)

En C++17, `Dataset` contient null, des booléens, des entiers 64 bits, des
double, chaînes, tables et tableaux, ainsi que des commentaires par valeur.

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

`Dataset::LoadDirectory("configs")` renvoie une `std::list` de paires
`(chemin, Dataset)` triées par chemin. Passer `true` en second argument inclut
les sous-répertoires ; la valeur par défaut est `false`. Les extensions non
prises en charge sont ignorées. Les liens symboliques vers fichiers et
répertoires sont suivis, même hors du répertoire initial ; une cible atteinte
par plusieurs chemins est chargée pour chacun. Liens rompus, cycles, entrées
inaccessibles et fichiers illisibles sont ignorés avec un avertissement sur
stderr. Un répertoire initial invalide ou inaccessible lève
`std::runtime_error`. Les chemins conservent les noms de répertoire et de lien.

```cpp
for (const auto& [path, dataset] : Dataset::LoadDirectory("configs", true)) {
  // Use path and dataset.
}
```

## Formats de fichiers

`Load` et `Save` choisissent CSV, JSON, YAML (`.yaml`, `.yml`) ou TOML selon
l'extension, sans distinction de casse. Les erreurs lèvent `std::runtime_error`
et un échec de chargement laisse le Dataset inchangé. Les commentaires sont
enregistrés en YAML et TOML, mais ne sont pas chargés : un chargement réussi
supprime les commentaires existants. CSV et JSON ne les enregistrent pas. Les chemins sont des `std::string` en UTF-8 sur toutes
les plateformes, Windows compris, et les noms de `LoadDirectory` aussi.

Indiquer les commentaires sans `#` initial ; ils sont écrits avant la valeur
avec le préfixe `# `. Les sauts de ligne produisent plusieurs lignes. UTF-8
valide est requis ; les caractères de contrôle autres que tabulation et saut
de ligne et les séparateurs de ligne Unicode provoquent une erreur. Les
commentaires TOML impossibles à placer, notamment dans les tables en ligne,
sont omis.

- CSV : la première ligne contient les noms de colonnes ; chaque ligne suivante
  devient une table de chaînes dans un tableau. Les noms répétés et nombres de
  champs différents sont des erreurs. Pour enregistrer, toutes les tables
  doivent être non vides et avoir les mêmes clés et des valeurs chaîne ; les
  colonnes sont triées par clé. Les fichiers vides ou ne contenant qu'un
  en-tête donnent un tableau vide, enregistré comme fichier vide sans conserver
  les noms de l'en-tête. Au chargement, un BOM UTF-8 initial et les lignes vides
  sont ignorés ; une valeur vide dans un fichier à une colonne est enregistrée
  sous la forme `""`.
- JSON : null, booléens, entiers signés 64 bits, flottants finis, chaînes,
  tableaux et objets.
- YAML : mêmes valeurs que JSON et types scalaires de base YAML 1.2 ; les clés
  sont des chaînes scalaires. Plusieurs documents, ancres répétées, alias
  récursifs, alias se développant en plus de 1 000 000 de valeurs au total et
  balises non prises en charge sont des erreurs. L'écriture utilise
  le style flow, avec une syntaxe compatible JSON en l'absence de commentaires.
- TOML : la racine doit être une table. L'enregistrement de null, le chargement
  de dates/heures et les flottants non finis ne sont pas pris en charge.

Les tables et tableaux imbriqués au-delà de `Dataset::kMaxNestingDepth` (256)
ne sont pas chargés sous forme d'arbre `Dataset`. Chacune de ces valeurs est
conservée comme chaîne contenant son texte JSON compact ; `Dataset::Parse(text)`
la convertit. Les ancres YAML ne peuvent pas être définies dans ces valeurs, un
TOML imbriqué deux fois plus profondément est rejeté et `Save` refuse les
valeurs imbriquées au-delà de la limite.

`make` construit `build/libcppdatasetutil.so`, `make test` exécute GoogleTest,
`make doc` produit la documentation HTML Doxygen avec la liste des tests. Les
pages man API sont maintenues dans [docs/](docs/). La compilation exige des
compilateurs C et C++11 ou ultérieur ; la documentation HTML exige Doxygen.

Sous C++11 et C++14, la bibliothèque utilise `mpark/variant` et `ghc::filesystem` fournis ; `Dataset::Value` est alors `cppdatasetutil::variant`. Lisez-le avec `cppdatasetutil::get`, `get_if` et `holds_alternative` (les fonctions `std::` à partir de C++17). Compilez la bibliothèque et ses utilisateurs au même niveau de langage (C++17 ou ultérieur, ou C++11/14) ; avec Make : `make CXX_STD=c++11`.

## DLL Windows et aide

Avec les outils de bureau C++ de Visual Studio 2022 et CMake, exécuter à la
racine du projet :

```powershell
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64
cmake --build build-windows --config Release
ctest --test-dir build-windows -C Release --output-on-failure
cmake --build build-windows --config Release --target windows_help
```

La DLL et la bibliothèque d'import sont `build-windows\Release\cppdatasetutil.dll`
et `cppdatasetutil.lib`. Inclure `Include\cppdatasetutil.h` et lier la
bibliothèque d'import avec la même architecture, un jeu d'outils MSVC
compatible et la même configuration d'exécution. À l'exécution, placer la DLL
à côté de l'exécutable ou dans le chemin de recherche des DLL de Windows.
`CPPDATASETUTIL_BUILD` sert uniquement à compiler la bibliothèque ; les
appelants ne doivent pas le définir. `Dataset` expose des membres de la
bibliothèque standard : la DLL et ses appelants doivent donc utiliser la même
variante de CRT (`/MD` ou `/MT`, Debug ou Release) et le même
`_ITERATOR_DEBUG_LEVEL`. En cas de divergence, l'édition de liens échoue avec
un symbole non résolu `CppDatasetUtilAbi1_*` au lieu de planter à l'exécution.
Le test lance un exécutable séparé lié à la DLL. La DLL Release exige aussi le
Microsoft Visual C++ Redistributable compatible sur la machine cible.

La cible `windows_help` utilise `hhc.exe` de Microsoft HTML Help Workshop pour
produire `build-windows\help\CppDatasetUtil.chm`. Choisir l'anglais, le
japonais, l'allemand, le français, l'italien, le coréen ou le chinois simplifié
sur la page de langue ; chaque sujet y renvoie. Si le compilateur est installé
ailleurs, configurer CMake avec `-DHTML_HELP_COMPILER="C:\path\to\hhc.exe"`.
Sans le Workshop, la DLL peut être compilée, mais la cible d'aide n'est pas
disponible. Les sources de l'aide sont dans [docs/windows/](docs/windows/) et
reposent sur les pages man correspondantes. Maintenir les deux jeux de
documentation synchronisés lors de toute modification de la référence API.

## Tests pilotés par les données

Les tests utilisent un sous-répertoire par élément de test sous `Tests/data/` :

```text
Tests/data/
  nested-values/   cas JSON et TOML pour l'accès aux tables
  array-values/    cas YAML et JSON pour l'accès aux tableaux
  server-config/   cas JSON et TOML pour le modèle d'exemple
```

Chaque fichier contient un tableau `cases`. Chaque cas a une valeur `input` et
une valeur `expected`. Les deux fichiers utilisés par l'exemple contiennent :

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

Exemple GoogleTest à exécuter depuis la racine du projet : `Dataset` lit les
cas ; le modèle indépendant `ServerConfig` est testé sur son adresse et son état :

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

[Tests/DatasetTest.cpp](Tests/DatasetTest.cpp) contient d'autres exemples.
`LoadDirectory` ignorant les fichiers illisibles, vérifiez le nombre
attendu ou les avertissements ; les tests vérifient les avertissements et les
cas vides. Exécuter `make test` à la racine du projet. Enregistrer l'exemple
ci-dessus séparément, le lier avec `cppdatasetutil` et `gtest_main`, puis
l'exécuter à la racine du projet.

## Paquets Linux

Construire sur la distribution et l'architecture cibles pour obtenir les
bonnes dépendances et la bonne ABI :

```sh
make rpm   # rpmbuild, outils RPM, tar, outils C/C++
make deb   # dpkg-dev, dpkg-deb, make, outils C/C++
```

Les paquets dans `dist/` contiennent la bibliothèque partagée, l'en-tête de
développement, les pages man de l'API en anglais, japonais, allemand, français,
italien, coréen et chinois simplifié, ainsi que les licences du projet et des
dépendances incluses. Après installation, `man 3 CppDataSet` affiche la référence API.
Une locale UTF-8 prise en charge sélectionne sa traduction automatiquement,
par exemple `man -L fr_FR.UTF-8 3 CppDataSet`. Les traductions sont maintenues
dans [docs/](docs/) ; le chinois cible `zh_CN`. Le SONAME est
`libcppdatasetutil.so.0`. Les dépendances d'exécution viennent de l'environnement
de compilation ; un paquet n'est pas forcément compatible avec une autre
distribution ou version.

La version provient de [VERSION](VERSION), ou de `PACKAGE_VERSION` si défini,
par exemple `PACKAGE_VERSION=1.2.3 make deb`. Elle doit être `X.Y.Z`, sans
zéro initial sauf pour `0`. `make package-test-rpm` ou `make package-test-deb`
construit, inspecte et teste l'édition de liens du paquet extrait. Les deux
tests exigent `readelf` ; le test RPM exige aussi `rpm2cpio` et `cpio`.

Le workflow GitHub Actions **Release packages** est lancé manuellement et
doit être présent sur la branche par défaut. La branche ou le tag choisi fournit
`VERSION`, qui détermine le tag `vX.Y.Z`. Un tag sélectionné doit correspondre
à cette version ; un tag de release existant doit pointer sur le commit choisi.
Un tag absent est créé après la réussite de toutes les compilations. La release
GitHub contient le DEB (Ubuntu 24.04, x86-64), le RPM (Fedora 44, x86-64), la
DLL et le CHM Windows x64. Un ZIP SDK Windows contient aussi la DLL, la
bibliothèque d'import, l'en-tête, le CHM et les licences.
Le job Windows télécharge l'installateur archivé de Microsoft HTML Help Workshop
et vérifie son SHA-256 avant d'extraire le compilateur. Les notes
contiennent les sujets des commits depuis le tag de version le plus proche
accessible depuis le premier parent, ou tout l'historique s'il n'y en a pas.
Une release existante n'est pas remplacée.

Le projet est distribué sous [licence MIT](LICENSE).

## Dépendances

| Bibliothèque | Usage | Disponibilité | Licence |
| --- | --- | --- | --- |
| [nlohmann/json 3.12.0](https://github.com/nlohmann/json) | lecture/écriture JSON, sortie YAML | incluse, en-têtes seuls | [MIT](third_party/json/LICENSE.MIT) |
| [libyaml 0.2.5](https://github.com/yaml/libyaml) | lecture YAML | incluse, compilée dans la bibliothèque | [MIT](third_party/libyaml/License) |
| [toml11 4.4.0](https://github.com/ToruNiina/toml11) | lecture/écriture TOML | incluse, en-têtes seuls | [MIT](third_party/toml11/LICENSE) |
| [GoogleTest](https://github.com/google/googletest) | tests uniquement | installation séparée | [BSD 3-Clause](licenses/googletest-LICENSE) |

Voir [third_party/README.md](third_party/README.md) pour les dépendances incluses.

## Style de code

Le code C++ suit Google C++ Style. L'extension VS Code C/C++
(`ms-vscode.cpptools`) formate à l'enregistrement selon `.clang-format`.
`make format` formate et `make style-check` vérifie ; les deux exigent `clang-format`.

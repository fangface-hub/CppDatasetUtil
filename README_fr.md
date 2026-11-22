# CppDatasetUtil

[English](README.md) | [日本語](README_ja.md) | [Deutsch](README_de.md) | Français | [Italiano](README_it.md) | [한국어](README_ko.md) | [简体中文](README_zh.md)

En C++17, `Dataset` contient null, des booléens, des entiers 64 bits, des
double, chaînes, tables et tableaux, ainsi que des commentaires par valeur.

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
supprime les commentaires existants. CSV et JSON ne les enregistrent pas.

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
  les noms de l'en-tête.
- JSON : null, booléens, entiers signés 64 bits, flottants finis, chaînes,
  tableaux et objets.
- YAML : mêmes valeurs que JSON et types scalaires de base YAML 1.2 ; les clés
  sont des chaînes scalaires. Plusieurs documents, ancres répétées, alias
  récursifs et balises non prises en charge sont des erreurs. L'écriture utilise
  le style flow, avec une syntaxe compatible JSON en l'absence de commentaires.
- TOML : la racine doit être une table. L'enregistrement de null, le chargement
  de dates/heures et les flottants non finis ne sont pas pris en charge.

`make` construit `build/libcppdatasetutil.so`, `make test` exécute GoogleTest,
`make doc` produit la documentation HTML Doxygen avec la liste des tests. Les
pages man API sont maintenues dans [docs/](docs/). La compilation exige des
compilateurs C et C++17 ; la documentation HTML exige Doxygen.

## Tests pilotés par les données

Sous `Tests/data/`, chaque test a son répertoire : `nested-values/` contient
des cas JSON et TOML pour les tables, `array-values/` des cas YAML et JSON pour
les tableaux. Chaque fichier contient un tableau `cases` avec `input` et
`expected` :

```json
{"cases":[{"input":{"host":"localhost","port":8080,"enabled":true},
           "expected":{"host":"localhost","port":8080,"enabled":true}}]}
```

Exemple GoogleTest à exécuter depuis la racine du projet :

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

[Tests/DatasetTest.cpp](Tests/DatasetTest.cpp) contient d'autres exemples.
`LoadDirectory` ignorant les fichiers illisibles, vérifiez le nombre
attendu ou les avertissements ; les tests vérifient les avertissements et les
cas vides. Exécuter `make test` à la racine du projet.

## Paquets Linux

Construire sur la distribution et l'architecture cibles pour obtenir les
bonnes dépendances et la bonne ABI :

```sh
make rpm   # rpmbuild, outils RPM, tar, outils C/C++
make deb   # dpkg-dev, dpkg-deb, make, outils C/C++
```

Les paquets dans `dist/` contiennent la bibliothèque, l'en-tête, les pages man
et les licences. Après installation, `man 3 CppDataSet` affiche la référence API.
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

Le workflow GitHub Actions **Release Linux packages** est lancé manuellement et
doit être présent sur la branche par défaut. La branche ou le tag choisi fournit
`VERSION`, qui détermine le tag `vX.Y.Z`. Un tag sélectionné doit correspondre
à cette version ; un tag de release existant doit pointer sur le commit choisi.
Un tag absent est créé après les deux compilations réussies. La release GitHub
contient le DEB (Ubuntu 24.04, x86-64) et le RPM (Fedora 44, x86-64). Les notes
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

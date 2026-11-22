# CppDatasetUtil

[English](README.md) | [日本語](README_ja.md) | [Deutsch](README_de.md) | [Français](README_fr.md) | Italiano | [한국어](README_ko.md) | [简体中文](README_zh.md)

In C++17, `Dataset` contiene null, booleani, interi a 64 bit, double, stringhe,
mappe e array, oltre ai commenti associati ai singoli valori.

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

`Dataset::LoadDirectory("configs")` restituisce una `std::list` di coppie
`(percorso, Dataset)` ordinate per percorso. Passare `true` come secondo
argomento include le sottodirectory; il valore predefinito è `false`. Le
estensioni non supportate sono ignorate. I link simbolici a file e directory
sono seguiti anche fuori dalla directory iniziale; una destinazione raggiunta
da più percorsi viene caricata per ciascuno. Link interrotti, cicli, voci
inaccessibili e file non caricabili sono saltati con un avviso su stderr.
Una directory iniziale non valida o inaccessibile genera `std::runtime_error`.
I percorsi mantengono i nomi delle directory e dei link.

```cpp
for (const auto& [path, dataset] : Dataset::LoadDirectory("configs", true)) {
  // Use path and dataset.
}
```

## Formati dei file

`Load` e `Save` selezionano CSV, JSON, YAML (`.yaml`, `.yml`) o TOML in base
all'estensione, senza distinzione tra maiuscole e minuscole. Gli errori generano
`std::runtime_error`; un caricamento fallito lascia invariato il Dataset.
I commenti sono salvati in YAML e TOML, ma non caricati: un caricamento
riuscito elimina i commenti esistenti. CSV e JSON non salvano commenti.

Specificare i commenti senza `#` iniziale; sono scritti prima del valore con
prefisso `# `. Gli a capo producono più righe. È richiesto UTF-8 valido; i
caratteri di controllo diversi da tabulazione e a capo e i separatori di riga
Unicode causano un errore. I commenti TOML non collocabili, ad esempio nelle
tabelle inline, vengono omessi.

- CSV: la prima riga contiene i nomi delle colonne; le successive diventano
  mappe di stringhe in un array. Nomi duplicati e numero di campi diverso sono
  errori. Per salvare, tutte le mappe devono essere non vuote, con le stesse
  chiavi e valori stringa; le colonne sono ordinate per chiave. File vuoti o
  con sola intestazione diventano array vuoti, salvati come file vuoti senza
  conservare i nomi dell'intestazione.
- JSON: null, booleani, interi con segno a 64 bit, numeri finiti in virgola
  mobile, stringhe, array e oggetti.
- YAML: gli stessi valori JSON e i tipi scalari di base YAML 1.2; le chiavi
  sono stringhe scalari. Documenti multipli, ancore duplicate, alias ricorsivi,
  alias che si espandono in più di 1.000.000 di valori in totale e tag non
  supportati sono errori. L'output usa lo stile flow, compatibile
  con JSON in assenza di commenti.
- TOML: la radice deve essere una tabella. Non sono supportati il salvataggio
  di null, il caricamento di date/ore e i numeri non finiti.

Le mappe e gli array annidati oltre `Dataset::kMaxNestingDepth` (256) non sono
caricati come alberi `Dataset`. Ciascuno di questi valori è conservato come
stringa con il relativo testo JSON compatto; `Dataset::Parse(text)` lo converte.
Le ancore YAML non possono essere definite in questi valori, un TOML annidato
oltre il doppio del limite è rifiutato e `Save` rifiuta i valori annidati oltre
il limite.

`make` compila `build/libcppdatasetutil.so`, `make test` esegue GoogleTest,
`make doc` genera la documentazione HTML Doxygen con la lista dei test.
Le pagine man API sono gestite in [docs/](docs/). La compilazione richiede
compilatori C e C++17; la documentazione HTML richiede Doxygen.

## Test basati sui dati

In `Tests/data/` ogni test ha una sottodirectory: `nested-values/` contiene
casi JSON e TOML per le mappe, `array-values/` casi YAML e JSON per gli array.
`server-config/` contiene i casi del modello. Ogni file contiene un array
`cases` con `input` ed `expected`. I due file dell'esempio:

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

Esempio GoogleTest dalla radice del progetto: `Dataset` legge i casi; il modello
indipendente `ServerConfig` viene verificato per endpoint e stato:

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

[Tests/DatasetTest.cpp](Tests/DatasetTest.cpp) contiene altri esempi.
Poiché `LoadDirectory` salta i file illeggibili, verificare il numero atteso o
gli avvisi; i test controllano avvisi e casi vuoti. Eseguire `make test` dalla
radice del progetto. Salvare l'esempio separatamente, collegarlo con
`cppdatasetutil` e `gtest_main` ed eseguirlo dalla radice del progetto.

## Pacchetti Linux

Compilare sulla distribuzione e architettura di destinazione per ottenere
dipendenze e ABI corrette:

```sh
make rpm   # rpmbuild, strumenti RPM, tar, strumenti C/C++
make deb   # dpkg-dev, dpkg-deb, make, strumenti C/C++
```

I pacchetti in `dist/` contengono libreria, header, pagine man e licenze.
Dopo l'installazione, `man 3 CppDataSet` mostra il riferimento API. Con una locale
UTF-8 supportata viene scelta automaticamente la traduzione, ad esempio con
`man -L it_IT.UTF-8 3 CppDataSet`. Le traduzioni sono gestite in [docs/](docs/);
il cinese è per `zh_CN`. Il SONAME è `libcppdatasetutil.so.0`. Le dipendenze
runtime derivano dall'ambiente di compilazione; un pacchetto non è
necessariamente compatibile con un'altra distribuzione o versione.

La versione proviene da [VERSION](VERSION); `PACKAGE_VERSION=1.2.3 make deb`
la sostituisce. Deve essere `X.Y.Z`, senza zeri iniziali salvo `0`.
`make package-test-rpm` o `make package-test-deb` compila e verifica il
pacchetto e il collegamento al pacchetto estratto. Entrambi richiedono
`readelf`; il test RPM richiede anche `rpm2cpio` e `cpio`.

Il workflow GitHub Actions **Release packages** viene avviato manualmente
e deve trovarsi nel branch predefinito. Il branch o tag scelto fornisce
`VERSION`, che determina il tag `vX.Y.Z`. Il tag selezionato deve corrispondere
alla versione; un tag di release esistente deve puntare al commit scelto. Se
manca, viene creato dopo la riuscita di tutte le compilazioni. La release GitHub
contiene DEB (Ubuntu 24.04, x86-64), RPM (Fedora 44, x86-64), DLL e CHM
Windows x64. Uno ZIP SDK Windows include anche libreria di importazione, header
e licenze. Le note
contengono gli oggetti dei commit dal tag di versione più vicino raggiungibile
dal primo genitore, oppure tutta la cronologia se non esiste. Una release
esistente non viene sostituita.

Il progetto è distribuito con [licenza MIT](LICENSE).

## Dipendenze

| Libreria | Uso | Disponibilità | Licenza |
| --- | --- | --- | --- |
| [nlohmann/json 3.12.0](https://github.com/nlohmann/json) | lettura/scrittura JSON, output YAML | inclusa, solo header | [MIT](third_party/json/LICENSE.MIT) |
| [libyaml 0.2.5](https://github.com/yaml/libyaml) | lettura YAML | inclusa, compilata nella libreria | [MIT](third_party/libyaml/License) |
| [toml11 4.4.0](https://github.com/ToruNiina/toml11) | lettura/scrittura TOML | inclusa, solo header | [MIT](third_party/toml11/LICENSE) |
| [GoogleTest](https://github.com/google/googletest) | solo test | installazione separata | [BSD 3-Clause](licenses/googletest-LICENSE) |

Per le dipendenze incluse vedere [third_party/README.md](third_party/README.md).

## Stile del codice

Il codice C++ segue Google C++ Style. L'estensione C/C++ di VS Code
(`ms-vscode.cpptools`) formatta al salvataggio secondo `.clang-format`.
`make format` formatta e `make style-check` controlla; entrambi richiedono `clang-format`.

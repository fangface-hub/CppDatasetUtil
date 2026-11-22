#include "cppdatasetutil.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

int main() {
  namespace fs = std::filesystem;
  const auto name = "cppdatasetutil-consumer-" +
                    std::to_string(std::chrono::steady_clock::now()
                                       .time_since_epoch()
                                       .count());
  const auto directory = fs::temp_directory_path() / name;
  bool created = false;
  try {
    if (!fs::create_directory(directory))
      throw std::runtime_error("Cannot create test directory");
    created = true;
    Dataset data;
    data["server"]["host"] = "localhost";
    data["server"]["port"] = 8080;
    data["server"]["host"].set_comments({"address"});
    for (const auto* extension : {"json", "yaml", "toml"}) {
      const auto path =
          (directory / (std::string("config.") + extension)).string();
      data.Save(path);
      Dataset loaded;
      loaded.Load(path);
      if (std::get<std::string>(loaded["server"]["host"].value()) !=
              "localhost" ||
          std::get<std::int64_t>(loaded["server"]["port"].value()) != 8080)
        throw std::runtime_error("Round-trip failed");
    }
    Dataset csv = Dataset::Array{Dataset::Map{{"name", "item"}}};
    csv.Save((directory / "config.csv").string());
    Dataset loaded_csv;
    loaded_csv.Load((directory / "config.csv").string());
    if (std::get<std::string>(loaded_csv[std::size_t{0}]["name"].value()) !=
        "item")
      throw std::runtime_error("CSV round-trip failed");
    if (Dataset::LoadDirectory(directory.string()).size() != 4)
      throw std::runtime_error("Directory loading failed");
    bool threw = false;
    try {
      data.Load((directory / "missing.json").string());
    } catch (const std::runtime_error&) {
      threw = true;
    }
    if (!threw || std::get<std::string>(data["server"]["host"].value()) !=
                      "localhost")
      throw std::runtime_error("Load failure did not preserve the value");
    fs::remove_all(directory);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    std::error_code cleanup_error;
    if (created) fs::remove_all(directory, cleanup_error);
    if (cleanup_error) std::cerr << cleanup_error.message() << '\n';
    return 1;
  }
}

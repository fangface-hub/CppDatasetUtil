# Dependencies

The source distribution vendors libyaml 0.2.5 (MIT), toml11 4.4.0 (MIT),
and nlohmann/json 3.12.0 (MIT). Their original license files are in the
respective directories. libyaml is compiled into the shared library; toml11
and nlohmann/json are header-only.

For C++11 and C++14 the library also bundles mpark/variant (BSL-1.0, in
`Include/cppdatasetutil/mpark`, license in `third_party/mpark_variant`) and
ghc::filesystem 1.5.x (MIT, `third_party/ghc_filesystem`). Both are header-only;
mpark/variant is installed with the public header.

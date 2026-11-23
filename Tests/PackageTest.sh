#!/bin/sh
set -eu

cd "$(dirname "$0")/.."
source_dir=$(pwd)
format=${1:-}
if [ "$format" != deb ] && [ "$format" != rpm ]; then
  echo "Usage: sh Tests/PackageTest.sh {deb|rpm}" >&2
  exit 1
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/cppdatasetutil-package-test.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
project=$work/project
root=$work/extracted
mkdir -p "$project" "$root"
cp -R Makefile VERSION Include Source docs third_party packaging LICENSE \
  README.md README_ja.md README_de.md README_fr.md README_it.md README_ko.md README_zh.md "$project/"

unset PACKAGE_VERSION
rm "$project/VERSION"
if make -C "$project" "$format"; then
  echo "Error: missing VERSION was accepted" >&2
  exit 1
fi
printf 'invalid\n' > "$project/VERSION"
if make -C "$project" "$format"; then
  echo "Error: invalid VERSION was accepted" >&2
  exit 1
fi
printf '2.3.4\ninvalid\n' > "$project/VERSION"
if make -C "$project" "$format"; then
  echo "Error: multiline VERSION was accepted" >&2
  exit 1
fi

if PACKAGE_VERSION=invalid make -C "$project" "$format"; then
  echo "Error: invalid package version was accepted" >&2
  exit 1
fi
printf '2.3.4\n' > "$project/VERSION"
make -C "$project" "$format"

if [ "$format" = deb ]; then
  arch=$(dpkg --print-architecture)
  multiarch=$(dpkg-architecture -qDEB_HOST_MULTIARCH)
  package=$project/dist/cppdatasetutil_2.3.4_${arch}.deb
  [ "$(dpkg-deb -f "$package" Version)" = 2.3.4 ]
  PACKAGE_VERSION=3.4.5 make -C "$project" "$format"
  [ "$(dpkg-deb -f "$project/dist/cppdatasetutil_3.4.5_${arch}.deb" Version)" = 3.4.5 ]
  [ "$(dpkg-deb -f "$package" Architecture)" = "$arch" ]
  dpkg-deb -f "$package" Depends | grep -q 'libc6'
  dpkg-deb -f "$package" Depends | grep -q 'libstdc++6'
  dpkg-deb -x "$package" "$root"
  libdir=/usr/lib/$multiarch
  test -s "$root/usr/share/doc/cppdatasetutil/copyright"
else
  package=$(find "$project/dist" -type f -name 'cppdatasetutil-2.3.4-*.rpm' -print -quit)
  [ -n "$package" ]
  [ "$(rpm -qp --qf '%{VERSION}' "$package")" = 2.3.4 ]
  PACKAGE_VERSION=3.4.5 make -C "$project" "$format"
  override_package=$(find "$project/dist" -type f -name 'cppdatasetutil-3.4.5-*.rpm' -print -quit)
  [ -n "$override_package" ]
  [ "$(rpm -qp --qf '%{VERSION}' "$override_package")" = 3.4.5 ]
  [ "$(rpm -qp --qf '%{ARCH}' "$package")" = "$(rpm --eval '%{_arch}')" ]
  rpm -qp --requires "$package" | grep -q 'libc.so.6'
  rpm -qp --requires "$package" | grep -q 'libstdc++.so.6'
  rpm -qp --provides "$package" | grep -q 'libcppdatasetutil.so.0'
  rpm2cpio "$package" > "$work/package.cpio"
  (cd "$root" && cpio -id --quiet < "$work/package.cpio")
  libdir=$(rpm --eval '%{_libdir}')
fi

cmp "$source_dir/Include/cppdatasetutil.h" "$root/usr/include/cppdatasetutil.h"
cmp "$source_dir/LICENSE" "$root/usr/share/licenses/cppdatasetutil/LICENSE"
cmp "$source_dir/third_party/json/LICENSE.MIT" "$root/usr/share/licenses/cppdatasetutil/json-LICENSE"
cmp "$source_dir/third_party/libyaml/License" "$root/usr/share/licenses/cppdatasetutil/libyaml-LICENSE"
cmp "$source_dir/third_party/toml11/LICENSE" "$root/usr/share/licenses/cppdatasetutil/toml11-LICENSE"
cmp "$source_dir/third_party/mpark_variant/LICENSE" "$root/usr/share/licenses/cppdatasetutil/mpark-variant-LICENSE"
cmp "$source_dir/third_party/ghc_filesystem/LICENSE" "$root/usr/share/licenses/cppdatasetutil/ghc-filesystem-LICENSE"
cmp "$source_dir/Include/cppdatasetutil/mpark/variant.hpp" "$root/usr/include/cppdatasetutil/mpark/variant.hpp"
if [ "$format" = rpm ]; then
  mandir=$(rpm --eval '%{_mandir}')
else
  mandir=/usr/share/man
fi
if [ -f "$root$mandir/man3/CppDataSet.3.gz" ]; then
  gzip -cd "$root$mandir/man3/CppDataSet.3.gz" > "$work/CppDataSet.3"
else
  cp "$root$mandir/man3/CppDataSet.3" "$work/CppDataSet.3"
fi
cmp "$source_dir/docs/man3/CppDataSet.3" "$work/CppDataSet.3"
test ! -e "$root$mandir/man3/Dataset.3"
test ! -e "$root$mandir/man3/Dataset.3.gz"
for lang in ja de fr it ko zh_CN; do
  if [ -f "$root$mandir/$lang/man3/CppDataSet.3.gz" ]; then
    gzip -cd "$root$mandir/$lang/man3/CppDataSet.3.gz" > "$work/CppDataSet-localized.3"
  else
    cp "$root$mandir/$lang/man3/CppDataSet.3" "$work/CppDataSet-localized.3"
  fi
  cmp "$source_dir/docs/$lang/man3/CppDataSet.3" "$work/CppDataSet-localized.3"
  test ! -e "$root$mandir/$lang/man3/Dataset.3"
  test ! -e "$root$mandir/$lang/man3/Dataset.3.gz"
done
[ "$(readlink "$root$libdir/libcppdatasetutil.so")" = libcppdatasetutil.so.0 ]
readelf -d "$root$libdir/libcppdatasetutil.so.0" | grep -q 'SONAME.*libcppdatasetutil.so.0'

cat > "$work/consumer.cpp" <<'EOF'
#include <cstdint>
#include "cppdatasetutil.h"
int main(int argc, char** argv) {
  if (argc != 2) return 1;
  Dataset data;
  data["answer"] = 42;
  data.Save(argv[1]);
  Dataset loaded;
  loaded.Load(argv[1]);
  return std::get<std::int64_t>(loaded["answer"].value()) == 42 ? 0 : 1;
}
EOF
${CXX:-c++} -std=c++17 -I"$root/usr/include" "$work/consumer.cpp" \
  -L"$root$libdir" -lcppdatasetutil -o "$work/consumer"
LD_LIBRARY_PATH="$root$libdir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  "$work/consumer" "$work/consumer.yaml"
echo "$format package test passed"

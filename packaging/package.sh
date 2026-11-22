#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

if [ "$#" -ne 1 ] || { [ "$1" != deb ] && [ "$1" != rpm ]; }; then
  echo "Usage: sh packaging/package.sh {deb|rpm}" >&2
  exit 1
fi

if [ -n "${PACKAGE_VERSION:-}" ]; then
  version=$PACKAGE_VERSION
else
  if ! version=$(cat VERSION); then
    echo "Error: unable to read VERSION" >&2
    exit 1
  fi
fi
case "$version" in
  *'
'*) echo "Error: package version must be a single line" >&2; exit 1 ;;
esac
if ! printf '%s\n' "$version" | grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'; then
  echo "Error: package version must be X.Y.Z (got '$version')" >&2
  exit 1
fi

if [ "$1" = deb ]; then
  tools="dpkg-deb dpkg-architecture dpkg-shlibdeps"
else
  tools="rpmbuild"
fi
for tool in $tools; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Error: $tool is required to build the $1 package" >&2
    exit 1
  fi
done

mkdir -p dist
dist=$(cd dist && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cppdatasetutil-package.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT HUP INT TERM

if [ "$1" = deb ]; then
  root=$work/deb
  project=$work/project
  mkdir -p "$project" "$root/DEBIAN"
  cp -R Makefile VERSION Include Source docs third_party LICENSE "$project/"
  multiarch=$(dpkg-architecture -qDEB_HOST_MULTIARCH)
  arch=$(dpkg --print-architecture)
  if [ -z "$multiarch" ] || [ -z "$arch" ]; then
    echo "Error: unable to determine Debian architecture" >&2
    exit 1
  fi
  make -C "$project" all
  make -C "$project" install DESTDIR="$root" LIBDIR="/usr/lib/$multiarch"
  mkdir -p "$project/debian" "$root/usr/share/doc/cppdatasetutil"
  cat > "$project/debian/control" <<EOF
Source: cppdatasetutil
Section: libs
Priority: optional
Maintainer: CppDatasetUtil contributors <noreply@github.com>

Package: cppdatasetutil
Architecture: any
Description: C++ dataset serialization library
EOF
  deps=$(cd "$project" && dpkg-shlibdeps -O -e"$root/usr/lib/$multiarch/libcppdatasetutil.so.0")
  case "$deps" in
    shlibs:Depends=*) depends=${deps#shlibs:Depends=} ;;
    *) echo "Error: unable to determine Debian runtime dependencies" >&2; exit 1 ;;
  esac
  if [ -z "$depends" ]; then
    echo "Error: Debian runtime dependency list is empty" >&2
    exit 1
  fi
  cat "$project/LICENSE" "$project/third_party/json/LICENSE.MIT" \
    "$project/third_party/libyaml/License" "$project/third_party/toml11/LICENSE" \
    > "$root/usr/share/doc/cppdatasetutil/copyright"
  cat > "$root/DEBIAN/control" <<EOF
Package: cppdatasetutil
Version: $version
Section: libs
Priority: optional
Architecture: $arch
Maintainer: CppDatasetUtil contributors <noreply@github.com>
Depends: $depends
Description: C++ dataset serialization library
 C++17 library for nested data with CSV, JSON, YAML and TOML serialization.
 Includes the shared library, development header, and man page.
EOF
  dpkg-deb --build --root-owner-group "$root" \
    "$dist/cppdatasetutil_${version}_${arch}.deb"
else
  topdir=$work/rpmbuild
  mkdir -p "$topdir/BUILD" "$topdir/BUILDROOT" "$topdir/RPMS" \
    "$topdir/SOURCES" "$topdir/SPECS" "$topdir/SRPMS"
  tar -czf "$topdir/SOURCES/cppdatasetutil-${version}.tar.gz" \
    --transform "s,^,cppdatasetutil-${version}/," \
    Makefile VERSION Include Source docs third_party LICENSE \
    README.md README_ja.md README_de.md README_fr.md README_it.md README_ko.md README_zh.md
  rpmbuild -bb packaging/cppdatasetutil.spec \
    --define "_topdir $topdir" --define "package_version $version"
  if ! find "$topdir/RPMS" -type f -name '*.rpm' -print -quit | grep -q .; then
    echo "Error: rpmbuild did not produce an RPM" >&2
    exit 1
  fi
  find "$topdir/RPMS" -type f -name '*.rpm' -exec cp -t "$dist" -- {} +
fi

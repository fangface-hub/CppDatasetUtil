CXX ?= g++
CC ?= cc
CXXFLAGS ?= -O2 -g
CFLAGS ?= -O2 -g
VARIANT ?=
ifeq ($(VARIANT),cxx11)
CXX_STD ?= c++11
PKG := cppdatasetutil-cxx11
LIBNAME := cppdatasetutil_cxx11
else ifeq ($(VARIANT),)
CXX_STD ?= c++17
PKG := cppdatasetutil
LIBNAME := cppdatasetutil
else
$(error VARIANT must be empty or cxx11)
endif
CPPFLAGS += -IInclude -Ithird_party/ghc_filesystem -Ithird_party/json -Ithird_party/toml11 -Ithird_party/libyaml/include
CLANG_FORMAT ?= clang-format
PREFIX ?= /usr
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
DATADIR ?= $(PREFIX)/share
MANDIR ?= $(DATADIR)/man
MAN_LANGS := ja de fr it ko zh_CN
STYLE_FILES := $(wildcard Include/*.h Source/*.cpp Tests/*.cpp)
LIBRARY := build/lib$(LIBNAME).so
LIBRARY_REAL := $(LIBRARY).0
TEST := build/DatasetTest
YAML_SOURCES := $(wildcard third_party/libyaml/src/*.c)
YAML_OBJECTS := $(patsubst third_party/libyaml/src/%.c,build/yaml-%.o,$(YAML_SOURCES))

.PHONY: all test doc clean format style-check install deb rpm deb-cxx11 rpm-cxx11 release-package package-test-deb package-test-rpm

all: $(LIBRARY)

build:
	mkdir -p $@

build/yaml-%.o: third_party/libyaml/src/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -fvisibility=hidden -DYAML_VERSION_MAJOR=0 \
		-DYAML_VERSION_MINOR=2 -DYAML_VERSION_PATCH=5 -DYAML_VERSION_STRING=\"0.2.5\" \
		-c $< -o $@

$(LIBRARY_REAL): Source/Dataset.cpp Source/Formats.cpp Include/cppdatasetutil.h $(YAML_OBJECTS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=$(CXX_STD) -Wall -Wextra -fPIC -shared \
		-Wl,-soname,lib$(LIBNAME).so.0 \
		Source/Dataset.cpp Source/Formats.cpp $(YAML_OBJECTS) $(LDFLAGS) -o $@

$(LIBRARY): $(LIBRARY_REAL)
	ln -sfn lib$(LIBNAME).so.0 $@

$(TEST): Tests/DatasetTest.cpp $(LIBRARY) Include/cppdatasetutil.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=$(CXX_STD) -Wall -Wextra $< -Lbuild \
		-Wl,-rpath,'$$ORIGIN' -l$(LIBNAME) -lgtest_main -lgtest -pthread -o $@

test: $(TEST)
	./$(TEST)

ifeq ($(VARIANT),cxx11)
HEADERDIR := $(INCLUDEDIR)/$(PKG)
else
HEADERDIR := $(INCLUDEDIR)
endif
LICENSEDIR := $(DATADIR)/licenses/$(PKG)

install: $(LIBRARY)
	install -d "$(DESTDIR)$(LIBDIR)" "$(DESTDIR)$(HEADERDIR)" "$(DESTDIR)$(LICENSEDIR)"
	install -m 755 $(LIBRARY_REAL) "$(DESTDIR)$(LIBDIR)/"
	ln -sfn lib$(LIBNAME).so.0 "$(DESTDIR)$(LIBDIR)/lib$(LIBNAME).so"
	install -m 644 Include/cppdatasetutil.h "$(DESTDIR)$(HEADERDIR)/"
	install -d "$(DESTDIR)$(HEADERDIR)/cppdatasetutil/mpark"
	install -m 644 Include/cppdatasetutil/mpark/*.hpp "$(DESTDIR)$(HEADERDIR)/cppdatasetutil/mpark/"
	install -m 644 third_party/mpark_variant/LICENSE "$(DESTDIR)$(LICENSEDIR)/mpark-variant-LICENSE"
	install -m 644 third_party/ghc_filesystem/LICENSE "$(DESTDIR)$(LICENSEDIR)/ghc-filesystem-LICENSE"
	install -m 644 LICENSE "$(DESTDIR)$(LICENSEDIR)/LICENSE"
	install -m 644 third_party/json/LICENSE.MIT "$(DESTDIR)$(LICENSEDIR)/json-LICENSE"
	install -m 644 third_party/libyaml/License "$(DESTDIR)$(LICENSEDIR)/libyaml-LICENSE"
	install -m 644 third_party/toml11/LICENSE "$(DESTDIR)$(LICENSEDIR)/toml11-LICENSE"
ifeq ($(VARIANT),)
	install -d "$(DESTDIR)$(MANDIR)/man3"
	install -m 644 docs/man3/CppDataSet.3 "$(DESTDIR)$(MANDIR)/man3/"
	@set -e; for lang in $(MAN_LANGS); do \
		install -d "$(DESTDIR)$(MANDIR)/$$lang/man3"; \
		install -m 644 "docs/$$lang/man3/CppDataSet.3" "$(DESTDIR)$(MANDIR)/$$lang/man3/"; \
	done
endif

deb:
	sh packaging/package.sh deb

rpm:
	sh packaging/package.sh rpm

deb-cxx11:
	sh packaging/package.sh deb cxx11

rpm-cxx11:
	sh packaging/package.sh rpm cxx11

release-package: deb rpm deb-cxx11 rpm-cxx11

package-test-deb:
	sh Tests/PackageTest.sh deb

package-test-rpm:
	sh Tests/PackageTest.sh rpm

doc:
	doxygen Doxyfile

format:
	$(CLANG_FORMAT) -i $(STYLE_FILES)

style-check:
	$(CLANG_FORMAT) --dry-run --Werror $(STYLE_FILES)

clean:
	rm -f $(LIBRARY) $(LIBRARY_REAL) $(TEST) $(YAML_OBJECTS)

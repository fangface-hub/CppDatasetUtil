CXX ?= g++
CC ?= cc
CXXFLAGS ?= -O2 -g
CFLAGS ?= -O2 -g
CPPFLAGS += -IInclude -Ithird_party/json -Ithird_party/toml11 -Ithird_party/libyaml/include
CLANG_FORMAT ?= clang-format
PREFIX ?= /usr
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
DATADIR ?= $(PREFIX)/share
MANDIR ?= $(DATADIR)/man
MAN_LANGS := ja de fr it ko zh_CN
STYLE_FILES := $(wildcard Include/*.h Source/*.cpp Tests/*.cpp)
LIBRARY := build/libcppdatasetutil.so
LIBRARY_REAL := $(LIBRARY).0
TEST := build/DatasetTest
YAML_SOURCES := $(wildcard third_party/libyaml/src/*.c)
YAML_OBJECTS := $(patsubst third_party/libyaml/src/%.c,build/yaml-%.o,$(YAML_SOURCES))

.PHONY: all test doc clean format style-check install deb rpm release-package package-test-deb package-test-rpm

all: $(LIBRARY)

build:
	mkdir -p $@

build/yaml-%.o: third_party/libyaml/src/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -fvisibility=hidden -DYAML_VERSION_MAJOR=0 \
		-DYAML_VERSION_MINOR=2 -DYAML_VERSION_PATCH=5 -DYAML_VERSION_STRING=\"0.2.5\" \
		-c $< -o $@

$(LIBRARY_REAL): Source/Dataset.cpp Source/Formats.cpp Include/Dataset.h $(YAML_OBJECTS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 -Wall -Wextra -fPIC -shared \
		-Wl,-soname,libcppdatasetutil.so.0 \
		Source/Dataset.cpp Source/Formats.cpp $(YAML_OBJECTS) $(LDFLAGS) -o $@

$(LIBRARY): $(LIBRARY_REAL)
	ln -sfn libcppdatasetutil.so.0 $@

$(TEST): Tests/DatasetTest.cpp $(LIBRARY) Include/Dataset.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 -Wall -Wextra $< -Lbuild \
		-Wl,-rpath,'$$ORIGIN' -lcppdatasetutil -lgtest_main -lgtest -pthread -o $@

test: $(TEST)
	./$(TEST)

install: $(LIBRARY)
	install -d "$(DESTDIR)$(LIBDIR)" "$(DESTDIR)$(INCLUDEDIR)" \
		"$(DESTDIR)$(DATADIR)/licenses/cppdatasetutil" "$(DESTDIR)$(MANDIR)/man3"
	install -m 755 $(LIBRARY_REAL) "$(DESTDIR)$(LIBDIR)/"
	ln -sfn libcppdatasetutil.so.0 "$(DESTDIR)$(LIBDIR)/libcppdatasetutil.so"
	install -m 644 Include/Dataset.h "$(DESTDIR)$(INCLUDEDIR)/"
	install -m 644 LICENSE "$(DESTDIR)$(DATADIR)/licenses/cppdatasetutil/LICENSE"
	install -m 644 third_party/json/LICENSE.MIT "$(DESTDIR)$(DATADIR)/licenses/cppdatasetutil/json-LICENSE"
	install -m 644 third_party/libyaml/License "$(DESTDIR)$(DATADIR)/licenses/cppdatasetutil/libyaml-LICENSE"
	install -m 644 third_party/toml11/LICENSE "$(DESTDIR)$(DATADIR)/licenses/cppdatasetutil/toml11-LICENSE"
	install -m 644 docs/man3/CppDataSet.3 "$(DESTDIR)$(MANDIR)/man3/"
	@set -e; for lang in $(MAN_LANGS); do \
		install -d "$(DESTDIR)$(MANDIR)/$$lang/man3"; \
		install -m 644 "docs/$$lang/man3/CppDataSet.3" "$(DESTDIR)$(MANDIR)/$$lang/man3/"; \
	done

deb:
	sh packaging/package.sh deb

rpm:
	sh packaging/package.sh rpm

release-package: deb rpm

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

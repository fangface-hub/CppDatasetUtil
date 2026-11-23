cmake_minimum_required(VERSION 3.16)
project(CppDatasetUtil LANGUAGES C CXX)

add_library(cppdatasetutil SHARED
  Source/Dataset.cpp
  Source/Formats.cpp
  third_party/libyaml/src/api.c
  third_party/libyaml/src/dumper.c
  third_party/libyaml/src/emitter.c
  third_party/libyaml/src/loader.c
  third_party/libyaml/src/parser.c
  third_party/libyaml/src/reader.c
  third_party/libyaml/src/scanner.c
  third_party/libyaml/src/writer.c
)
target_compile_features(cppdatasetutil PUBLIC cxx_std_11)
target_include_directories(cppdatasetutil
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
  PRIVATE third_party/json third_party/ghc_filesystem third_party/toml11 third_party/libyaml/include
)
target_compile_definitions(cppdatasetutil PRIVATE
  CPPDATASETUTIL_BUILD YAML_DECLARE_STATIC
  YAML_VERSION_STRING="0.2.5"
  YAML_VERSION_MAJOR=0 YAML_VERSION_MINOR=2 YAML_VERSION_PATCH=5
)
if(MSVC)
  target_compile_definitions(cppdatasetutil PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(dll_consumer_test Tests/DllConsumerTest.cpp)
  target_compile_features(dll_consumer_test PRIVATE cxx_std_17)
  target_link_libraries(dll_consumer_test PRIVATE cppdatasetutil)
  add_test(NAME dll_consumer COMMAND dll_consumer_test)
endif()

if(WIN32)
  find_program(HTML_HELP_COMPILER hhc
    HINTS "$ENV{ProgramFiles\(x86\)}/HTML Help Workshop"
  )
  if(HTML_HELP_COMPILER)
    add_custom_target(windows_help
      COMMAND ${CMAKE_COMMAND}
        "-DHHC=${HTML_HELP_COMPILER}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/docs/windows"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/help"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/docs/windows/build-help.cmake"
      VERBATIM
    )
  else()
    message(STATUS "HTML Help Workshop not found; windows_help target unavailable")
  endif()
endif()

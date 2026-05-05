cmake_minimum_required(VERSION 3.16)

project(bees LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BEES_BUILD_TESTS "Build and run tests" ON)
option(BEES_BUILD_DOCS "Build docs target" ON)

find_package(Threads REQUIRED)

execute_process(
  COMMAND git describe --always --dirty
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE BEES_VERSION
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(BEES_VERSION STREQUAL "")
  set(BEES_VERSION "UNKNOWN")
endif()

set(BEES_COMMON_COMPILE_OPTIONS
  -Wall
  -Wextra
  -Werror
  -O3
  -D_FILE_OFFSET_BITS=64
)
set(BEES_CXX_COMPILE_OPTIONS
  -Wold-style-cast
  -Wno-missing-field-initializers
)

set(BEES_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
set(BEES_LIB_BINARY_DIR "${CMAKE_BINARY_DIR}/lib")
set(BEES_SRC_BINARY_DIR "${CMAKE_BINARY_DIR}/src")
file(MAKE_DIRECTORY "${BEES_LIB_BINARY_DIR}")
file(MAKE_DIRECTORY "${BEES_SRC_BINARY_DIR}")

configure_file(
  "${CMAKE_SOURCE_DIR}/lib/configure.h.in"
  "${BEES_LIB_BINARY_DIR}/configure.h"
  @ONLY
)

set(CRUCIBLE_SOURCES
  lib/bytevector.cc
  lib/btrfs-tree.cc
  lib/chatter.cc
  lib/city.cc
  lib/cleanup.cc
  lib/crc64.cc
  lib/error.cc
  lib/extentwalker.cc
  lib/fd.cc
  lib/fs.cc
  lib/multilock.cc
  lib/ntoa.cc
  lib/openat2.cc
  lib/path.cc
  lib/process.cc
  lib/seeker.cc
  lib/string.cc
  lib/table.cc
  lib/task.cc
  lib/time.cc
  lib/uname.cc
)

add_library(crucible STATIC ${CRUCIBLE_SOURCES})
target_include_directories(crucible
  PUBLIC
    "${BEES_INCLUDE_DIR}"
  PRIVATE
    "${BEES_LIB_BINARY_DIR}"
)
target_compile_options(crucible PRIVATE ${BEES_COMMON_COMPILE_OPTIONS} ${BEES_CXX_COMPILE_OPTIONS})
target_link_libraries(crucible PUBLIC Threads::Threads atomic)

set(BEES_USAGE_CONTENT "")
file(STRINGS "${CMAKE_SOURCE_DIR}/src/bees-usage.txt" BEES_USAGE_LINES NEWLINE_CONSUME)
foreach(line IN LISTS BEES_USAGE_LINES)
  string(REPLACE "\\" "\\\\" line "${line}")
  string(REPLACE "\"" "\\\"" line "${line}")
  string(APPEND BEES_USAGE_CONTENT "\"${line}\\n\"\n")
endforeach()
file(WRITE "${BEES_SRC_BINARY_DIR}/bees-usage.c" "const char *BEES_USAGE =\n${BEES_USAGE_CONTENT};\n")
file(WRITE "${BEES_SRC_BINARY_DIR}/bees-version.c" "const char *BEES_VERSION = \"${BEES_VERSION}\";\n")

set(BEES_SOURCES
  src/bees.cc
  src/bees-context.cc
  src/bees-hash.cc
  src/bees-resolve.cc
  src/bees-roots.cc
  src/bees-thread.cc
  src/bees-trace.cc
  src/bees-types.cc
  "${BEES_SRC_BINARY_DIR}/bees-usage.c"
  "${BEES_SRC_BINARY_DIR}/bees-version.c"
)

add_executable(bees ${BEES_SOURCES})
target_include_directories(bees PRIVATE "${BEES_INCLUDE_DIR}" "${BEES_LIB_BINARY_DIR}")
target_compile_options(bees PRIVATE ${BEES_COMMON_COMPILE_OPTIONS} ${BEES_CXX_COMPILE_OPTIONS})
target_link_libraries(bees PRIVATE crucible Threads::Threads atomic)
set_target_properties(bees PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

if(BEES_BUILD_TESTS)
  set(TEST_PROGRAMS
    chatter
    crc64
    fd
    limits
    namedptr
    path
    process
    progress
    seeker
    table
    task
  )

  foreach(prog IN LISTS TEST_PROGRAMS)
    add_executable("test_${prog}" "test/${prog}.cc")
    target_include_directories("test_${prog}" PRIVATE "${BEES_INCLUDE_DIR}" "${BEES_LIB_BINARY_DIR}")
    target_compile_options("test_${prog}" PRIVATE ${BEES_COMMON_COMPILE_OPTIONS} ${BEES_CXX_COMPILE_OPTIONS})
    target_link_libraries("test_${prog}" PRIVATE crucible Threads::Threads atomic)
    add_test(NAME "${prog}" COMMAND "test_${prog}")
  endforeach()
endif()

set(BEESD_SCRIPT_IN "${CMAKE_SOURCE_DIR}/scripts/beesd.in")
set(BEESD_SERVICE_IN "${CMAKE_SOURCE_DIR}/scripts/beesd@.service.in")
set(BEESD_SCRIPT_OUT "${CMAKE_BINARY_DIR}/scripts/beesd")
set(BEESD_SERVICE_OUT "${CMAKE_BINARY_DIR}/scripts/beesd@.service")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/scripts")

set(DESTDIR "")
set(PREFIX "${CMAKE_INSTALL_PREFIX}")
set(BINDIR "${CMAKE_INSTALL_SBINDIR}")
set(ETC_PREFIX "${CMAKE_INSTALL_FULL_SYSCONFDIR}")
set(LIBEXEC_PREFIX "${CMAKE_INSTALL_FULL_LIBEXECDIR}/bees")
configure_file("${BEESD_SCRIPT_IN}" "${BEESD_SCRIPT_OUT}" @ONLY)
configure_file("${BEESD_SERVICE_IN}" "${BEESD_SERVICE_OUT}" @ONLY)

install(TARGETS bees RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/bees")
install(PROGRAMS "${BEESD_SCRIPT_OUT}" DESTINATION "${CMAKE_INSTALL_SBINDIR}")
install(FILES "${CMAKE_SOURCE_DIR}/scripts/beesd.conf.sample" DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/bees")

find_program(PKG_CONFIG_EXECUTABLE pkg-config)
if(PKG_CONFIG_EXECUTABLE)
  execute_process(
    COMMAND "${PKG_CONFIG_EXECUTABLE}" systemd --variable=systemdsystemunitdir
    OUTPUT_VARIABLE SYSTEMD_SYSTEM_UNIT_DIR
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT SYSTEMD_SYSTEM_UNIT_DIR STREQUAL "")
    install(FILES "${BEESD_SERVICE_OUT}" DESTINATION "${SYSTEMD_SYSTEM_UNIT_DIR}")
  endif()
endif()

if(BEES_BUILD_DOCS)
  find_program(MARKDOWN_EXE NAMES cmark-gfm redcarpet markdown2 markdown markdown_py)
  if(MARKDOWN_EXE)
    add_custom_target(docs
      COMMAND /bin/sh -c "\"${MARKDOWN_EXE}\" \"${CMAKE_SOURCE_DIR}/README.md\" > \"${CMAKE_BINARY_DIR}/README.html\""
      COMMENT "Building docs"
      VERBATIM
    )
  endif()
endif()

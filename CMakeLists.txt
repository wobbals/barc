cmake_minimum_required (VERSION 3.5)
project (Barc)

set (CMAKE_CXX_FLAGS "--std=gnu++11 ${CMAKE_CXX_FLAGS}")
set (CMAKE_C_FLAGS "--std=gnu99 ${CMAKE_C_FLAGS}")

file (GLOB SOURCES "barc/*.c" "barc/*.cpp" "barc/*.cc")
list (REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/barc/main.c")

message ("libbarc using sources: ${SOURCES}")

add_subdirectory (${CMAKE_SOURCE_DIR}/ext/litehtml)

link_libraries (litehtml)

find_package (PkgConfig)
pkg_check_modules (MAGICKWAND REQUIRED MagickWand)
pkg_check_modules (LIBAVCODEC REQUIRED libavcodec)
pkg_check_modules (LIBAVUTIL REQUIRED libavutil)
pkg_check_modules (LIBAVFORMAT REQUIRED libavformat)
pkg_check_modules (LIBAVFILTER REQUIRED libavfilter)
pkg_check_modules (LIBSWSCALE REQUIRED libswscale)
pkg_check_modules (LIBSWRESAMPLE REQUIRED libswresample)
pkg_check_modules (LIBUV REQUIRED libuv)
pkg_check_modules (LIBZIP REQUIRED libzip)
pkg_check_modules (LIBJANSSON REQUIRED jansson)

# Repair broken -framework flags from pkg_check_modules
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  function(repair_framework_flags BAD_FLAG)
    string(REPLACE "-framework;" "-framework " GOOD_FLAG "${BAD_FLAG}")
    set(REPAIRED_FLAG "${GOOD_FLAG}" PARENT_SCOPE)
  endfunction()
  # "-framework;Security;-framework;CoreFoundation" to
  # "-framework Security;-framework CoreFoundation"
  repair_framework_flags("${LIBAVCODEC_LDFLAGS}")
  set(LIBAVCODEC_LDFLAGS "${REPAIRED_FLAG}")
  repair_framework_flags("${LIBAVFORMAT_LDFLAGS}")
  set(LIBAVFORMAT_LDFLAGS "${REPAIRED_FLAG}")
  repair_framework_flags("${LIBAVFILTER_LDFLAGS}")
  set(LIBAVFILTER_LDFLAGS "${REPAIRED_FLAG}")
endif()

link_libraries (${MAGICKWAND_LDFLAGS})
link_libraries (${LIBAVCODEC_LDFLAGS})
link_libraries (${LIBAVUTIL_LDFLAGS})
link_libraries (${LIBAVFORMAT_LDFLAGS})
link_libraries (${LIBAVFILTER_LDFLAGS})
link_libraries (${LIBSWSCALE_LDFLAGS})
link_libraries (${LIBSWRESAMPLE_LDFLAGS})
link_libraries (${LIBUV_LDFLAGS})
link_libraries (${LIBZIP_LDFLAGS})
link_libraries (${LIBJANSSON_LDFLAGS})

include_directories (
  ${PROJECT_SOURCE_DIR}/ext
  ${MAGICKWAND_INCLUDE_DIRS}
  ${LIBAVCODEC_INCLUDE_DIRS}
  ${LIBAVUTIL_INCLUDE_DIRS}
  ${LIBAVFORMAT_INCLUDE_DIRS}
  ${LIBAVFILTER_INCLUDE_DIRS}
  ${LIBSWSCALE_INCLUDE_DIRS}
  ${LIBSWRESAMPLE_INCLUDE_DIRS}
  ${LIBUV_INCLUDE_DIRS}
  ${LIBZIP_INCLUDE_DIRS}
  ${LIBJANSSON_INCLUDE_DIRS}
)

# This comes at the end of all the linking commands issued above.
add_library (libbarc ${SOURCES})
link_libraries (libbarc)
add_executable (barc "barc/main.c")

add_subdirectory(ext/googletest/googletest)
enable_testing (true)
include_directories (
  ${gtest_SOURCE_DIR}/include
  ${gtest_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/barc
)

cxx_executable(test_smart_avframe test gtest_main test/test_smart_avframe.cc)
add_test(test_smart_avframe test_smart_avframe)
cxx_executable(test_file_writer test gtest_main test/test_file_writer.cc)
add_test(test_file_writer test_file_writer)

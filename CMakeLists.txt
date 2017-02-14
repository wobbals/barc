cmake_minimum_required (VERSION 3.6)
project (Barc)

#include_directories ("src" "ext")

file(GLOB SOURCES "barc/*.c" "barc/*.cpp" "barc/*.cc")

set (CMAKE_CXX_FLAGS "--std=gnu++11 ${CMAKE_CXX_FLAGS}")

add_executable (barc ${SOURCES})

add_subdirectory (${CMAKE_SOURCE_DIR}/ext/litehtml)
target_link_libraries(barc PUBLIC litehtml)

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

target_link_libraries (barc PUBLIC ${MAGICKWAND_LDFLAGS})
target_link_libraries (barc PUBLIC ${LIBAVCODEC_LDFLAGS})
target_link_libraries (barc PUBLIC ${LIBAVUTIL_LDFLAGS})
target_link_libraries (barc PUBLIC ${LIBAVFORMAT_LDFLAGS})
target_link_libraries (barc PUBLIC ${LIBAVFILTER_LDFLAGS})
target_link_libraries (barc PUBLIC ${LIBSWSCALE_LDFLAGS})
target_link_libraries (barc PUBLIC ${LIBSWRESAMPLE_LDFLAGS})
target_link_libraries (barc PUBLIC ${LIBUV_LDFLAGS})
target_link_libraries (barc PUBLIC ${LIBZIP_LDFLAGS})
target_link_libraries (barc PUBLIC ${LIBJANSSON_LDFLAGS})

target_include_directories (barc PUBLIC
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

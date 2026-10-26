cmake_minimum_required(VERSION 3.20)
project(ZuPather VERSION 0.2.0 LANGUAGES C OBJC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Load explicit source file manifest
include(cmake/sources.cmake)

# Homebrew / MacPorts search paths
list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")

# macOS Compatibility Layer (CoreGraphics, CoreText & Cocoa View Subsystem)
list(APPEND ZUPATHER_SOURCES
    src/compat/win32_macos_gdi.m
    src/compat/win32_macos_user.m
    src/compat/win32_macos_io.m
)
list(APPEND ZUPATHER_HEADERS
    src/compat/win32_macos_shim.h
)

# Bundle Resources for macOS App Bundle
file(GLOB_RECURSE MACOS_BUNDLE_FONTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.ttf")
file(GLOB_RECURSE MACOS_BUNDLE_MEDIA "${CMAKE_CURRENT_SOURCE_DIR}/assets/media/*.*")
set(MACOS_BUNDLE_RESOURCES ${MACOS_BUNDLE_FONTS} ${MACOS_BUNDLE_MEDIA})

set_source_files_properties(${MACOS_BUNDLE_RESOURCES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
)

# Define macOS Application Bundle (ZuPather.app)
add_executable(ZuPather MACOSX_BUNDLE
    ${ZUPATHER_SOURCES}
    ${ZUPATHER_HEADERS}
    ${MACOS_BUNDLE_RESOURCES}
)

target_include_directories(ZuPather PRIVATE
    src
    src/compat
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(ZuPather PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "ZuPather"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.rizkybayuu.zupather"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

# Apple Native Frameworks
target_link_libraries(ZuPather PRIVATE
    "-framework Cocoa"
    "-framework Carbon"
    "-framework QuartzCore"
    "-framework CoreGraphics"
    "-framework CoreText"
    "-framework ImageIO"
    "-framework UniformTypeIdentifiers"
)

# WebP Library Resolution
find_package(WebP CONFIG QUIET)
if(WebP_FOUND)
    set(WEBP_LIBS WebP::webp WebP::webpdemux)
    if(TARGET WebP::libwebpmux)
        list(APPEND WEBP_LIBS WebP::libwebpmux)
    elseif(TARGET WebP::webpmux)
        list(APPEND WEBP_LIBS WebP::webpmux)
    elseif(TARGET WebP::mux)
        list(APPEND WEBP_LIBS WebP::mux)
    endif()
    target_link_libraries(ZuPather PRIVATE ${WEBP_LIBS})
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(WEBP QUIET libwebp libwebpdemux libwebpmux)
        if(WEBP_FOUND)
            target_include_directories(ZuPather PRIVATE ${WEBP_INCLUDE_DIRS})
            target_link_libraries(ZuPather PRIVATE ${WEBP_LIBRARIES})
        endif()
    endif()
    if(NOT WEBP_FOUND)
        target_link_libraries(ZuPather PRIVATE webp webpdemux webpmux)
    endif()
endif()

# Zstandard (zstd) Library Resolution
find_package(zstd CONFIG QUIET)
if(zstd_FOUND)
    if(TARGET zstd::libzstd_static)
        target_link_libraries(ZuPather PRIVATE zstd::libzstd_static)
    elseif(TARGET zstd::libzstd_shared)
        target_link_libraries(ZuPather PRIVATE zstd::libzstd_shared)
    endif()
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ZSTD QUIET libzstd)
        if(ZSTD_FOUND)
            target_include_directories(ZuPather PRIVATE ${ZSTD_INCLUDE_DIRS})
            target_link_libraries(ZuPather PRIVATE ${ZSTD_LIBRARIES})
        endif()
    endif()
    if(NOT ZSTD_FOUND)
        find_library(ZSTD_LIB NAMES zstd zstd_static)
        if(ZSTD_LIB)
            target_link_libraries(ZuPather PRIVATE ${ZSTD_LIB})
        endif()
    endif()
endif()

# Compiler & Linker Flag Optimizations
target_compile_definitions(ZuPather PRIVATE
    _stricmp=strcasecmp
    _strnicmp=strncasecmp
)

if(APPLE OR CMAKE_C_COMPILER_ID MATCHES "AppleClang")
    target_compile_options(ZuPather PRIVATE
        -O2
        -Wall
        -Wextra
        -Wno-unused-parameter
    )
endif()

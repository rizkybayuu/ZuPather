cmake_minimum_required(VERSION 3.20)
project(ZuPather VERSION 0.1.2 LANGUAGES C RC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Load explicit source file manifest
include(cmake/sources.cmake)

# Windows Resource file (.rc)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/res/zupather.rc")
    list(APPEND ZUPATHER_SOURCES res/zupather.rc)
endif()

# Define WIN32 GUI executable (ZuPather.exe)
add_executable(ZuPather WIN32 ${ZUPATHER_SOURCES} ${ZUPATHER_HEADERS})

target_include_directories(ZuPather PRIVATE
    src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Common Windows System Libraries
target_link_libraries(ZuPather PRIVATE
    user32
    gdi32
    comdlg32
    comctl32
    msimg32
    windowscodecs
    ole32
    shell32
    uxtheme
    dwmapi
    mfplat
    mfreadwrite
    mfuuid
    winmm
)

# WebP Library Resolution
find_package(WebP CONFIG QUIET)
if(WebP_FOUND)
    target_link_libraries(ZuPather PRIVATE WebP::webp WebP::webpdemux WebP::webpmux)
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
        if(MINGW)
            target_link_libraries(ZuPather PRIVATE
                -Wl,-Bstatic
                -lwebpmux
                -lwebpdemux
                -lwebp
                -lsharpyuv
                -Wl,-Bdynamic
            )
        endif()
    endif()
endif()

# Compiler & Linker Flag Optimizations
if(MSVC)
    target_compile_options(ZuPather PRIVATE
        /O2
        /W3
        /utf-8
    )
    target_link_options(ZuPather PRIVATE
        /SUBSYSTEM:WINDOWS
    )
elseif(MINGW OR CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ZuPather PRIVATE
        -O2
        -Wall
        -Wextra
        -Wno-unused-parameter
    )
    target_link_libraries(ZuPather PRIVATE -lm)
endif()

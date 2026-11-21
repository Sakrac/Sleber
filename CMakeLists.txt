cmake_minimum_required(VERSION 3.16)
project(Sleber LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(WIN32 AND MSVC)
    add_compile_options("$<$<CONFIG:Debug>:/fsanitize=address>")
    add_link_options("$<$<CONFIG:Debug>:/INCREMENTAL:NO>")
    add_compile_options("$<$<CONFIG:Release>:/Zi>")
    add_link_options("$<$<CONFIG:Release>:/DEBUG>")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if(MSVC)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(SLEBER_TARGET_ARCH "x64")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(SLEBER_TARGET_ARCH "x86")
    else()
        set(SLEBER_TARGET_ARCH "unknown")
    endif()
    message(STATUS "MSVC target architecture: ${SLEBER_TARGET_ARCH}")
endif()

add_library(miniz STATIC src/miniz/miniz.c)
target_compile_definitions(miniz PUBLIC MINIZ_NO_ARCHIVE_APIS MINIZ_NO_STDIO)
target_include_directories(miniz SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/miniz
)

add_library(stb STATIC src/stb/stb.c)
target_include_directories(stb SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set( SOURCES
    src/c64_data.h
    src/c64_data.c
    src/convert.h
    src/convert.c
    src/Edit.c
    src/Edit.h
    src/files.c
    src/files.h
    src/gpx_load.c
    src/gpx_load.h
    src/palette.c
    src/palette.h
    src/platform.h
    src/sbx_file.c
    src/sbx_file.h
    src/sleber.c
    src/sleber.h
    src/undo.c
    src/undo.h
)

if(WIN32)
    set(PLATFORM_SOURCE src/platform_windows.c)
    set(PLATFORM_RESOURCE src/sleber.rc)
    add_executable(Sleber WIN32 ${SOURCES} ${PLATFORM_SOURCE} ${PLATFORM_RESOURCE})
    target_compile_definitions(Sleber PRIVATE UNICODE _UNICODE)
    target_link_libraries(Sleber PRIVATE
        comdlg32
        gdi32
        ole32
        user32
    )
    target_include_directories(Sleber PRIVATE
        src
    )

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_SOURCE src/platform_linux.c)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

    add_executable(Sleber ${SOURCES} ${PLATFORM_SOURCE})
    configure_file(src/sleber.png ${CMAKE_CURRENT_BINARY_DIR}/sleber.png COPYONLY)

    target_include_directories(Sleber PRIVATE
        ${GTK3_INCLUDE_DIRS}
        src
    )

    target_compile_definitions(Sleber PRIVATE
        SLEBER_ICON_PATH="${CMAKE_CURRENT_BINARY_DIR}/sleber.png"
    )

    target_compile_options(Sleber PRIVATE ${GTK3_CFLAGS_OTHER})

    target_link_libraries(Sleber PRIVATE
        ${GTK3_LIBRARIES}
        m
    )
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

target_link_libraries(Sleber PRIVATE miniz stb)
target_compile_definitions(Sleber PRIVATE "$<$<CONFIG:Debug>:DEBUG>")

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Sleber PRIVATE -Wall -Wextra -Werror)
elseif(MSVC)
    target_compile_options(Sleber PRIVATE /W4 /WX)
endif()

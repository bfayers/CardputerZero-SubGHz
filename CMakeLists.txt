#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.25)

project(cc1101_subghz VERSION 0.1.8 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(USE_DESKTOP ON CACHE BOOL "Build the desktop emulator target with LVGL SDL support")
set(BUILD_TESTING ON CACHE BOOL "Build unit tests")
set(APP_NAME "${PROJECT_NAME}" CACHE STRING "Application name used for installed asset lookup")
set(APP_ASSETS_ROOT "" CACHE PATH "Optional runtime assets root. Expected subdirectories include fonts/")
set(APP_CONFIG_FILE "" CACHE FILEPATH "Application config file; defaults to the repository file on desktop and /etc/cc1101-subghz.conf on device")
set(APP_KEY_INPUT_DEVICE "" CACHE STRING "Optional Linux evdev keyboard/keypad device path, e.g. /dev/input/event0")
set(APP_FRAMEBUFFER_DEVICE "/dev/fb0" CACHE STRING "Linux framebuffer device path used by embedded builds")
set(APP_DRM_DEVICE "/dev/dri/card0" CACHE STRING "Linux DRM device path used when APP_USE_DRM=ON")
set(APP_DRM_CONNECTOR_ID "-1" CACHE STRING "DRM connector id used when APP_USE_DRM=ON; -1 auto-selects")
set(APP_USE_DRM OFF CACHE BOOL "Enable LVGL Linux DRM/KMS backend for embedded Linux builds")
set(CM0_ALLOW_FETCH_DEPS ON CACHE BOOL "Allow downloading missing CM0 BSP/dependencies during cross builds")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif()

if(NOT APP_CONFIG_FILE)
    if(USE_DESKTOP)
        set(APP_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/template-app.conf")
    else()
        set(APP_CONFIG_FILE "/etc/cc1101-subghz.conf")
    endif()
endif()

include(FetchContent)
find_package(Threads REQUIRED)

# lvgl - main ui framework
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS    OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/src/config/lv_conf.h" CACHE PATH "" FORCE)

FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.5.0
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(lvgl)
if(NOT lvgl_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(lvgl)
    if(POLICY CMP0169)
        cmake_policy(POP)
    endif()
endif()
add_subdirectory("${lvgl_SOURCE_DIR}" "${lvgl_BINARY_DIR}" EXCLUDE_FROM_ALL)
set(LVGL_SOURCE_DIR "${lvgl_SOURCE_DIR}")
get_filename_component(LVGL_INCLUDE_PARENT "${LVGL_SOURCE_DIR}" DIRECTORY)

if(USE_DESKTOP)
    message(STATUS "Configuring DESKTOP build")

    find_package(PNG REQUIRED)
    find_package(JPEG REQUIRED)
    if(TARGET libjpeg-turbo::libjpeg-turbo)
        set(JPEG_TARGET libjpeg-turbo::libjpeg-turbo)
    elseif(TARGET JPEG::JPEG)
        set(JPEG_TARGET JPEG::JPEG)
    else()
        message(FATAL_ERROR "No JPEG provider found")
    endif()

    find_package(Freetype REQUIRED)
    find_package(SDL2 CONFIG REQUIRED)
    find_package(fmt CONFIG REQUIRED)
    find_package(ZLIB REQUIRED)

    set(LVGL_DESKTOP_DEPS
        ZLIB::ZLIB
        PNG::PNG
        ${JPEG_TARGET}
        Freetype::Freetype
        SDL2::SDL2
    )

    foreach(dep IN LISTS LVGL_DESKTOP_DEPS)
        if(TARGET ${dep})
            target_link_libraries(lvgl PUBLIC ${dep})
        endif()
    endforeach()

    if(SDL2_FOUND AND NOT TARGET SDL2::SDL2)
        target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
        target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
    endif()

    target_compile_definitions(lvgl PUBLIC USE_DESKTOP=1)

    if(MSVC)
        target_compile_options(lvgl PUBLIC /utf-8)
    endif()
else()
    message(STATUS "Configuring CM0/aarch64 Linux build")

    if(NOT CM0_SDK_ROOT OR NOT EXISTS "${CM0_SDK_ROOT}")
        message(FATAL_ERROR
            "CM0 SDK sysroot not found or invalid.\n"
            "The cp0 toolchain should download the BSP before project() runs, "
            "or configure with -DCM0_SDK_ROOT=<path_to_sdk_sysroot>."
        )
    endif()

    if(CMAKE_SYSROOT)
        set(CM0_SYSROOT "${CMAKE_SYSROOT}")
    else()
        set(CM0_SYSROOT "${CM0_SDK_ROOT}")
    endif()
    if(NOT CM0_MULTIARCH)
        set(CM0_MULTIARCH "aarch64-linux-gnu")
    endif()

    set(CM0_INCLUDE_HINTS
        "${CM0_SYSROOT}/usr/include/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/usr/include"
        "${CM0_SYSROOT}/usr/local/include"
        "${CM0_SYSROOT}/include/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/include"
    )
    set(CM0_LIBRARY_HINTS
        "${CM0_SYSROOT}/usr/lib/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/usr/lib"
        "${CM0_SYSROOT}/usr/local/lib/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/usr/local/lib"
        "${CM0_SYSROOT}/lib/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/lib"
    )

    function(cm0_find_header out_var header_name)
        find_path(${out_var}
            NAMES ${header_name}
            HINTS ${ARGN}
            NO_DEFAULT_PATH
        )
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
    endfunction()

    function(cm0_find_library out_var)
        find_library(${out_var}
            NAMES ${ARGN}
            HINTS ${CM0_LIBRARY_HINTS}
            NO_DEFAULT_PATH
        )
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
    endfunction()

    function(cm0_add_imported_library target_name library_path include_dirs)
        if(NOT TARGET ${target_name})
            add_library(${target_name} UNKNOWN IMPORTED)
            set_target_properties(${target_name} PROPERTIES
                IMPORTED_LOCATION "${library_path}"
                INTERFACE_INCLUDE_DIRECTORIES "${include_dirs}"
            )
        endif()
    endfunction()

    if(APP_USE_DRM)
        cm0_find_header(DRM_XF86_INCLUDE_DIR xf86drm.h
            "${CM0_SYSROOT}/usr/include"
            "${CM0_SYSROOT}/usr/include/libdrm"
        )
        cm0_find_header(DRM_XF86_MODE_INCLUDE_DIR xf86drmMode.h
            "${CM0_SYSROOT}/usr/include"
            "${CM0_SYSROOT}/usr/include/libdrm"
        )
        cm0_find_header(DRM_MODE_INCLUDE_DIR drm.h
            "${CM0_SYSROOT}/usr/include/drm"
            "${CM0_SYSROOT}/usr/include/libdrm"
            "${CM0_SYSROOT}/usr/include"
        )
        cm0_find_library(DRM_LIBRARY drm drm.so.2)
        if(NOT DRM_XF86_INCLUDE_DIR OR NOT DRM_XF86_MODE_INCLUDE_DIR OR NOT DRM_MODE_INCLUDE_DIR OR NOT DRM_LIBRARY)
            message(FATAL_ERROR "APP_USE_DRM=ON but libdrm headers/library were not found in BSP sysroot.")
        endif()
        cm0_add_imported_library(CM0::DRM "${DRM_LIBRARY}"
            "${DRM_XF86_INCLUDE_DIR};${DRM_XF86_MODE_INCLUDE_DIR};${DRM_MODE_INCLUDE_DIR}"
        )
    endif()

    cm0_find_header(FREETYPE_INCLUDE_DIR ft2build.h
        "${CM0_SYSROOT}/usr/include/freetype2"
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(FREETYPE_LIBRARY freetype)
    cm0_add_imported_library(CM0::Freetype "${FREETYPE_LIBRARY}" "${FREETYPE_INCLUDE_DIR}")

    cm0_find_header(PNG_INCLUDE_DIR png.h
        "${CM0_SYSROOT}/usr/include/libpng16"
        "${CM0_SYSROOT}/usr/include/libpng"
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(PNG_LIBRARY png16 png)
    cm0_add_imported_library(CM0::PNG "${PNG_LIBRARY}" "${PNG_INCLUDE_DIR}")

    cm0_find_header(JPEG_INCLUDE_DIR jpeglib.h "${CM0_SYSROOT}/usr/include")
    cm0_find_library(JPEG_LIBRARY jpeg)
    cm0_add_imported_library(CM0::JPEG "${JPEG_LIBRARY}" "${JPEG_INCLUDE_DIR}")

    cm0_find_header(ZLIB_INCLUDE_DIR zlib.h "${CM0_SYSROOT}/usr/include")
    cm0_find_library(ZLIB_LIBRARY z)
    cm0_add_imported_library(ZLIB::ZLIB "${ZLIB_LIBRARY}" "${ZLIB_INCLUDE_DIR}")

    # libgpiod for CC1101 GDO0 and RF_SW0
    cm0_find_header(GPIOD_INCLUDE_DIR gpiod.h "${CM0_SYSROOT}/usr/include" "${CM0_SYSROOT}/usr/include/${CM0_MULTIARCH}")
    cm0_find_library(GPIOD_LIBRARY gpiod libgpiod.so.3.1.1 libgpiod.so.3 libgpiod.so.2)
    if(NOT GPIOD_INCLUDE_DIR AND CM0_ALLOW_FETCH_DEPS)
        # The v0.0.4 CM0 BSP ships libgpiod.so.3.1.1 but omits gpiod.h; fetch matching v2.2.1 gpiod.h
        set(GPIOD_FETCHED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/libgpiod/include")
        file(MAKE_DIRECTORY "${GPIOD_FETCHED_INCLUDE_DIR}")
        if(NOT EXISTS "${GPIOD_FETCHED_INCLUDE_DIR}/gpiod.h")
            message(STATUS "Fetching libgpiod v2.2.1 gpiod.h for CM0 BSP sysroot")
            file(DOWNLOAD
                "https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/plain/include/gpiod.h?h=v2.2.1"
                "${GPIOD_FETCHED_INCLUDE_DIR}/gpiod.h"
                STATUS _GPIOD_DL_STATUS
                TIMEOUT 60
            )
            list(GET _GPIOD_DL_STATUS 0 _GPIOD_DL_CODE)
            if(NOT _GPIOD_DL_CODE EQUAL 0)
                file(REMOVE "${GPIOD_FETCHED_INCLUDE_DIR}/gpiod.h")
                message(FATAL_ERROR "Failed to download gpiod.h for CM0 cross build")
            endif()
        endif()
        set(GPIOD_INCLUDE_DIR "${GPIOD_FETCHED_INCLUDE_DIR}")
    endif()
    if(NOT GPIOD_INCLUDE_DIR OR NOT GPIOD_LIBRARY)
        message(FATAL_ERROR "Missing libgpiod header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(CM0::GPIOD "${GPIOD_LIBRARY}" "${GPIOD_INCLUDE_DIR}")

    set(GPIOD_V2 OFF)
    if(EXISTS "${GPIOD_INCLUDE_DIR}/gpiod.h")
        file(STRINGS "${GPIOD_INCLUDE_DIR}/gpiod.h" GPIOD_V2_DECL REGEX "gpiod_line_settings_new" LIMIT_COUNT 1)
        if(GPIOD_V2_DECL)
            set(GPIOD_V2 ON)
        endif()
    endif()

    set(CM0_MULTIARCH_INCLUDE_DIRS)
    foreach(CM0_INCLUDE_DIR IN ITEMS
            "${CM0_SYSROOT}/usr/include/${CM0_MULTIARCH}"
            "${CM0_SYSROOT}/include/${CM0_MULTIARCH}")
        if(EXISTS "${CM0_INCLUDE_DIR}/bits/timesize.h" OR EXISTS "${CM0_INCLUDE_DIR}/bits/wordsize.h")
            list(APPEND CM0_MULTIARCH_INCLUDE_DIRS "${CM0_INCLUDE_DIR}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES CM0_MULTIARCH_INCLUDE_DIRS)

    cm0_find_header(FMT_INCLUDE_DIR fmt/format.h "${CM0_SYSROOT}/usr/include")
    cm0_find_library(FMT_LIBRARY fmt libfmt.so.10 libfmt.so.10.1.0)
    if(FMT_INCLUDE_DIR AND FMT_LIBRARY)
        cm0_add_imported_library(fmt::fmt "${FMT_LIBRARY}" "${FMT_INCLUDE_DIR}")
    elseif(CM0_ALLOW_FETCH_DEPS)
        FetchContent_Declare(
            fmt
            GIT_REPOSITORY https://github.com/fmtlib/fmt.git
            GIT_TAG        10.1.1
            GIT_SHALLOW    TRUE
        )
        set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
        set(FMT_TEST OFF CACHE BOOL "" FORCE)
        set(FMT_DOC OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(fmt)
        if(TARGET fmt)
            target_include_directories(fmt SYSTEM BEFORE PRIVATE ${CM0_MULTIARCH_INCLUDE_DIRS})
        endif()
        if(TARGET fmt AND NOT TARGET fmt::fmt)
            add_library(fmt::fmt ALIAS fmt)
        endif()
    else()
        message(FATAL_ERROR "Missing fmt header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()

    set_target_properties(CM0::PNG PROPERTIES INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)
    set_target_properties(CM0::Freetype PROPERTIES INTERFACE_LINK_LIBRARIES "CM0::PNG;ZLIB::ZLIB")

    cm0_find_library(PTHREAD_LIBRARY pthread libpthread.so.0 libpthread.a)
    cm0_find_library(M_LIBRARY m libm.so libm.so.6 libm.a)

    set(PLATFORM_INCLUDE_DIR ${CM0_MULTIARCH_INCLUDE_DIRS})
    set(PLATFORM_LIBS ${PTHREAD_LIBRARY} ${M_LIBRARY})

    set(LVGL_INCLUDE_DIR
        ${FREETYPE_INCLUDE_DIR}
        ${PNG_INCLUDE_DIR}
        ${JPEG_INCLUDE_DIR}
        ${ZLIB_INCLUDE_DIR}
    )
    set(LVGL_LIBS
        CM0::Freetype
        CM0::PNG
        CM0::JPEG
        ZLIB::ZLIB
    )
    if(APP_USE_DRM)
        list(APPEND LVGL_INCLUDE_DIR
            ${DRM_XF86_INCLUDE_DIR}
            ${DRM_XF86_MODE_INCLUDE_DIR}
            ${DRM_MODE_INCLUDE_DIR}
        )
        list(APPEND LVGL_LIBS CM0::DRM)
    endif()

    target_compile_definitions(lvgl
        PUBLIC
            USE_DESKTOP=0
            APP_USE_DRM=$<BOOL:${APP_USE_DRM}>
    )

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 15.0)
        target_compile_options(lvgl PRIVATE -Wno-stringop-overflow)
    endif()

    target_include_directories(lvgl PUBLIC ${LVGL_INCLUDE_DIR} ${PLATFORM_INCLUDE_DIR})
    target_link_libraries(lvgl PUBLIC ${LVGL_LIBS} ${PLATFORM_LIBS})
endif()

# ====================================================
# Logger library
# ====================================================
file(GLOB_RECURSE LOGGER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/logger/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/logger/*.cpp"
)

add_library(logger STATIC ${LOGGER_SOURCES})
target_include_directories(logger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logger
    ${PLATFORM_INCLUDE_DIR}
)
target_link_libraries(logger PUBLIC fmt::fmt PRIVATE ${PLATFORM_LIBS})

# ====================================================
# Platform sources
# ====================================================
file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp"
)

add_library(${PROJECT_NAME}_platform STATIC ${PLATFORM_SOURCES})
target_include_directories(${PROJECT_NAME}_platform
    PUBLIC
        ${LVGL_INCLUDE_DIR}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform"
)
target_compile_definitions(${PROJECT_NAME}_platform
    PUBLIC
        APP_KEY_INPUT_DEVICE="${APP_KEY_INPUT_DEVICE}"
        APP_FRAMEBUFFER_DEVICE="${APP_FRAMEBUFFER_DEVICE}"
        APP_DRM_DEVICE="${APP_DRM_DEVICE}"
        APP_DRM_CONNECTOR_ID=${APP_DRM_CONNECTOR_ID}
)
target_link_libraries(${PROJECT_NAME}_platform PUBLIC lvgl)

# ====================================================
# Model sources
# ====================================================
file(GLOB_RECURSE MODEL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/model/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/model/*.cpp"
)

add_library(${PROJECT_NAME}_model STATIC ${MODEL_SOURCES})
target_include_directories(${PROJECT_NAME}_model
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/model"
        ${PLATFORM_INCLUDE_DIR}
)

# ====================================================
# HAL + Radio sources
# ====================================================
file(GLOB_RECURSE RADIO_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/hal/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/radio/*.cpp"
)

add_library(${PROJECT_NAME}_radio STATIC ${RADIO_SOURCES})
target_include_directories(${PROJECT_NAME}_radio
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/hal"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/radio"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/radio/driver"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/model"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/logger"
)
target_link_libraries(${PROJECT_NAME}_radio
    PUBLIC
        ${PROJECT_NAME}_model
        logger
        Threads::Threads
)
target_compile_definitions(${PROJECT_NAME}_radio
    PUBLIC
        USE_DESKTOP=$<IF:$<BOOL:${USE_DESKTOP}>,1,0>
)
if(NOT USE_DESKTOP)
    target_link_libraries(${PROJECT_NAME}_radio PUBLIC CM0::GPIOD)
    if(GPIOD_V2)
        target_compile_definitions(${PROJECT_NAME}_radio PUBLIC GPIOD_V2=1)
    endif()
endif()

# ====================================================
# Reactive & View sources
# ====================================================
file(GLOB_RECURSE REACTIVE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reactive/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reactive/*.cpp"
)

add_library(${PROJECT_NAME}_reactive STATIC ${REACTIVE_SOURCES})
target_include_directories(${PROJECT_NAME}_reactive
    PUBLIC
        ${LVGL_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/reactive
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view
)
target_link_libraries(${PROJECT_NAME}_reactive PUBLIC lvgl)

file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/view/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/view/*.cpp"
)

add_library(${PROJECT_NAME}_view STATIC ${UI_SOURCES})
target_include_directories(${PROJECT_NAME}_view
    PUBLIC
        ${LVGL_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view/screens
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/reactive
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/radio
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
)
target_link_libraries(${PROJECT_NAME}_view
    PUBLIC
        lvgl
        ${PROJECT_NAME}_reactive
        ${PROJECT_NAME}_platform
        ${PROJECT_NAME}_model
        ${PROJECT_NAME}_radio
)

# ====================================================
# ViewModel sources
# ====================================================
file(GLOB_RECURSE VIEWMODEL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel/*.cpp"
)

add_library(${PROJECT_NAME}_viewmodel STATIC ${VIEWMODEL_SOURCES})
target_include_directories(${PROJECT_NAME}_viewmodel
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel/"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/model"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/radio"
        ${LVGL_INCLUDE_DIR}
)
target_link_libraries(${PROJECT_NAME}_viewmodel
    PUBLIC
        lvgl
        ${PROJECT_NAME}_reactive
        ${PROJECT_NAME}_model
        ${PROJECT_NAME}_radio
        ${PROJECT_NAME}_platform
)

# ====================================================
# Core sources
# ====================================================
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp"
)

add_library(${PROJECT_NAME}_core STATIC ${APP_SOURCES})
target_include_directories(${PROJECT_NAME}_core
    PUBLIC
        ${LVGL_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/logger
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view/screens
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/radio
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
)
target_compile_definitions(${PROJECT_NAME}_core
    PUBLIC
        APP_NAME="${APP_NAME}"
        APP_CONFIG_FILE="${APP_CONFIG_FILE}"
        APP_CMAKE_ASSETS_ROOT="${APP_ASSETS_ROOT}"
        APP_SOURCE_ASSETS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/assets"
        APP_FRAMEBUFFER_DEVICE="${APP_FRAMEBUFFER_DEVICE}"
        APP_DRM_DEVICE="${APP_DRM_DEVICE}"
        APP_DRM_CONNECTOR_ID=${APP_DRM_CONNECTOR_ID}
)
target_link_libraries(${PROJECT_NAME}_core
    PUBLIC
        lvgl
        logger
        ${PROJECT_NAME}_view
        ${PROJECT_NAME}_viewmodel
        ${PROJECT_NAME}_platform
        ${PROJECT_NAME}_radio
)

if(USE_DESKTOP)
    target_link_libraries(${PROJECT_NAME}_core PRIVATE PNG::PNG)
    if(TARGET SDL2::SDL2)
        target_link_libraries(${PROJECT_NAME}_core PRIVATE SDL2::SDL2)
    else()
        target_include_directories(${PROJECT_NAME}_core PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(${PROJECT_NAME}_core PRIVATE ${SDL2_LIBRARIES})
    endif()
endif()

# ====================================================
# main executable
# ====================================================
add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_NAME}_platform
        ${PROJECT_NAME}_core
        ${PROJECT_NAME}_model
        ${PROJECT_NAME}_radio
        ${PROJECT_NAME}_view
        ${PROJECT_NAME}_viewmodel
)

# ====================================================
# Automated Unit Tests
# ====================================================
if(BUILD_TESTING)
    enable_testing()

    add_executable(sub_file_parser_test tests/sub_file_parser_test.cpp)
    target_compile_definitions(sub_file_parser_test PRIVATE TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(sub_file_parser_test PRIVATE ${PROJECT_NAME}_model)
    add_test(NAME sub_file_parser COMMAND sub_file_parser_test)

    add_executable(subghz_protocols_test tests/subghz_protocols_test.cpp)
    target_link_libraries(subghz_protocols_test PRIVATE ${PROJECT_NAME}_model)
    add_test(NAME subghz_protocols COMMAND subghz_protocols_test)

    add_executable(cc1101_preset_test tests/cc1101_preset_test.cpp)
    target_link_libraries(cc1101_preset_test PRIVATE ${PROJECT_NAME}_radio ${PROJECT_NAME}_model)
    add_test(NAME cc1101_preset COMMAND cc1101_preset_test)

    add_executable(subghz_worker_test tests/subghz_worker_test.cpp)
    target_link_libraries(subghz_worker_test PRIVATE ${PROJECT_NAME}_radio ${PROJECT_NAME}_model)
    add_test(NAME subghz_worker COMMAND subghz_worker_test)
endif()

# Package deb
include(cmake/cm0-package.cmake)

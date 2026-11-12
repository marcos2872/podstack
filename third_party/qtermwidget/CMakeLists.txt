# SPDX-License-Identifier: GPL-2.0-or-later
#
# qtermwidget 2.0.0, vendored for PodStack.
#
# Upstream: https://github.com/lxqt/qtermwidget (tag 2.0.0)
# Local changes are documented in README.podstack.md:
#   - Qt 6.4 minimum (Ubuntu 24.04 ships Qt 6.4.2);
#   - built as a static library and linked into podstack-gui;
#   - lxqt2-build-tools, translations, pkg-config and devel installs removed.
#
cmake_minimum_required(VERSION 3.18.0 FATAL_ERROR)
project(qtermwidget LANGUAGES CXX)

include(GNUInstallDirs)
include(GenerateExportHeader)
include(CheckFunctionExists)
include(CheckIncludeFile)

option(QTERMWIDGET_USE_UTEMPTER "Uses libutempter on Linux or libulog on FreeBSD for login records." OFF)
option(USE_UTF8PROC "Use libutf8proc for better Unicode support." OFF)

set(QTERMWIDGET_VERSION_MAJOR "2")
set(QTERMWIDGET_VERSION_MINOR "0")
set(QTERMWIDGET_VERSION_PATCH "0")
set(QTERMWIDGET_VERSION "${QTERMWIDGET_VERSION_MAJOR}.${QTERMWIDGET_VERSION_MINOR}.${QTERMWIDGET_VERSION_PATCH}")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake")
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(QT_MINIMUM_VERSION "6.4.0")
find_package(Qt6Widgets "${QT_MINIMUM_VERSION}" REQUIRED)

if(USE_UTF8PROC)
    find_package(Utf8Proc REQUIRED)
endif()

if(APPLE)
    if(CMAKE_VERSION VERSION_GREATER 3.9)
        cmake_policy(SET CMP0068 NEW)
        set(CMAKE_BUILD_WITH_INSTALL_NAME_DIR ON)
    endif()
endif()

set(QTERMWIDGET_LIBRARY_NAME qtermwidget6)

set(SRCS
    lib/BlockArray.cpp
    lib/ColorScheme.cpp
    lib/Emulation.cpp
    lib/Filter.cpp
    lib/History.cpp
    lib/HistorySearch.cpp
    lib/KeyboardTranslator.cpp
    lib/konsole_wcwidth.cpp
    lib/kprocess.cpp
    lib/kpty.cpp
    lib/kptydevice.cpp
    lib/kptyprocess.cpp
    lib/Pty.cpp
    lib/qtermwidget.cpp
    lib/Screen.cpp
    lib/ScreenWindow.cpp
    lib/SearchBar.cpp
    lib/Session.cpp
    lib/ShellCommand.cpp
    lib/TerminalCharacterDecoder.cpp
    lib/TerminalDisplay.cpp
    lib/tools.cpp
    lib/Vt102Emulation.cpp
)

set(HDRS
    lib/Emulation.h
    lib/Filter.h
    lib/HistorySearch.h
    lib/kprocess.h
    lib/kptydevice.h
    lib/kptyprocess.h
    lib/Pty.h
    lib/qtermwidget.h
    lib/ScreenWindow.h
    lib/SearchBar.h
    lib/Session.h
    lib/TerminalDisplay.h
    lib/Vt102Emulation.h
)

set(UI
    lib/SearchBar.ui
)

set(KB_LAYOUT_DIR "${CMAKE_INSTALL_FULL_DATADIR}/podstack/${QTERMWIDGET_LIBRARY_NAME}/kb-layouts")
set(COLORSCHEMES_DIR "${CMAKE_INSTALL_FULL_DATADIR}/podstack/${QTERMWIDGET_LIBRARY_NAME}/color-schemes")
set(TRANSLATIONS_DIR "${CMAKE_INSTALL_FULL_DATADIR}/podstack/${QTERMWIDGET_LIBRARY_NAME}/translations")

# Os testes e o binário de desenvolvimento usam estes dados sem instalar em
# /usr. O fallback em tools.cpp procura ../share/podstack/qtermwidget6.
file(COPY lib/kb-layouts DESTINATION "${CMAKE_BINARY_DIR}/share/podstack/${QTERMWIDGET_LIBRARY_NAME}")
file(COPY lib/color-schemes DESTINATION "${CMAKE_BINARY_DIR}/share/podstack/${QTERMWIDGET_LIBRARY_NAME}")

CHECK_FUNCTION_EXISTS(updwtmpx HAVE_UPDWTMPX)

qt6_wrap_cpp(MOCS ${HDRS})
qt6_wrap_ui(UI_SRCS ${UI})

add_library(${QTERMWIDGET_LIBRARY_NAME} STATIC ${SRCS} ${MOCS} ${UI_SRCS})
target_link_libraries(${QTERMWIDGET_LIBRARY_NAME} PUBLIC Qt6::Widgets)
target_compile_definitions(${QTERMWIDGET_LIBRARY_NAME} PUBLIC QTERMWIDGET_STATIC_DEFINE)
set_target_properties(${QTERMWIDGET_LIBRARY_NAME} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

if(APPLE)
    target_compile_definitions(${QTERMWIDGET_LIBRARY_NAME}
        PRIVATE
            "HAVE_UTMPX"
            "UTMPX_COMPAT"
    )
endif()

if(HAVE_UPDWTMPX)
    target_compile_definitions(${QTERMWIDGET_LIBRARY_NAME}
        PRIVATE
            "HAVE_UPDWTMPX"
    )
endif()

if(QTERMWIDGET_USE_UTEMPTER)
    CHECK_INCLUDE_FILE(utempter.h HAVE_UTEMPTER)
    if(HAVE_UTEMPTER)
        target_compile_definitions(${QTERMWIDGET_LIBRARY_NAME} PRIVATE
            "HAVE_UTEMPTER"
        )
        find_library(UTEMPTER_LIB NAMES utempter ulog REQUIRED)
        target_link_libraries(${QTERMWIDGET_LIBRARY_NAME} ${UTEMPTER_LIB})
    endif()
endif()

if(UTF8PROC_FOUND)
    target_compile_definitions(${QTERMWIDGET_LIBRARY_NAME}
        PRIVATE
            "HAVE_UTF8PROC"
    )
    target_include_directories(${QTERMWIDGET_LIBRARY_NAME}
        INTERFACE
            ${UTF8PROC_INCLUDE_DIRS}
    )
    target_link_libraries(${QTERMWIDGET_LIBRARY_NAME}
        ${UTF8PROC_LIBRARIES}
    )
endif()

if(APPLE)
    set(CMAKE_SKIP_RPATH 1)
    set_target_properties(${QTERMWIDGET_LIBRARY_NAME} PROPERTIES INSTALL_NAME_DIR ${CMAKE_INSTALL_FULL_LIBDIR})
endif()

target_compile_definitions(${QTERMWIDGET_LIBRARY_NAME}
    PRIVATE
        "KB_LAYOUT_DIR=\"${KB_LAYOUT_DIR}\""
        "COLORSCHEMES_DIR=\"${COLORSCHEMES_DIR}\""
        "TRANSLATIONS_DIR=\"${TRANSLATIONS_DIR}\""
        "HAVE_POSIX_OPENPT"
        "HAVE_SYS_TIME_H"
)

generate_export_header(${QTERMWIDGET_LIBRARY_NAME}
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/lib/qtermwidget_export.h"
    BASE_NAME QTERMWIDGET
)

target_include_directories(${QTERMWIDGET_LIBRARY_NAME}
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/lib>"
)

# Runtime data only. The library itself is static and is linked into
# podstack-gui, so there is no shared object to install or conflict with.
install(DIRECTORY
    lib/kb-layouts/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/podstack/${QTERMWIDGET_LIBRARY_NAME}/kb-layouts"
    COMPONENT Runtime
    FILES_MATCHING PATTERN "*.keytab"
)
install(DIRECTORY
    lib/color-schemes/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/podstack/${QTERMWIDGET_LIBRARY_NAME}/color-schemes"
    COMPONENT Runtime
    FILES_MATCHING PATTERN "*.*schem*"
)

configure_file(
    "${PROJECT_SOURCE_DIR}/lib/qtermwidget_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/lib/qtermwidget_version.h"
    @ONLY
)

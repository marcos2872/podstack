cmake_minimum_required(VERSION 3.24)

project(PodStack VERSION 0.1.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Binários ficam na raiz do build (build/<preset>/), como scripts e testes esperam.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Qml
    Quick
    QuickControls2
    QuickWidgets
    Network
    Concurrent
    DBus
)
find_package(CURL REQUIRED)
option(PODSTACK_USE_VENDORED_QTERMWIDGET
    "Compila o qtermwidget vendorizado (Ubuntu 24.04/Qt 6.4)" OFF)
if(PODSTACK_USE_VENDORED_QTERMWIDGET)
    add_subdirectory(third_party/qtermwidget)
else()
    find_package(qtermwidget6 REQUIRED)
endif()
find_package(LibArchive REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)

find_library(CRYPT_LIBRARY crypt)
if(NOT CRYPT_LIBRARY)
    message(FATAL_ERROR
        "libcrypt não encontrada. Instale libxcrypt-devel (Fedora) ou libcrypt-dev (Debian/Ubuntu).")
endif()

qt_standard_project_setup(REQUIRES 6.4)

# Chave pública Ed25519 embutida no updater. Vazia mantém o default null em
# Settings.cpp e permite configurar updates.publicKey em runtime.
set(PODSTACK_UPDATE_PUBLIC_KEY "" CACHE STRING "Chave publica Ed25519 embutida no updater")

# ---------------------------------------------------------------------------
# Módulos: cada pasta define seus próprios targets (ver AGENTS.md §7).
# ---------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/podman)
add_subdirectory(src/machines)
add_subdirectory(src/k8s)
add_subdirectory(src/daemon)
add_subdirectory(src/helper)
add_subdirectory(src/cli)
add_subdirectory(src/gui)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS podstack podstackd podstack-gui RUNTIME DESTINATION bin)

install(FILES
    packaging/systemd/podstack.socket
    packaging/systemd/podstack.service
    DESTINATION lib/systemd/user
)

install(FILES packaging/systemd-system/podstack-helper.service
    DESTINATION lib/systemd/system
)

install(TARGETS podstack-helper RUNTIME DESTINATION libexec/podstack)
install(TARGETS podstack-update-exec RUNTIME DESTINATION libexec/podstack)
install(FILES packaging/tmpfiles.d/podstack.conf DESTINATION lib/tmpfiles.d)
install(FILES packaging/logrotate/podstack DESTINATION /etc/logrotate.d)

install(FILES packaging/polkit/dev.podstack.helper.policy
    DESTINATION share/polkit-1/actions
)

install(FILES packaging/dbus/dev.podstack.Helper1.conf
    DESTINATION share/dbus-1/system.d
)

install(FILES packaging/dev.podstack.PodStack.desktop
    DESTINATION share/applications
)

install(FILES packaging/dev.podstack.PodStack.metainfo.xml
    DESTINATION share/metainfo
)

install(FILES LICENSE NOTICE
    DESTINATION share/licenses/podstack
)

install(FILES
    third_party/qtermwidget/LICENSE
    third_party/qtermwidget/LICENSE.LGPL2+
    third_party/qtermwidget/LICENSE.BSD-3-clause
    DESTINATION share/licenses/podstack/qtermwidget
)

install(FILES assets/podstack-icon.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME dev.podstack.PodStack.svg
)

foreach(icon_size IN ITEMS 16 24 32 48 64 128 256 512)
    install(FILES assets/png/podstack-icon-${icon_size}.png
        DESTINATION share/icons/hicolor/${icon_size}x${icon_size}/apps
        RENAME dev.podstack.PodStack.png
    )
endforeach()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

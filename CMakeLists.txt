cmake_minimum_required(VERSION 3.24)

project(PodStack VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.10 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Qml
    Quick
    QuickControls2
    Network
    Concurrent
    DBus
)
find_package(CURL REQUIRED)
find_package(qtermwidget6 REQUIRED)
find_package(LibArchive REQUIRED)
find_package(SQLite3 REQUIRED)

find_library(CRYPT_LIBRARY crypt)
if(NOT CRYPT_LIBRARY)
    message(FATAL_ERROR
        "libcrypt não encontrada. Instale libxcrypt-devel (Fedora) ou libcrypt-dev (Debian/Ubuntu).")
endif()

qt_standard_project_setup(REQUIRES 6.10)

# ---------------------------------------------------------------------------
# Core: errors, paths, config, logging, JSON-RPC client
# ---------------------------------------------------------------------------
add_library(podstack_core STATIC
    src/core/DiskUsage.cpp
    src/core/Error.cpp
    src/core/Ids.cpp
    src/core/Paths.cpp
    src/core/Config.cpp
    src/core/Logging.cpp
    src/core/Proxy.cpp
    src/core/RpcClient.cpp
    src/core/RpcStream.cpp
)
target_include_directories(podstack_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(podstack_core PUBLIC Qt6::Core Qt6::Network)
target_compile_definitions(podstack_core PUBLIC PODSTACK_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# Podman: libcurl over the Unix socket + typed client
# ---------------------------------------------------------------------------
add_library(podstack_podman STATIC
    src/podman/UnixHttpClient.cpp
    src/podman/PodmanClient.cpp
)
target_link_libraries(podstack_podman PUBLIC podstack_core CURL::libcurl LibArchive::LibArchive)

# ---------------------------------------------------------------------------
# Machines: registry de distros, download e cache de imagens cloud
# ---------------------------------------------------------------------------
add_library(podstack_machines STATIC
    src/machines/DistroRegistry.cpp
    src/machines/HttpDownloader.cpp
    src/machines/CloudImageCache.cpp
    src/machines/CloudInit.cpp
    src/machines/SshConfig.cpp
    src/machines/MachineMeta.cpp
    src/machines/MachineStore.cpp
    src/machines/QemuCommand.cpp
    src/machines/QmpClient.cpp
    src/machines/QemuRuntime.cpp
    src/machines/MachineManager.cpp
)
target_include_directories(podstack_machines PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(podstack_machines PUBLIC
    podstack_core
    Qt6::Core
    Qt6::Network
    CURL::libcurl
    LibArchive::LibArchive
    SQLite::SQLite3
    ${CRYPT_LIBRARY}
)

# ---------------------------------------------------------------------------
# Daemon
# ---------------------------------------------------------------------------
add_executable(podstackd
    src/daemon/main.cpp
    src/daemon/Daemon.cpp
    src/daemon/RpcServer.cpp
    src/daemon/BuildStore.cpp
    src/daemon/ComposeManager.cpp
    src/daemon/SetupManager.cpp
    src/daemon/HelperClient.cpp
    src/daemon/StatsCollector.cpp
)
target_link_libraries(podstackd PRIVATE
    podstack_podman
    podstack_machines
    Qt6::Concurrent
    Qt6::DBus
    SQLite::SQLite3
)

# ---------------------------------------------------------------------------
# Helper privilegiado (system D-Bus + polkit, métodos allowlisted)
# ---------------------------------------------------------------------------
add_executable(podstack-helper
    src/helper/main.cpp
    src/helper/HelperService.cpp
    src/helper/Firewall.cpp
    src/helper/Polkit.cpp
)
target_include_directories(podstack-helper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(podstack-helper PRIVATE PODSTACK_VERSION="${PROJECT_VERSION}")
target_link_libraries(podstack-helper PRIVATE podstack_core Qt6::DBus)

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
add_executable(podstack
    src/cli/main.cpp
)
target_link_libraries(podstack PRIVATE podstack_core Qt6::Network)

# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------
add_library(podstack_gui_lib STATIC
    src/gui/Autostart.cpp
    src/gui/Backend.cpp
    src/gui/ContainerListModel.cpp
    src/gui/Notifications.cpp
    src/gui/SingleInstance.cpp
    src/gui/StreamClient.cpp
    src/gui/TerminalWindow.cpp
    src/gui/Tray.cpp
)
target_include_directories(podstack_gui_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(podstack_gui_lib PUBLIC
    podstack_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    Qt6::DBus
    qtermwidget6
)

qt_add_executable(podstack-gui
    src/gui/main.cpp
)

set_source_files_properties(src/gui/qml/Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

file(GLOB PODSTACK_UI_ICONS CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    assets/icons/*.svg)

file(GLOB PODSTACK_TRAY_ICONS CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    assets/png/tray/*.png)

qt_add_qml_module(podstack-gui
    URI PodStack
    VERSION 1.0
    QML_FILES
        src/gui/qml/Main.qml
        src/gui/qml/ActivityMonitorWindow.qml
        src/gui/qml/Theme.qml
        src/gui/qml/AppIcon.qml
        src/gui/qml/AppSelect.qml
        src/gui/qml/AppDialog.qml
        src/gui/qml/AppFooter.qml
        src/gui/qml/AppBadge.qml
        src/gui/qml/NavItem.qml
        src/gui/qml/Sparkline.qml
        src/gui/qml/ViewHeader.qml
        src/gui/qml/ActionButton.qml
        src/gui/qml/FormField.qml
        src/gui/qml/FormCheck.qml
        src/gui/qml/LogsView.qml
        src/gui/qml/StatsView.qml
        src/gui/qml/Inspector.qml
        src/gui/qml/FilesView.qml
        src/gui/qml/CatalogList.qml
        src/gui/qml/ContainersPage.qml
        src/gui/qml/ImagesView.qml
        src/gui/qml/VolumesView.qml
        src/gui/qml/NetworksView.qml
        src/gui/qml/PodsView.qml
        src/gui/qml/MachinesPage.qml
        src/gui/qml/MachineInspector.qml
        src/gui/qml/ComposeView.qml
        src/gui/qml/BuildsView.qml
        src/gui/qml/Toast.qml
        src/gui/qml/SetupWindow.qml
        src/gui/qml/SetupRail.qml
        src/gui/qml/SetupCheckRow.qml
    RESOURCES
        assets/fonts/Inter-Regular.ttf
        assets/fonts/Inter-Medium.ttf
        assets/fonts/Inter-SemiBold.ttf
        assets/fonts/Inter-Bold.ttf
        assets/podstack-capsule.svg
        assets/podstack-capsule-mark.svg
        assets/podstack-icon.svg
        ${PODSTACK_UI_ICONS}
        ${PODSTACK_TRAY_ICONS}
)

target_link_libraries(podstack-gui PRIVATE
    podstack_gui_lib
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

set_target_properties(podstack-gui PROPERTIES
    OUTPUT_NAME podstack-gui
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

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

install(FILES packaging/polkit/dev.podstack.helper.policy
    DESTINATION share/polkit-1/actions
)

install(FILES packaging/dbus/dev.podstack.Helper1.conf
    DESTINATION share/dbus-1/system.d
)

install(FILES packaging/dev.podstack.PodStack.desktop
    DESTINATION share/applications
)

install(FILES assets/podstack-icon.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME dev.podstack.PodStack.svg
)

install(FILES assets/png/podstack-icon-256.png
    DESTINATION share/icons/hicolor/256x256/apps
    RENAME dev.podstack.PodStack.png
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

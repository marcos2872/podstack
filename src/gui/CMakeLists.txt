# ---------------------------------------------------------------------------
# GUI: shell Qt Widgets + backend + módulo QML
# ---------------------------------------------------------------------------
add_library(podstack_gui_lib STATIC
    app/Autostart.cpp
    app/MainWindow.cpp
    app/SingleInstance.cpp
    app/Translation.cpp
    app/UiState.cpp
    app/UnixSignals.cpp
    app/notifications/Notifications.cpp
    app/terminal/ExternalTerminal.cpp
    app/tray/Tray.cpp
    controllers/Backend.cpp
    controllers/BuildController.cpp
    controllers/ContainerController.cpp
    controllers/ContainerDetailController.cpp
    controllers/ComposeController.cpp
    controllers/CatalogController.cpp
    controllers/ControllerBase.cpp
    controllers/ImageController.cpp
    controllers/K8sController.cpp
    controllers/NetworkController.cpp
    controllers/PodController.cpp
    controllers/VolumeController.cpp
    models/ContainerListModel.cpp
    windows/ContainerDetailDialog.cpp
    windows/MachineDetailDialog.cpp
    windows/TerminalWindow.cpp
    windows/terminal/TerminalView.cpp
)
target_include_directories(podstack_gui_lib PUBLIC ${PROJECT_SOURCE_DIR}/src)
qt_add_resources(podstack_gui_lib "translations"
    PREFIX "/i18n"
    BASE "${PROJECT_SOURCE_DIR}/translations"
    FILES "${PROJECT_SOURCE_DIR}/translations/podstack_en_US.qm"
)
target_link_libraries(podstack_gui_lib PUBLIC
    podstack_core
    podstack_helper_client
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    Qt6::DBus
    Qt6::QuickWidgets
    qtermwidget6
)

qt_add_executable(podstack-gui
    main.cpp
)

qt_add_library(podstack_qml STATIC)

set_source_files_properties(qml/Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

file(GLOB PODSTACK_UI_ICONS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/assets/icons/*.svg")

file(GLOB PODSTACK_TRAY_ICONS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/assets/png/tray/*.png")

# Os assets ficam fora de src/gui, então o alias do recurso precisa preservar
# o caminho usado pelo QML (qrc:/qt/qml/PodStack/assets/...).
set(PODSTACK_QML_ASSETS
    assets/fonts/Inter-Regular.ttf
    assets/fonts/Inter-Medium.ttf
    assets/fonts/Inter-SemiBold.ttf
    assets/fonts/Inter-Bold.ttf
    assets/podstack-capsule.svg
    assets/podstack-capsule-mark.svg
    assets/podstack-icon.svg
)
foreach(asset IN LISTS PODSTACK_QML_ASSETS)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/${asset}"
        PROPERTIES QT_RESOURCE_ALIAS "${asset}")
endforeach()
foreach(asset IN LISTS PODSTACK_UI_ICONS PODSTACK_TRAY_ICONS)
    file(RELATIVE_PATH asset_alias "${PROJECT_SOURCE_DIR}" "${asset}")
    set_source_files_properties("${asset}"
        PROPERTIES QT_RESOURCE_ALIAS "${asset_alias}")
endforeach()

qt_add_qml_module(podstack_qml
    URI PodStack
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        qml/Main.qml
        qml/ActivityMonitorWindow.qml
        qml/Theme.qml
        qml/AppIcon.qml
        qml/AppSelect.qml
        qml/AppDialog.qml
        qml/AppFooter.qml
        qml/AppBadge.qml
        qml/NavItem.qml
        qml/Sparkline.qml
        qml/ViewHeader.qml
        qml/ActionButton.qml
        qml/FormField.qml
        qml/PathField.qml
        qml/FormCheck.qml
        qml/SettingsRow.qml
        qml/LogsView.qml
        qml/SystemLogsDialog.qml
        qml/Inspector.qml
        qml/ContainerDetail.qml
        qml/FilesView.qml
        qml/CatalogList.qml
        qml/SelectionState.qml
        qml/ContainersPage.qml
        qml/ImagesView.qml
        qml/VolumesView.qml
        qml/NetworksView.qml
        qml/PodsView.qml
        qml/MachinesPage.qml
        qml/KubernetesPage.qml
        qml/MachineInspector.qml
        qml/MachineFilesView.qml
        qml/ComposeView.qml
        qml/BuildsView.qml
        qml/Toast.qml
        qml/SetupWindow.qml
        qml/SetupRail.qml
        qml/SetupCheckRow.qml
    RESOURCES
        ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Medium.ttf
        ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-SemiBold.ttf
        ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Bold.ttf
        ${PROJECT_SOURCE_DIR}/assets/podstack-capsule.svg
        ${PROJECT_SOURCE_DIR}/assets/podstack-capsule-mark.svg
        ${PROJECT_SOURCE_DIR}/assets/podstack-icon.svg
        ${PODSTACK_UI_ICONS}
        ${PODSTACK_TRAY_ICONS}
)

target_link_libraries(podstack_qml PUBLIC
    Qt6::Qml
    Qt6::Quick
)

target_link_libraries(podstack-gui PRIVATE
    podstack_gui_lib
    podstack_qml
    podstack_qmlplugin
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

find_package(Qt6 6.4 REQUIRED COMPONENTS Test QuickTest)

qt_add_executable(test_core test_core.cpp ../src/daemon/builds/BuildStore.cpp
    ../src/daemon/builds/ManifestBuilder.cpp)
target_link_libraries(test_core PRIVATE podstack_core SQLite::SQLite3 Qt6::Test)
add_test(NAME core COMMAND test_core)

qt_add_executable(test_podman test_podman.cpp)
target_link_libraries(test_podman PRIVATE podstack_podman Qt6::Test)
add_test(NAME podman COMMAND test_podman)

qt_add_executable(test_contract test_contract.cpp MockPodmanServer.cpp)
target_link_libraries(test_contract PRIVATE podstack_podman Qt6::Test)
add_test(NAME contract COMMAND test_contract)

qt_add_executable(test_http_proxy test_http_proxy.cpp ../src/daemon/domains/HttpProxy.cpp)
target_link_libraries(test_http_proxy PRIVATE podstack_core podstack_podman Qt6::Network
    Qt6::Test)
add_test(NAME http_proxy COMMAND test_http_proxy)

qt_add_executable(test_dns test_dns.cpp ../src/daemon/domains/DnsServer.cpp)
target_link_libraries(test_dns PRIVATE podstack_core Qt6::Network Qt6::Test)
add_test(NAME dns COMMAND test_dns)

qt_add_executable(test_caddy test_caddy.cpp ../src/daemon/domains/CaddyProxy.cpp)
target_link_libraries(test_caddy PRIVATE podstack_core Qt6::Network Qt6::Test)
add_test(NAME caddy COMMAND test_caddy)

qt_add_executable(test_rpc test_rpc.cpp MockPodmanServer.cpp MockHttpServer.cpp
    ../src/daemon/compose/ComposeManager.cpp)
target_link_libraries(test_rpc PRIVATE podstack_core podstack_machines podstack_k8s
    podstack_podman Qt6::Test
    Qt6::Network)
add_dependencies(test_rpc podstackd)
add_test(NAME rpc COMMAND test_rpc)

qt_add_executable(test_cli test_cli.cpp MockPodmanServer.cpp MockHttpServer.cpp)
target_link_libraries(test_cli PRIVATE podstack_machines Qt6::Test Qt6::Network)
add_dependencies(test_cli podstack podstackd)
add_test(NAME cli COMMAND test_cli)

qt_add_executable(test_gui test_gui.cpp MockPodmanServer.cpp)
target_link_libraries(test_gui PRIVATE podstack_gui_lib podstack_machines podstack_k8s
    Qt6::Test Qt6::Network)
add_dependencies(test_gui podstackd)
add_test(NAME gui COMMAND test_gui)

qt_add_executable(test_terminal test_terminal.cpp)
target_link_libraries(test_terminal PRIVATE podstack_gui_lib podstack_qml podstack_qmlplugin
    podstack_qml_design podstack_qml_designplugin podstack_qml_pages podstack_qml_pagesplugin
    podstack_qml_features podstack_qml_featuresplugin
    podstack_qmlplugin_init
    podstack_qml_designplugin_init
    podstack_qml_pagesplugin_init
    podstack_qml_featuresplugin_init
    Qt6::Test Qt6::Widgets)
qt_import_qml_plugins(test_terminal)
add_test(NAME terminal COMMAND test_terminal)
set_tests_properties(terminal PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(test_tray test_tray.cpp)
target_link_libraries(test_tray PRIVATE podstack_gui_lib Qt6::Test Qt6::Widgets)
qt_add_resources(test_tray "tray_icons"
    PREFIX "/qt/qml/PodStack"
    BASE ".."
    FILES
        ../assets/png/tray/podstack-tray-16.png
        ../assets/png/tray/podstack-tray-22.png
        ../assets/png/tray/podstack-tray-24.png
        ../assets/png/tray/podstack-tray-32.png
        ../assets/png/tray/podstack-tray-48.png
        ../assets/png/tray/podstack-tray-64.png
)
add_test(NAME tray COMMAND test_tray)
set_tests_properties(tray PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(test_single_instance test_single_instance.cpp)
target_link_libraries(test_single_instance PRIVATE podstack_gui_lib Qt6::Test)
add_test(NAME single_instance COMMAND test_single_instance)

qt_add_executable(test_autostart test_autostart.cpp)
target_link_libraries(test_autostart PRIVATE podstack_gui_lib Qt6::Test)
add_test(NAME autostart COMMAND test_autostart)

qt_add_executable(test_stats test_stats.cpp ../src/daemon/stats/StatsCollector.cpp)
target_link_libraries(test_stats PRIVATE podstack_podman Qt6::Test)
add_test(NAME stats COMMAND test_stats)

qt_add_executable(test_notifications test_notifications.cpp)
target_link_libraries(test_notifications PRIVATE podstack_gui_lib Qt6::Test)
add_test(NAME notifications COMMAND test_notifications)

qt_add_executable(test_machines test_machines.cpp MockHttpServer.cpp MockQmpServer.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/builds/BuildStore.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/stats/StatsCollector.cpp)
target_link_libraries(test_machines PRIVATE podstack_machines podstack_k8s podstack_podman
    LibArchive::LibArchive Qt6::Test)
add_test(NAME machines COMMAND test_machines)

qt_add_executable(test_logging test_logging.cpp)
target_link_libraries(test_logging PRIVATE podstack_core Qt6::Test)
add_test(NAME logging COMMAND test_logging)

qt_add_executable(test_daemon_bootstrap test_daemon_bootstrap.cpp)
target_link_libraries(test_daemon_bootstrap PRIVATE podstack_core Qt6::Test)
add_test(NAME daemon_bootstrap COMMAND test_daemon_bootstrap)

qt_add_executable(test_update_service test_update_service.cpp
    ${CMAKE_SOURCE_DIR}/src/helper/updates/UpdateService.cpp)
target_link_libraries(test_update_service PRIVATE podstack_core Qt6::Test)
add_test(NAME update_service COMMAND test_update_service)

qt_add_executable(test_update_runner test_update_runner.cpp MockHttpServer.cpp)
target_link_libraries(test_update_runner PRIVATE podstack_core Qt6::Network Qt6::Test)
add_test(NAME update_runner COMMAND test_update_runner)

qt_add_executable(test_setup test_setup.cpp MockPodmanServer.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/setup/SetupManager.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/docker/DockerInventory.cpp
    ${CMAKE_SOURCE_DIR}/src/core/helper/HelperClient.cpp
    ${CMAKE_SOURCE_DIR}/src/helper/ca/CaTrust.cpp
    ${CMAKE_SOURCE_DIR}/src/helper/dns/DnsZone.cpp
    ${CMAKE_SOURCE_DIR}/src/helper/ports/PortBinding.cpp
    ${CMAKE_SOURCE_DIR}/src/helper/ports/Firewall.cpp
    ${CMAKE_SOURCE_DIR}/src/helper/HelperService.cpp
    ${CMAKE_SOURCE_DIR}/src/helper/updates/UpdateService.cpp
    ${CMAKE_SOURCE_DIR}/src/helper/polkit/Polkit.cpp)
target_link_libraries(test_setup PRIVATE podstack_podman Qt6::Concurrent Qt6::DBus
    SQLite::SQLite3 Qt6::Test)
add_test(NAME setup COMMAND test_setup)

add_test(NAME package_scriptlets
    COMMAND ${CMAKE_SOURCE_DIR}/tests/packaging/test-scriptlets.sh ${CMAKE_SOURCE_DIR})
set_tests_properties(package_scriptlets PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME update_exec
    COMMAND ${CMAKE_SOURCE_DIR}/tests/packaging/test-update-exec.sh
            $<TARGET_FILE:podstack-update-exec>)

qt_add_executable(test_qml qml/main.cpp)
target_link_libraries(test_qml PRIVATE podstack_qml podstack_qmlplugin
    podstack_qml_design podstack_qml_designplugin
    podstack_qml_pages podstack_qml_pagesplugin
    podstack_qml_features podstack_qml_featuresplugin
    podstack_qmlplugin_init
    podstack_qml_designplugin_init
    podstack_qml_pagesplugin_init
    podstack_qml_featuresplugin_init
    Qt6::QuickTest Qt6::Test)
qt_import_qml_plugins(test_qml)
add_test(NAME qml COMMAND test_qml -input ${CMAKE_CURRENT_SOURCE_DIR}/qml -import qrc:/qt/qml)
set_tests_properties(qml PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

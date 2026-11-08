find_package(Qt6 6.10 REQUIRED COMPONENTS Test)

qt_add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE podstack_core Qt6::Test)
add_test(NAME core COMMAND test_core)

qt_add_executable(test_podman test_podman.cpp)
target_link_libraries(test_podman PRIVATE podstack_podman Qt6::Test)
add_test(NAME podman COMMAND test_podman)

qt_add_executable(test_contract test_contract.cpp MockPodmanServer.cpp)
target_link_libraries(test_contract PRIVATE podstack_podman Qt6::Test)
add_test(NAME contract COMMAND test_contract)

qt_add_executable(test_rpc test_rpc.cpp MockPodmanServer.cpp)
target_link_libraries(test_rpc PRIVATE podstack_core Qt6::Test Qt6::Network)
add_dependencies(test_rpc podstackd)
add_test(NAME rpc COMMAND test_rpc)

qt_add_executable(test_cli test_cli.cpp MockPodmanServer.cpp)
target_link_libraries(test_cli PRIVATE Qt6::Test Qt6::Network)
add_dependencies(test_cli podstack podstackd)
add_test(NAME cli COMMAND test_cli)

qt_add_executable(test_gui test_gui.cpp MockPodmanServer.cpp)
target_link_libraries(test_gui PRIVATE podstack_gui_lib Qt6::Test Qt6::Network)
add_dependencies(test_gui podstackd)
add_test(NAME gui COMMAND test_gui)

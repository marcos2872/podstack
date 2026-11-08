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

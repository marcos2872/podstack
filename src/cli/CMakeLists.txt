# ---------------------------------------------------------------------------
# CLI (podstack)
# ---------------------------------------------------------------------------
add_executable(podstack
    main.cpp
    Internal.cpp
    commands/builds.cpp
    commands/compose.cpp
    commands/config.cpp
    commands/containers.cpp
    commands/images.cpp
    commands/kube.cpp
    commands/machines.cpp
    commands/networks.cpp
    commands/pods.cpp
    commands/setup.cpp
    commands/system.cpp
    commands/volumes.cpp
)
target_link_libraries(podstack PRIVATE podstack_core podstack_helper_client Qt6::Network Qt6::DBus)

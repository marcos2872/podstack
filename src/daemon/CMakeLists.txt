# ---------------------------------------------------------------------------
# Daemon (podstackd)
# ---------------------------------------------------------------------------
add_executable(podstackd
    main.cpp
    Daemon.cpp
    rpc/RpcServer.cpp
    rpc/Internal.cpp
    rpc/handlers/builds.cpp
    rpc/handlers/compose.cpp
    rpc/handlers/containers.cpp
    rpc/handlers/images.cpp
    rpc/handlers/k8s.cpp
    rpc/handlers/machines.cpp
    rpc/handlers/networks.cpp
    rpc/handlers/pods.cpp
    rpc/handlers/settings.cpp
    rpc/handlers/setup.cpp
    rpc/handlers/system.cpp
    rpc/handlers/volumes.cpp
    builds/BuildStore.cpp
    builds/ManifestBuilder.cpp
    compose/ComposeManager.cpp
    setup/SetupManager.cpp
    updates/UpdateChecker.cpp
    docker/DockerInventory.cpp
    stats/StatsCollector.cpp
    domains/HttpProxy.cpp
    domains/DnsServer.cpp
    domains/CaddyProxy.cpp
)
target_link_libraries(podstackd PRIVATE
    podstack_podman
    podstack_machines
    podstack_k8s
    podstack_helper_client
    Qt6::Concurrent
    Qt6::DBus
    SQLite::SQLite3
)

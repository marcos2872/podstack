# ---------------------------------------------------------------------------
# Core: erros, paths, config, logging, JSON-RPC client
# ---------------------------------------------------------------------------
add_library(podstack_core STATIC
    config/Config.cpp
    config/Paths.cpp
    config/Settings.cpp
    error/Error.cpp
    logging/Logging.cpp
    logging/LogReader.cpp
    logging/LogRotation.cpp
    logging/LogWriter.cpp
    logging/Redaction.cpp
    rpc/RpcClient.cpp
    rpc/RpcStream.cpp
    system/DiskSpace.cpp
    system/DiskUsage.cpp
    system/Emulation.cpp
    system/Proxy.cpp
    update/UpdateManifest.cpp
    update/UpdateRunner.cpp
    util/Ids.cpp
)
target_include_directories(podstack_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(podstack_core PUBLIC Qt6::Core Qt6::Network OpenSSL::Crypto)
target_compile_definitions(podstack_core PUBLIC
    PODSTACK_VERSION="${PROJECT_VERSION}"
    PODSTACK_UPDATE_PUBLIC_KEY="${PODSTACK_UPDATE_PUBLIC_KEY}"
    QT_MESSAGELOGCONTEXT
)

# ---------------------------------------------------------------------------
# Cliente do helper privilegiado (D-Bus de sistema). Fica no core para que
# daemon, CLI e GUI conversem com o helper sem depender do módulo daemon.
# ---------------------------------------------------------------------------
add_library(podstack_helper_client STATIC
    helper/HelperClient.cpp
)
target_include_directories(podstack_helper_client PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(podstack_helper_client PUBLIC Qt6::Core Qt6::DBus)

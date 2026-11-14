# ---------------------------------------------------------------------------
# Machines: registry de distros, imagens cloud, QEMU/KVM e operações
# ---------------------------------------------------------------------------
add_library(podstack_machines STATIC
    MachineManager.cpp
    model/MachineMeta.cpp
    model/MachineStore.cpp
    provision/CloudImageCache.cpp
    provision/CloudInit.cpp
    provision/DistroRegistry.cpp
    provision/HttpDownloader.cpp
    provision/SshConfig.cpp
    runtime/ProcessScope.cpp
    runtime/QemuCommand.cpp
    runtime/QemuRuntime.cpp
    runtime/QmpClient.cpp
    runtime/VirtiofsdProcess.cpp
    operations/MachineArchive.cpp
    operations/MachineFiles.cpp
    operations/MachineStats.cpp
    operations/SerialPorts.cpp
    operations/UsbDevices.cpp
)
target_include_directories(podstack_machines PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(podstack_machines PUBLIC
    podstack_core
    Qt6::Core
    Qt6::Network
    CURL::libcurl
    LibArchive::LibArchive
    SQLite::SQLite3
    ${CRYPT_LIBRARY}
)

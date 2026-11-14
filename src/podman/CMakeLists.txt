# ---------------------------------------------------------------------------
# Podman: libcurl sobre o Unix socket + client tipado
# ---------------------------------------------------------------------------
add_library(podstack_podman STATIC
    PodmanClient.cpp
    transport/UnixHttpClient.cpp
    client/Internal.cpp
    client/build.cpp
    client/containers.cpp
    client/exec.cpp
    client/files.cpp
    client/images.cpp
    client/networks.cpp
    client/pods.cpp
    client/streams.cpp
    client/system.cpp
    client/types.cpp
    client/volumes.cpp
)
target_link_libraries(podstack_podman PUBLIC podstack_core CURL::libcurl LibArchive::LibArchive)

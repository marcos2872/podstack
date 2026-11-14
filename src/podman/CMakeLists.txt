# ---------------------------------------------------------------------------
# Podman: libcurl sobre o Unix socket + client tipado
# ---------------------------------------------------------------------------
add_library(podstack_podman STATIC
    PodmanClient.cpp
    transport/UnixHttpClient.cpp
)
target_link_libraries(podstack_podman PUBLIC podstack_core CURL::libcurl LibArchive::LibArchive)

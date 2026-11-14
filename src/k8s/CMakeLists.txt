# ---------------------------------------------------------------------------
# Kubernetes: VM dedicada + k3s (M5)
# ---------------------------------------------------------------------------
add_library(podstack_k8s STATIC
    K8sManager.cpp
    cluster/K8sCluster.cpp
    store/K8sStore.cpp
)
target_include_directories(podstack_k8s PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(podstack_k8s PUBLIC podstack_machines Qt6::Core SQLite::SQLite3)

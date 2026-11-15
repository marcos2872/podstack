# ---------------------------------------------------------------------------
# Helper privilegiado (system D-Bus + polkit, métodos allowlisted)
# ---------------------------------------------------------------------------
add_executable(podstack-helper
    main.cpp
    HelperService.cpp
    ca/CaTrust.cpp
    dns/DnsZone.cpp
    ports/PortBinding.cpp
    ports/Firewall.cpp
    polkit/Polkit.cpp
    updates/UpdateService.cpp
)
target_include_directories(podstack-helper PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(podstack-helper PRIVATE PODSTACK_VERSION="${PROJECT_VERSION}")
target_link_libraries(podstack-helper PRIVATE podstack_core Qt6::DBus)

add_executable(podstack-update-exec
    updates/UpdateExec.cpp
)
target_link_libraries(podstack-update-exec PRIVATE podstack_core)

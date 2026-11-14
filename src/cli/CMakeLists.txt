# ---------------------------------------------------------------------------
# CLI (podstack)
# ---------------------------------------------------------------------------
add_executable(podstack
    main.cpp
)
target_link_libraries(podstack PRIVATE podstack_core podstack_helper_client Qt6::Network Qt6::DBus)

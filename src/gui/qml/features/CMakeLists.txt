# ---------------------------------------------------------------------------
# PodStack.Features — onboarding, logs do sistema e monitor de atividade
# ---------------------------------------------------------------------------
qt_add_library(podstack_qml_features STATIC)

qt_add_qml_module(podstack_qml_features
    URI PodStack.Features
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    IMPORTS PodStack.Design
    QML_FILES
        setup/SetupWindow.qml
        setup/SetupRail.qml
        setup/SetupCheckRow.qml
        system-logs/SystemLogsDialog.qml
        activity/ActivityMonitorWindow.qml
)

target_link_libraries(podstack_qml_features PUBLIC podstack_qml_design)

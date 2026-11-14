# ---------------------------------------------------------------------------
# PodStack.Pages — telas do app
# ---------------------------------------------------------------------------
qt_add_library(podstack_qml_pages STATIC)

qt_add_qml_module(podstack_qml_pages
    URI PodStack.Pages
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    IMPORTS PodStack.Design
    QML_FILES
        containers/ContainersPage.qml
        containers/detail/ContainerDetail.qml
        images/ImagesPage.qml
        volumes/VolumesPage.qml
        networks/NetworksPage.qml
        pods/PodsPage.qml
        machines/MachinesPage.qml
        machines/MachineInspector.qml
        machines/MachineFilesView.qml
        compose/ComposePage.qml
        builds/BuildsPage.qml
        kubernetes/KubernetesPage.qml
)

target_link_libraries(podstack_qml_pages PUBLIC podstack_qml_design)

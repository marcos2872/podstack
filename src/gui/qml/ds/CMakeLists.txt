# ---------------------------------------------------------------------------
# PodStack.Design — design system (tokens, primitivas e padrões)
# ---------------------------------------------------------------------------
qt_add_library(podstack_qml_design STATIC)

set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(podstack_qml_design
    URI PodStack.Design
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        Theme.qml
        primitives/AppButton.qml
        primitives/AppIcon.qml
        primitives/AppBadge.qml
        primitives/AppModal.qml
        primitives/AppSelect.qml
        primitives/AppInput.qml
        primitives/AppCheck.qml
        primitives/AppPathField.qml
        primitives/AppToast.qml
        primitives/AppSparkline.qml
        primitives/AppTabs.qml
        primitives/AppConfirmDialog.qml
        primitives/AppSpinner.qml
        primitives/AppTooltip.qml
        primitives/AppSwitch.qml
        primitives/AppIconButton.qml
        primitives/AppSearchField.qml
        patterns/AppViewHeader.qml
        patterns/AppFooter.qml
        patterns/AppNavItem.qml
        patterns/AppSettingsRow.qml
        patterns/AppCatalogList.qml
        patterns/AppInspector.qml
        patterns/AppLogView.qml
        patterns/AppFileList.qml
        patterns/SelectionState.qml
        patterns/AppEmptyState.qml
        patterns/AppErrorBanner.qml
        patterns/AppStatCard.qml
        patterns/AppSection.qml
)

target_link_libraries(podstack_qml_design PUBLIC Qt6::Qml Qt6::Quick)

pragma Singleton
import QtQuick

// Design tokens do PodStack (ver poc/ui/DESIGN-SYSTEM.md).
// Singleton: `Theme.dark` alterna o tema em toda a UI de forma reativa.
QtObject {
    id: root

    property bool dark: true

    // Superfícies
    readonly property color bgBase: dark ? "#080a0f" : "#f5f6fa"
    readonly property color bgSidebar: dark ? "#0a0c11" : "#ffffff"
    readonly property color bgSurface: dark ? "#0e1118" : "#ffffff"
    readonly property color bgSurface2: dark ? "#12161f" : "#f7f8fc"
    readonly property color bgElevated: dark ? "#171c27" : "#eef0f7"
    readonly property color bgHover: dark ? "#1c2230" : "#e7eaf3"
    readonly property color border: dark ? "#1d2330" : "#e4e7ef"
    readonly property color borderStrong: dark ? "#2a3242" : "#d3d8e4"

    // Texto
    readonly property color textPrimary: dark ? "#e7eaf0" : "#171a22"
    readonly property color textSecondary: dark ? "#9aa3b5" : "#5b6475"
    readonly property color textTertiary: dark ? "#6b7488" : "#8a92a4"
    readonly property color textDisabled: dark ? "#4d5668" : "#aab1c0"

    // Marca e acentos
    readonly property color indigo700: "#3730a3"
    readonly property color indigo500: dark ? "#6366f1" : "#5b5ee8"
    readonly property color indigo400: dark ? "#818cf8" : "#4f46e5"
    readonly property color accentSoft: dark ? Qt.rgba(99 / 255, 102 / 255, 241 / 255, 0.12)
                                            : Qt.rgba(91 / 255, 94 / 255, 232 / 255, 0.10)
    readonly property color cyan: dark ? "#22d3ee" : "#0891b2"

    // Estados
    readonly property color success: dark ? "#34d399" : "#059669"
    readonly property color successSoft: dark ? Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.12)
                                              : Qt.rgba(5 / 255, 150 / 255, 105 / 255, 0.10)
    readonly property color warning: dark ? "#fbbf24" : "#b45309"
    readonly property color warningSoft: dark ? Qt.rgba(251 / 255, 191 / 255, 36 / 255, 0.12)
                                              : Qt.rgba(180 / 255, 83 / 255, 9 / 255, 0.10)
    readonly property color danger: dark ? "#f87171" : "#dc2626"
    readonly property color dangerSoft: dark ? Qt.rgba(248 / 255, 113 / 255, 113 / 255, 0.12)
                                             : Qt.rgba(220 / 255, 38 / 255, 38 / 255, 0.10)
    readonly property color info: dark ? "#58a6ff" : "#0969da"

    // Utilitários
    readonly property color terminalBg: dark ? "#0a0d13" : "#141821"
    readonly property color scroll: dark ? "#2a3344" : "#cfd5e1"
    readonly property color shadow: dark ? Qt.rgba(0, 0, 0, 0.38) : Qt.rgba(16 / 255, 24 / 255, 40 / 255, 0.16)
    readonly property color overlay: Qt.rgba(0, 0, 0, dark ? 0.42 : 0.24)

    // Tipografia
    readonly property string fontFamily: "Inter"
    readonly property string monoFamily: "JetBrains Mono"
    readonly property real fontMicro: 10.5
    readonly property real fontMeta: 11
    readonly property real fontBody: 12
    readonly property real fontUi: 13
    readonly property real fontTitle: 15
    readonly property real fontHeading: 19
    readonly property real fontDisplay: 23

    // Raios
    readonly property int radiusXs: 6
    readonly property int radiusSm: 8
    readonly property int radius: 10
    readonly property int radiusLg: 14

    // Espaçamento
    readonly property int spacingXs: 4
    readonly property int spacingSm: 6
    readonly property int spacing: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 14
    readonly property int spacingXl: 20

    // Métricas
    readonly property int sidebarWidth: 236
    readonly property int topbarHeight: 58
    readonly property int statusbarHeight: 30
    readonly property int navItemHeight: 34
    readonly property int rowHeight: 56
    readonly property int controlHeight: 32
    readonly property int controlHeightSm: 27
    readonly property int duration: 160
}

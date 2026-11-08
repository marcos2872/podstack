import QtQuick

// Sparkline simples (Canvas), desenhado só quando chegam amostras.
Canvas {
    id: root

    property var values: []
    property color lineColor: Theme.cyan
    property color fillColor: Qt.rgba(lineColor.r, lineColor.g, lineColor.b, 0.10)
    property real maxValue: 0
    property bool autoScale: false

    onValuesChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onLineColorChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.clearRect(0, 0, width, height)

        const count = values.length
        if (count < 2 || width <= 0 || height <= 0) {
            return
        }

        let lo = 0
        let hi = maxValue > 0 ? maxValue : 1
        if (autoScale) {
            lo = values[0]
            hi = values[0]
            for (let i = 1; i < count; ++i) {
                lo = Math.min(lo, values[i])
                hi = Math.max(hi, values[i])
            }
            if (hi <= lo) {
                hi = lo + 1
            }
            const padding = (hi - lo) * 0.08
            lo -= padding
            hi += padding
        } else {
            for (let i = 0; i < count; ++i) {
                hi = Math.max(hi, values[i])
            }
        }

        const padY = 3
        const span = Math.max(0.0001, hi - lo)
        const drawPath = function() {
            ctx.beginPath()
            for (let i = 0; i < count; ++i) {
                const x = (i / (count - 1)) * width
                const y = height - padY - ((values[i] - lo) / span) * (height - padY * 2)
                if (i === 0) {
                    ctx.moveTo(x, y)
                } else {
                    ctx.lineTo(x, y)
                }
            }
        }

        ctx.lineJoin = "round"
        ctx.lineCap = "round"

        // área
        drawPath()
        ctx.lineTo(width, height)
        ctx.lineTo(0, height)
        ctx.closePath()
        ctx.fillStyle = fillColor
        ctx.fill()

        // linha
        drawPath()
        ctx.strokeStyle = lineColor
        ctx.lineWidth = 1.6
        ctx.stroke()
    }
}

import QtQuick

// Estado de multi-seleção compartilhado pelas telas de catálogo.
// O comportamento é o mesmo da tela de Containers: clique seleciona só o
// item, Ctrl alterna e Shift seleciona o intervalo a partir da âncora.
QtObject {
    id: root

    property var selected: ({})
    property int anchor: -1

    readonly property int count: Object.keys(selected).length

    signal changed()

    function has(id) {
        return id !== undefined && id !== null && id !== "" && selected[id] === true
    }

    function ids() {
        return Object.keys(selected)
    }

    function clear() {
        selected = {}
        anchor = -1
        changed()
    }

    function selectOnly(id, index) {
        if (id === undefined || id === null || id === "") {
            return
        }
        const next = {}
        next[id] = true
        selected = next
        anchor = index
        changed()
    }

    function toggle(id, index) {
        if (id === undefined || id === null || id === "") {
            return
        }
        const next = Object.assign({}, selected)
        if (next[id]) {
            delete next[id]
        } else {
            next[id] = true
        }
        selected = next
        anchor = index
        changed()
    }

    function selectRange(allIds, index) {
        if (anchor < 0 || allIds.length === 0) {
            if (index >= 0 && index < allIds.length) {
                selectOnly(allIds[index], index)
            }
            return
        }
        const from = Math.min(anchor, index)
        const to = Math.max(anchor, index)
        const next = {}
        for (let i = from; i <= to; ++i) {
            if (allIds[i] !== undefined && allIds[i] !== "") {
                next[allIds[i]] = true
            }
        }
        selected = next
        changed()
    }

    // Mantém apenas IDs que ainda existem na lista atual.
    function prune(allIds) {
        const allowed = {}
        for (let i = 0; i < allIds.length; ++i) {
            allowed[allIds[i]] = true
        }
        const next = {}
        let changedAny = false
        for (const id in selected) {
            if (allowed[id]) {
                next[id] = true
            } else {
                changedAny = true
            }
        }
        if (changedAny) {
            selected = next
            changed()
        }
    }
}

import QtQuick
import QtQuick.Window
import ExplorerApp

// Hosts the production NavPanel in a window, as Main.qml does, and gives the
// test the user's actions on its tree and what the tree shows.
Window {
    width: 320
    height: 400
    visible: true

    property alias panel: nav

    NavPanel {
        id: nav
        anchors.fill: parent
    }

    function find(item, matches) {
        if (item && matches(item))
            return item
        for (let i = 0; item && i < item.children.length; ++i) {
            const found = find(item.children[i], matches)
            if (found)
                return found
        }
        return null
    }

    function view() {
        return find(nav, function(item) {
            return typeof item.expandRecursively === "function" && typeof item.isExpanded === "function"
        })
    }

    function rowOf(title) {
        const v = view()
        for (let r = 0; r < v.rows; ++r) {
            if (v.model.data(v.index(r, 0), Qt.DisplayRole) === title)
                return r
        }
        return -1
    }

    // Whether the shown tab's navigation has been applied.
    function settled() {
        return nav.tab !== null && nav._appliedTab === nav.tab
    }

    function expand(title) {
        const v = view()
        v.expand(rowOf(title))
        v.forceLayout()
    }

    // Expands a row as a click does: the view lays it out on its next frame.
    function expandUnlaid(title) {
        view().expand(rowOf(title))
    }

    function select(title) {
        const v = view()
        v.selectionModel.setCurrentIndex(v.index(rowOf(title), 0), ItemSelectionModel.ClearAndSelect)
    }

    function scrollTo(y) {
        view().contentY = y
    }

    function collapseAll() {
        const v = view()
        v.collapseRecursively()
        v.forceLayout()
    }

    // The filter field's action, as typing does.
    function filter(text) {
        nav._applyFilter(text)
    }

    function searchText() {
        return find(nav, function(item) { return item.placeholderText === "Filter (Ctrl+F)" }).text
    }

    // What the tree shows once laid out: expanded rows, current row, scroll
    // position, rows.
    function state() {
        const v = view()
        v.forceLayout()
        let expanded = []
        for (let r = 0; r < v.rows; ++r) {
            if (v.isExpanded(r))
                expanded.push(v.model.data(v.index(r, 0), Qt.DisplayRole))
        }
        const current = v.selectionModel.currentIndex
        return "expanded=[" + expanded.join(", ") + "] current="
            + (current.valid ? v.model.data(current, Qt.DisplayRole) : "none")
            + " contentY=" + Math.round(v.contentY) + " rows=" + v.rows
    }
}

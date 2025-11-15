import QtQuick 2.0
import QtQuick.Controls 2.2

Menu {
    onOpened: {
        // Give focus to the first visible and enabled menu item
        for (var i = 0; i < count; i++) {
            var item = itemAt(i)
            if (item.visible && item.enabled) {
                item.forceActiveFocus(Qt.TabFocusReason)
                break
            }
        }
    }
}

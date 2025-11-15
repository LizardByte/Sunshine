import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

ToolButton {
    property string iconSource

    activeFocusOnTab: true

    icon.source: iconSource
    icon.width: background.width
    icon.height: background.height

    // This determines the size of the Material highlight. We increase it
    // from the default because we use larger than normal icons for TV readability.
    Layout.preferredHeight: parent.height

    Keys.onReturnPressed: {
        clicked()
    }

    Keys.onEnterPressed: {
        clicked()
    }

    Keys.onRightPressed: {
        nextItemInFocusChain(true).forceActiveFocus(Qt.TabFocus)
    }

    Keys.onLeftPressed: {
        nextItemInFocusChain(false).forceActiveFocus(Qt.TabFocus)
    }
}

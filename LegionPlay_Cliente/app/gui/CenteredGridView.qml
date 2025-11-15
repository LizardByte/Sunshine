import QtQuick 2.9
import QtQuick.Controls 2.2

GridView {
    property int minMargin: 10
    property real availableWidth: (parent.width - 2 * minMargin)
    property int itemsPerRow: availableWidth / cellWidth
    property real horizontalMargin: itemsPerRow < count && availableWidth >= cellWidth ?
                                        (availableWidth % cellWidth) / 2 : minMargin

    function updateMargins() {
        leftMargin = horizontalMargin
        rightMargin = horizontalMargin
    }

    onHorizontalMarginChanged: {
        updateMargins()
    }

    Component.onCompleted: {
        updateMargins()
    }

    boundsBehavior: Flickable.OvershootBounds
}

import QtQuick 2.0
import QtQuick.Controls 2.2

Dialog {
    // We should use Overlay.overlay here but that's not available in Qt 5.9 :(
    parent: ApplicationWindow.contentItem

    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)

    onAboutToHide: {
        // We must force focus back to the last item for platforms without
        // support for more than one active window like Steam Link. If
        // we don't, gamepad and keyboard navigation will break after a
        // dialog appears.
        stackView.forceActiveFocus()
    }
}

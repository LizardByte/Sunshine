import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2

import AppModel 1.0
import ComputerManager 1.0
import SdlGamepadKeyNavigation 1.0

CenteredGridView {
    property int computerIndex
    property AppModel appModel : createModel()
    property bool activated
    property bool showHiddenGames
    property bool showGames

    id: appGrid
    focus: true
    activeFocusOnTab: true
    topMargin: 20
    bottomMargin: 5
    cellWidth: 230; cellHeight: 297;

    function computerLost()
    {
        // Go back to the PC view on PC loss
        stackView.pop()
    }

    Component.onCompleted: {
        // Don't show any highlighted item until interacting with them.
        // We do this here instead of onActivated to avoid losing the user's
        // selection when backing out of a different page of the app.
        currentIndex = -1
    }

    StackView.onActivated: {
        appModel.computerLost.connect(computerLost)
        activated = true

        // Highlight the first item if a gamepad is connected
        if (currentIndex === -1 && SdlGamepadKeyNavigation.getConnectedGamepads() > 0) {
            currentIndex = 0
        }

        if (!showGames && !showHiddenGames) {
            // Check if there's a direct launch app
            var directLaunchAppIndex = model.getDirectLaunchAppIndex();
            if (directLaunchAppIndex >= 0) {
                // Start the direct launch app if nothing else is running
                currentIndex = directLaunchAppIndex
                currentItem.launchOrResumeSelectedApp(false)

                // Set showGames so we will not loop when the stream ends
                showGames = true
            }
        }
    }

    StackView.onDeactivating: {
        appModel.computerLost.disconnect(computerLost)
        activated = false
    }

    function createModel()
    {
        var model = Qt.createQmlObject('import AppModel 1.0; AppModel {}', parent, '')
        model.initialize(ComputerManager, computerIndex, showHiddenGames)
        return model
    }

    model: appModel

    delegate: NavigableItemDelegate {
        width: 220; height: 287;
        grid: appGrid

        property alias appContextMenu: appContextMenuLoader.item
        property alias appNameText: appNameTextLoader.item

        // Dim the app if it's hidden
        opacity: model.hidden ? 0.4 : 1.0

        Image {
            property bool isPlaceholder: false

            id: appIcon
            anchors.horizontalCenter: parent.horizontalCenter
            y: 10
            source: model.boxart

            onSourceSizeChanged: {
                // Nearly all of Nvidia's official box art does not match the dimensions of placeholder
                // images, however the one known exception is Overcooked. Therefore, we only execute
                // the image size checks if this is not an app collector game. We know the officially
                // supported games all have box art, so this check is not required.
                if (!model.isAppCollectorGame &&
                    ((sourceSize.width === 130 && sourceSize.height === 180) || // GFE 2.0 placeholder image
                     (sourceSize.width === 628 && sourceSize.height === 888) || // GFE 3.0 placeholder image
                     (sourceSize.width === 200 && sourceSize.height === 266)))  // Our no_app_image.png
                {
                    isPlaceholder = true
                }
                else
                {
                    isPlaceholder = false
                }

                width = 200
                height = 267
            }

            // Display a tooltip with the full name if it's truncated
            ToolTip.text: model.name
            ToolTip.delay: 1000
            ToolTip.timeout: 5000
            ToolTip.visible: (parent.hovered || parent.highlighted) && (!appNameText || appNameText.truncated)
        }

        Loader {
            active: model.running
            asynchronous: true
            anchors.fill: appIcon

            sourceComponent: Item {
                RoundButton {
                    anchors.horizontalCenterOffset: appIcon.isPlaceholder ? -47 : 0
                    anchors.verticalCenterOffset: appIcon.isPlaceholder ? -75 : -60
                    anchors.centerIn: parent
                    implicitWidth: 85
                    implicitHeight: 85

                    icon.source: "qrc:/res/play_arrow_FILL1_wght700_GRAD200_opsz48.svg"
                    icon.width: 75
                    icon.height: 75

                    onClicked: {
                        launchOrResumeSelectedApp(true)
                    }

                    ToolTip.text: qsTr("Resume Game")
                    ToolTip.delay: 1000
                    ToolTip.timeout: 3000
                    ToolTip.visible: hovered

                    Material.background: "#D0808080"
                }

                RoundButton {
                    anchors.horizontalCenterOffset: appIcon.isPlaceholder ? 47 : 0
                    anchors.verticalCenterOffset: appIcon.isPlaceholder ? -75 : 60
                    anchors.centerIn: parent
                    implicitWidth: 85
                    implicitHeight: 85

                    icon.source: "qrc:/res/stop_FILL1_wght700_GRAD200_opsz48.svg"
                    icon.width: 75
                    icon.height: 75

                    onClicked: {
                        doQuitGame()
                    }

                    ToolTip.text: qsTr("Quit Game")
                    ToolTip.delay: 1000
                    ToolTip.timeout: 3000
                    ToolTip.visible: hovered

                    Material.background: "#D0808080"
                }
            }
        }

        Loader {
            id: appNameTextLoader
            active: appIcon.isPlaceholder

            // This loader is not asynchronous to avoid noticeable differences
            // in the time in which the text loads for each game.

            width: appIcon.width
            height: model.running ? 175 : appIcon.height

            anchors.left: appIcon.left
            anchors.right: appIcon.right
            anchors.bottom: appIcon.bottom

            sourceComponent: Label {
                id: appNameText
                text: model.name
                font.pointSize: 22
                leftPadding: 20
                rightPadding: 20
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                elide: Text.ElideRight
            }
        }

        function launchOrResumeSelectedApp(quitExistingApp)
        {
            var runningId = appModel.getRunningAppId()
            if (runningId !== 0 && runningId !== model.appid) {
                if (quitExistingApp) {
                    quitAppDialog.appName = appModel.getRunningAppName()
                    quitAppDialog.segueToStream = true
                    quitAppDialog.nextAppName = model.name
                    quitAppDialog.nextAppIndex = index
                    quitAppDialog.open()
                }

                return
            }

            var component = Qt.createComponent("StreamSegue.qml")
            var segue = component.createObject(stackView, {
                                                   "appName": model.name,
                                                   "session": appModel.createSessionForApp(index),
                                                   "isResume": runningId === model.appid
                                               })
            stackView.push(segue)
        }

        onClicked: {
            // Only allow clicking on the box art for non-running games.
            // For running games, buttons will appear to resume or quit which
            // will handle starting the game and clicks on the box art will
            // be ignored.
            if (!model.running) {
                launchOrResumeSelectedApp(true)
            }
        }

        onPressAndHold: {
            // popup() ensures the menu appears under the mouse cursor
            if (appContextMenu.popup) {
                appContextMenu.popup()
            }
            else {
                // Qt 5.9 doesn't have popup()
                appContextMenu.open()
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton;
            onClicked: {
                parent.pressAndHold()
            }
        }

        Keys.onReturnPressed: {
            // Open the app context menu if activated via the gamepad or keyboard
            // for running games. If the game isn't running, the above onClicked
            // method will handle the launch.
            if (model.running) {
                // This will be keyboard/gamepad driven so use
                // open() instead of popup()
                appContextMenu.open()
            }
        }

        Keys.onEnterPressed: {
            // Open the app context menu if activated via the gamepad or keyboard
            // for running games. If the game isn't running, the above onClicked
            // method will handle the launch.
            if (model.running) {
                // This will be keyboard/gamepad driven so use
                // open() instead of popup()
                appContextMenu.open()
            }
        }

        Keys.onMenuPressed: {
            // This will be keyboard/gamepad driven so use open() instead of popup()
            appContextMenu.open()
        }

        function doQuitGame() {
            quitAppDialog.appName = appModel.getRunningAppName()
            quitAppDialog.segueToStream = false
            quitAppDialog.open()
        }

        Loader {
            id: appContextMenuLoader
            asynchronous: true
            sourceComponent: NavigableMenu {
                id: appContextMenu
                NavigableMenuItem {
                    text: model.running ? qsTr("Resume Game") : qsTr("Launch Game")
                    onTriggered: launchOrResumeSelectedApp(true)
                }
                NavigableMenuItem {
                    text: qsTr("Quit Game")
                    onTriggered: doQuitGame()
                    visible: model.running
                }
                NavigableMenuItem {
                    checkable: true
                    checked: model.directLaunch
                    text: qsTr("Direct Launch")
                    onTriggered: appModel.setAppDirectLaunch(model.index, !model.directLaunch)
                    enabled: !model.hidden

                    ToolTip.text: qsTr("Launch this app immediately when the host is selected, bypassing the app selection grid.")
                    ToolTip.delay: 1000
                    ToolTip.timeout: 3000
                    ToolTip.visible: hovered
                }
                NavigableMenuItem {
                    checkable: true
                    checked: model.hidden
                    text: qsTr("Hide Game")
                    onTriggered: appModel.setAppHidden(model.index, !model.hidden)
                    enabled: model.hidden || (!model.running && !model.directLaunch)

                    ToolTip.text: qsTr("Hide this game from the app grid. To access hidden games, right-click on the host and choose %1.").arg(qsTr("View All Apps"))
                    ToolTip.delay: 1000
                    ToolTip.timeout: 5000
                    ToolTip.visible: hovered
                }
            }
        }
    }

    Row {
        anchors.centerIn: parent
        spacing: 5
        visible: appGrid.count === 0

        Label {
            text: qsTr("This computer doesn't seem to have any applications or some applications are hidden")
            font.pointSize: 20
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }

    NavigableMessageDialog {
        id: quitAppDialog
        property string appName : ""
        property bool segueToStream : false
        property string nextAppName: ""
        property int nextAppIndex: 0
        text:qsTr("Are you sure you want to quit %1? Any unsaved progress will be lost.").arg(appName)
        standardButtons: Dialog.Yes | Dialog.No

        function quitApp() {
            var component = Qt.createComponent("QuitSegue.qml")
            var params = {"appName": appName, "quitRunningAppFn": function() { appModel.quitRunningApp() }}
            if (segueToStream) {
                // Store the session and app name if we're going to stream after
                // successfully quitting the old app.
                params.nextAppName = nextAppName
                params.nextSession = appModel.createSessionForApp(nextAppIndex)
            }
            else {
                params.nextAppName = null
                params.nextSession = null
            }

            stackView.push(component.createObject(stackView, params))
        }

        onAccepted: quitApp()
    }

    ScrollBar.vertical: ScrollBar {}
}

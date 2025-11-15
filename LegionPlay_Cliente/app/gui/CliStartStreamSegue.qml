import QtQuick 2.0
import QtQuick.Controls 2.2

import ComputerManager 1.0

Item {
    function onSearchingComputer() {
        stageLabel.text = qsTr("Establishing connection to PC...")
    }

    function onSearchingApp() {
        stageLabel.text = qsTr("Loading app list...")
    }

    function onSessionCreated(appName, session) {
        var component = Qt.createComponent("StreamSegue.qml")
        var segue = component.createObject(stackView, {
            "appName": appName,
            "session": session,
            "quitAfter": true
        })
        stackView.push(segue)
    }

    function onLaunchFailed(message) {
        errorDialog.text = message
        errorDialog.open()
        console.error(message)
    }

    function onAppQuitRequired(appName) {
        quitAppDialog.appName = appName
        quitAppDialog.open()
    }

    StackView.onActivated: {
        if (!launcher.isExecuted()) {
            toolBar.visible = false

            launcher.searchingComputer.connect(onSearchingComputer)
            launcher.searchingApp.connect(onSearchingApp)
            launcher.sessionCreated.connect(onSessionCreated)
            launcher.failed.connect(onLaunchFailed)
            launcher.appQuitRequired.connect(onAppQuitRequired)
            launcher.execute(ComputerManager)
        }
    }

    Row {
        anchors.centerIn: parent
        spacing: 5

        BusyIndicator {
            id: stageSpinner
            running: visible
        }

        Label {
            id: stageLabel
            height: stageSpinner.height
            font.pointSize: 20
            verticalAlignment: Text.AlignVCenter

            wrapMode: Text.Wrap
        }
    }

    ErrorMessageDialog {
        id: errorDialog

        onClosed: {
            Qt.quit();
        }
    }

    NavigableMessageDialog {
        id: quitAppDialog
        text:qsTr("Are you sure you want to quit %1? Any unsaved progress will be lost.").arg(appName)
        standardButtons: Dialog.Yes | Dialog.No
        property string appName : ""

        function quitApp() {
            var component = Qt.createComponent("QuitSegue.qml")
            var params = {"appName": appName, "quitRunningAppFn": function() { launcher.quitRunningApp() }}
            stackView.push(component.createObject(stackView, params))
        }

        onAccepted: quitApp()
        onRejected: Qt.quit()
    }
}

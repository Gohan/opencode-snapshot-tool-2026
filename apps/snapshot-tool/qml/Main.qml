import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1440
    height: 900
    minimumWidth: 1060
    minimumHeight: 680
    visible: true
    title: qsTr("OpenCode Snapshot Tool")
    color: "#0b0f17"

    palette.window: "#0b0f17"
    palette.windowText: "#e8edf6"
    palette.base: "#121925"
    palette.text: "#e8edf6"
    palette.button: "#1a2433"
    palette.buttonText: "#e8edf6"
    palette.highlight: "#76e4b4"
    palette.highlightedText: "#07110d"

    Shortcut { sequence: "Ctrl+,"; onActivated: settingsPopup.open() }
    Shortcut {
        sequence: "Ctrl+P"
        enabled: !snapshotController.busy && snapshotController.repositoryCount > 0
        onActivated: snapshotController.previewCleanup()
    }

    component MetricCard: Rectangle {
        required property string label
        required property string value
        property color accent: "#76e4b4"
        radius: 12
        color: "#121925"
        border.color: "#263245"
        implicitHeight: 82
        Layout.fillWidth: true
        Column {
            anchors.fill: parent
            anchors.margins: 15
            spacing: 7
            Label { text: parent.parent.label; color: "#8492a8"; font.pixelSize: 12 }
            Label { text: parent.parent.value; color: parent.parent.accent; font.pixelSize: 23; font.weight: Font.DemiBold }
        }
    }

    header: ToolBar {
        height: 68
        background: Rectangle { color: "#0d131d"; border.color: "#202b3b" }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Label { text: qsTr("OpenCode Snapshot Tool"); font.pixelSize: 20; font.weight: Font.DemiBold }
                Label { text: qsTr("See what is retained before reclaiming disk space"); color: "#8492a8"; font.pixelSize: 12 }
            }
            Button { text: qsTr("Settings"); onClicked: settingsPopup.open() }
            Button {
                text: snapshotController.busy ? qsTr("Working…") : qsTr("Scan")
                enabled: !snapshotController.busy
                highlighted: true
                onClicked: snapshotController.scan()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            MetricCard { label: qsTr("Storage"); value: snapshotController.formatBytes(snapshotController.totalBytes) }
            MetricCard { label: qsTr("Repositories"); value: snapshotController.repositoryCount.toString(); accent: "#80bfff" }
            MetricCard { label: qsTr("Snapshot records"); value: snapshotController.snapshotCount.toString(); accent: "#cba6f7" }
            MetricCard { label: qsTr("Retain / release"); value: snapshotController.keepCount + " / " + snapshotController.dropCount; accent: "#ffd580" }
            MetricCard { label: qsTr("Immediate estimate"); value: snapshotController.formatBytes(snapshotController.estimatedReclaimableBytes); accent: "#ff9d8d" }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            Rectangle {
                SplitView.preferredWidth: 420
                SplitView.minimumWidth: 310
                color: "#101722"
                radius: 12
                border.color: "#263245"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Repositories"); font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Label { text: snapshotController.repositoryCount; color: "#8492a8" }
                    }
                    TextField {
                        id: repositorySearch
                        Layout.fillWidth: true
                        placeholderText: qsTr("Filter by project or path")
                    }
                    ListView {
                        id: repositoryList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 7
                        model: snapshotController.repositories
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property var modelData
                            width: repositoryList.width
                            height: visible ? 90 : 0
                            visible: repositorySearch.text.length === 0 ||
                                     modelData.name.toLowerCase().includes(repositorySearch.text.toLowerCase()) ||
                                     modelData.worktree.toLowerCase().includes(repositorySearch.text.toLowerCase())
                            radius: 9
                            color: snapshotController.selectedRepository === modelData.index ? "#203445" : mouse.containsMouse ? "#182231" : "#141c28"
                            border.color: snapshotController.selectedRepository === modelData.index ? "#76e4b4" : "#243044"
                            MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; onClicked: snapshotController.selectedRepository = modelData.index }
                            Column {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 5
                                Label { width: parent.width; text: modelData.name; elide: Text.ElideMiddle; font.weight: Font.DemiBold }
                                Label { width: parent.width; text: modelData.worktree || qsTr("Worktree unknown"); color: "#8492a8"; elide: Text.ElideMiddle; font.pixelSize: 11 }
                                Row {
                                    spacing: 14
                                    Label { text: modelData.bytesText; color: "#80bfff"; font.pixelSize: 11 }
                                    Label { text: qsTr("%1 records").arg(modelData.snapshots); color: "#a9b5c7"; font.pixelSize: 11 }
                                    Label { text: qsTr("%1 release").arg(modelData.dropped); color: modelData.dropped ? "#ff9d8d" : "#76e4b4"; font.pixelSize: 11 }
                                }
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: repositoryList.count === 0
                            text: qsTr("Run a scan to discover snapshot repositories")
                            color: "#8492a8"
                        }
                    }
                }
            }

            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 580
                color: "#101722"
                radius: 12
                border.color: "#263245"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Snapshots"); font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Rectangle { width: 10; height: 10; radius: 5; color: "#76e4b4" }
                        Label { text: qsTr("retained"); color: "#8492a8"; font.pixelSize: 11 }
                        Rectangle { width: 10; height: 10; radius: 5; color: "#ff7f73" }
                        Label { text: qsTr("released"); color: "#8492a8"; font.pixelSize: 11 }
                    }
                    ListView {
                        id: snapshotList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 7
                        model: snapshotController.snapshots
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property var modelData
                            width: snapshotList.width
                            height: 92
                            radius: 9
                            color: "#141c28"
                            border.color: modelData.keep ? "#2e6a57" : "#613b3d"
                            Rectangle { width: 4; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; radius: 2; color: modelData.keep ? "#76e4b4" : "#ff7f73" }
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                anchors.leftMargin: 16
                                spacing: 14
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Label { Layout.fillWidth: true; text: modelData.title; elide: Text.ElideRight; font.weight: Font.DemiBold }
                                    Label { text: modelData.shortHash + "  ·  " + modelData.time + "  ·  " + modelData.source; color: "#8492a8"; font.family: "monospace"; font.pixelSize: 11 }
                                    Label { text: modelData.keep ? modelData.reason : qsTr("Outside retention policy"); color: modelData.keep ? "#76e4b4" : "#ff9d8d"; font.pixelSize: 11 }
                                }
                                Label { text: qsTr("%1 refs\n%2 sessions").arg(modelData.references).arg(modelData.sessions); horizontalAlignment: Text.AlignRight; color: "#a9b5c7"; font.pixelSize: 11 }
                            }
                            ToolTip.visible: hover.hovered
                            ToolTip.text: modelData.hash
                            HoverHandler { id: hover }
                        }
                        Label { anchors.centerIn: parent; visible: snapshotList.count === 0; text: qsTr("Select a repository to inspect snapshots"); color: "#8492a8" }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 64
            radius: 12
            color: "#121925"
            border.color: "#263245"
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12
                BusyIndicator { running: snapshotController.busy; visible: running; implicitWidth: 32; implicitHeight: 32 }
                Label { text: snapshotController.status; color: "#a9b5c7"; Layout.fillWidth: true; wrapMode: Text.Wrap }
                Button { text: qsTr("Preview cleanup"); enabled: !snapshotController.busy && snapshotController.repositoryCount > 0; onClicked: snapshotController.previewCleanup() }
                Button { text: qsTr("Clean now"); highlighted: true; enabled: !snapshotController.busy && snapshotController.hasPlan; onClicked: confirmDialog.open() }
            }
        }
    }

    Dialog {
        id: settingsPopup
        title: qsTr("Scan and retention settings")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(window.width - 80, 760)
        standardButtons: Dialog.Close
        ColumnLayout {
            width: parent.width
            spacing: 14
            Label { text: qsTr("Snapshot directory"); color: "#8492a8" }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    Layout.fillWidth: true
                    text: snapshotController.snapshotRoot
                    onEditingFinished: snapshotController.snapshotRoot = text
                }
                Button { text: qsTr("Browse…"); onClicked: snapshotController.chooseSnapshotRoot() }
            }
            Label { text: qsTr("OpenCode database"); color: "#8492a8" }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    Layout.fillWidth: true
                    text: snapshotController.databasePath
                    onEditingFinished: snapshotController.databasePath = text
                }
                Button { text: qsTr("Browse…"); onClicked: snapshotController.chooseDatabase() }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Retain snapshots from the last"); Layout.fillWidth: true }
                SpinBox { from: 0; to: 3650; value: snapshotController.recentDays; onValueModified: snapshotController.recentDays = value }
                Label { text: qsTr("days") }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("If none are recent, retain newest per repository"); Layout.fillWidth: true }
                SpinBox { from: 1; to: 1000; value: snapshotController.fallbackCount; onValueModified: snapshotController.fallbackCount = value }
            }
            CheckBox { text: qsTr("Run full Git garbage collection"); checked: snapshotController.fullGc; onToggled: snapshotController.fullGc = checked }
            CheckBox { text: qsTr("Remove Git LFS objects not referenced by retained trees"); checked: snapshotController.pruneLfs; onToggled: snapshotController.pruneLfs = checked }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Remove temporary packs and empty index locks older than"); Layout.fillWidth: true }
                SpinBox { from: 1; to: 8760; value: snapshotController.staleFileHours; onValueModified: snapshotController.staleFileHours = value }
                Label { text: qsTr("hours") }
            }
            Label { Layout.fillWidth: true; text: qsTr("A setting change invalidates the current cleanup preview. Path changes require another scan; retention changes update the current result immediately."); color: "#ffd580"; wrapMode: Text.Wrap }
        }
    }

    Dialog {
        id: confirmDialog
        title: qsTr("Confirm cleanup")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 560
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: snapshotController.executeCleanup()
        ColumnLayout {
            width: parent.width
            spacing: 10
            Label { Layout.fillWidth: true; text: qsTr("This will protect retained trees with private Git refs, then prune unreachable Git/LFS data according to the preview."); wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; text: qsTr("Close OpenCode or ensure it is idle before continuing. Cleanup cannot be undone from this tool."); color: "#ff9d8d"; wrapMode: Text.Wrap; font.weight: Font.DemiBold }
            Label { text: qsTr("Protect %1 trees · release %2 trees").arg(snapshotController.planKeepTrees).arg(snapshotController.planRemoveTrees); color: "#ffd580" }
            Label { text: qsTr("Estimated immediately reclaimable: %1").arg(snapshotController.formatBytes(snapshotController.estimatedReclaimableBytes)); color: "#76e4b4" }
        }
    }

    Component.onCompleted: snapshotController.scan()
}

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1440
    height: 900
    minimumWidth: 1080
    minimumHeight: 720
    visible: true
    title: qsTr("OpenCode Snapshot Tool")
    color: paper
    flags: Qt.Window | Qt.FramelessWindowHint

    readonly property color ink: "#1a1a1a"
    readonly property color yellow: "#ffcc00"
    readonly property color red: "#e63b2e"
    readonly property color blue: "#0055ff"
    readonly property color paper: "#f5f0e8"
    readonly property color white: "#ffffff"
    readonly property color muted: "#5c574f"

    palette.window: paper
    palette.windowText: ink
    palette.base: paper
    palette.text: ink
    palette.button: yellow
    palette.buttonText: ink
    palette.highlight: blue
    palette.highlightedText: white

    Shortcut { sequence: "Ctrl+,"; onActivated: settingsDialog.open() }
    Shortcut {
        sequence: "Ctrl+P"
        enabled: !snapshotController.busy && snapshotController.repositoryCount > 0
        onActivated: snapshotController.previewCleanup()
    }
    Shortcut { sequence: "Ctrl+1"; onActivated: repositoryTabs.currentIndex = 0 }
    Shortcut { sequence: "Ctrl+2"; onActivated: repositoryTabs.currentIndex = 1 }
    Shortcut { sequence: "Ctrl+3"; onActivated: repositoryTabs.currentIndex = 2 }
    Shortcut { sequence: "Ctrl+4"; onActivated: repositoryTabs.currentIndex = 3 }
    Shortcut { sequence: "Ctrl+5"; onActivated: repositoryTabs.currentIndex = 4 }
    Shortcut { sequence: "Ctrl+D"; enabled: !snapshotController.busy && snapshotController.selectedRepository >= 0; onActivated: snapshotController.analyzeSelectedRepository() }
    Shortcut {
        sequence: "Ctrl+Shift+R"
        enabled: !snapshotController.busy && snapshotController.selectedRepository >= 0 && repositoryDetailPanel.analysis.ready
        onActivated: snapshotController.hasProjectPlan && snapshotController.projectPlanMode === "reset"
                     ? projectConfirmDialog.open() : snapshotController.previewProjectReset()
    }

    component BrutalButton: Button {
        id: control
        property color fillColor: window.yellow
        property color foregroundColor: window.ink
        property color hoverFillColor: window.ink
        property color hoverForegroundColor: window.yellow
        property bool offset: true

        implicitWidth: Math.max(118, contentItem.implicitWidth + 34)
        implicitHeight: 46
        leftPadding: 17
        rightPadding: 17
        hoverEnabled: true
        opacity: enabled ? 1 : 0.42

        background: Item {
            Rectangle {
                visible: control.offset
                x: 5
                y: 5
                width: parent.width - 5
                height: parent.height - 5
                color: window.ink
            }
            Rectangle {
                x: control.down && control.offset ? 3 : 0
                y: control.down && control.offset ? 3 : 0
                width: parent.width - (control.offset ? 5 : 0)
                height: parent.height - (control.offset ? 5 : 0)
                color: control.hovered || control.down ? control.hoverFillColor : control.fillColor
                border.color: window.ink
                border.width: 3
            }
        }
        contentItem: Text {
            leftPadding: control.down && control.offset ? 3 : 0
            topPadding: control.down && control.offset ? 3 : 0
            text: control.text.toUpperCase()
            color: control.hovered || control.down ? control.hoverForegroundColor : control.foregroundColor
            font.family: "Space Grotesk"
            font.pixelSize: 13
            font.weight: Font.Bold
            font.letterSpacing: 1.1
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    component WindowButton: Button {
        id: windowControl
        property color hoverColor: window.yellow
        implicitWidth: 48
        implicitHeight: 36
        padding: 0
        hoverEnabled: true
        background: Rectangle {
            color: windowControl.hovered ? windowControl.hoverColor : window.paper
            border.color: window.ink
            border.width: 0
        }
        contentItem: Text {
            text: windowControl.text
            color: window.ink
            font.family: "Space Grotesk"
            font.pixelSize: 17
            font.weight: Font.Bold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component BatchButton: Button {
        id: batchControl
        required property string step
        required property string actionLabel
        required property string detail
        property color fillColor: window.blue
        property color foregroundColor: window.white
        property color hoverForegroundColor: window.blue

        implicitWidth: 270
        implicitHeight: 64
        leftPadding: 13
        rightPadding: 13
        hoverEnabled: true
        opacity: enabled ? 1 : 0.58
        background: Item {
            Rectangle { x: 5; y: 5; width: parent.width - 5; height: parent.height - 5; color: window.ink }
            Rectangle {
                x: batchControl.down ? 3 : 0
                y: batchControl.down ? 3 : 0
                width: parent.width - 5
                height: parent.height - 5
                color: batchControl.hovered && batchControl.enabled ? window.ink : batchControl.fillColor
                border.color: window.ink
                border.width: 3
            }
        }
        contentItem: RowLayout {
            spacing: 12
            Label {
                text: batchControl.step
                color: batchControl.hovered && batchControl.enabled ? batchControl.hoverForegroundColor : batchControl.foregroundColor
                font.family: "Space Grotesk"
                font.pixelSize: 27
                font.weight: Font.Bold
            }
            Rectangle {
                implicitWidth: 3
                Layout.fillHeight: true
                color: batchControl.hovered && batchControl.enabled ? batchControl.hoverForegroundColor : batchControl.foregroundColor
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: batchControl.actionLabel
                    color: batchControl.hovered && batchControl.enabled ? batchControl.hoverForegroundColor : batchControl.foregroundColor
                    font.family: "Space Grotesk"
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    font.letterSpacing: 0.8
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: batchControl.detail
                    color: batchControl.hovered && batchControl.enabled ? batchControl.hoverForegroundColor : batchControl.foregroundColor
                    font.family: "Inter"
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    font.letterSpacing: 0.4
                    elide: Text.ElideRight
                }
            }
        }
    }

    component BrutalTabButton: TabButton {
        id: tabControl
        implicitHeight: 34
        hoverEnabled: true
        background: Rectangle {
            color: tabControl.checked ? window.yellow : tabControl.hovered ? window.blue : window.ink
            border.color: window.ink
            border.width: 2
        }
        contentItem: Text {
            text: tabControl.text.toUpperCase()
            color: tabControl.checked ? window.ink : window.white
            font.family: "Space Grotesk"
            font.pixelSize: 10
            font.weight: Font.Bold
            font.letterSpacing: 0.7
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component BrutalField: TextField {
        id: field
        implicitHeight: 44
        color: window.ink
        placeholderTextColor: window.muted
        selectionColor: window.blue
        selectedTextColor: window.white
        font.family: "Inter"
        font.pixelSize: 13
        leftPadding: 8
        rightPadding: 8
        background: Item {
            Rectangle { anchors.fill: parent; color: window.paper }
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 3; color: window.ink }
        }
    }

    component BrutalCheckBox: CheckBox {
        id: check
        spacing: 12
        indicator: Rectangle {
            implicitWidth: 24
            implicitHeight: 24
            x: check.leftPadding
            y: parent.height / 2 - height / 2
            color: check.checked ? window.yellow : window.paper
            border.color: window.ink
            border.width: 3
            Text {
                anchors.centerIn: parent
                text: "×"
                visible: check.checked
                color: window.ink
                font.family: "Space Grotesk"
                font.pixelSize: 22
                font.weight: Font.Bold
            }
        }
        contentItem: Text {
            leftPadding: check.indicator.width + check.spacing
            text: check.text
            color: window.ink
            font.family: "Inter"
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }

    component NumberStepper: RowLayout {
        id: stepper
        property int value: 0
        property int minimum: 0
        property int maximum: 100
        signal valueEdited(int nextValue)
        spacing: 6
        BrutalButton {
            text: "−"
            implicitWidth: 44
            fillColor: window.paper
            hoverForegroundColor: window.paper
            offset: false
            enabled: stepper.value > stepper.minimum
            onClicked: stepper.valueEdited(stepper.value - 1)
        }
        Rectangle {
            implicitWidth: 82
            implicitHeight: 44
            color: window.paper
            border.color: window.ink
            border.width: 3
            Label {
                anchors.centerIn: parent
                text: stepper.value
                color: window.ink
                font.family: "Space Grotesk"
                font.pixelSize: 16
                font.weight: Font.Bold
            }
        }
        BrutalButton {
            text: "+"
            implicitWidth: 44
            fillColor: window.paper
            hoverForegroundColor: window.paper
            offset: false
            enabled: stepper.value < stepper.maximum
            onClicked: stepper.valueEdited(stepper.value + 1)
        }
    }

    component BrutalPanel: Item {
        id: panel
        default property alias bodyData: panelBody.data
        Rectangle {
            x: 6
            y: 6
            width: parent.width - 6
            height: parent.height - 6
            color: window.ink
        }
        Rectangle {
            id: panelBody
            width: parent.width - 6
            height: parent.height - 6
            color: window.paper
            border.color: window.ink
            border.width: 3
        }
    }

    component MetricCard: Item {
        id: metric
        required property string label
        required property string value
        property color accent: window.yellow
        implicitHeight: 104
        Layout.fillWidth: true
        Rectangle {
            x: 5
            y: 5
            width: parent.width - 5
            height: parent.height - 5
            color: window.ink
        }
        Rectangle {
            width: parent.width - 5
            height: parent.height - 5
            color: window.paper
            border.color: window.ink
            border.width: 3
            Rectangle { width: 11; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; color: metric.accent }
            Column {
                anchors.fill: parent
                anchors.leftMargin: 26
                anchors.rightMargin: 12
                anchors.topMargin: 12
                spacing: 5
                Label {
                    width: parent.width
                    text: metric.label.toUpperCase()
                    color: window.ink
                    font.family: "Inter"
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    font.letterSpacing: 0.8
                    elide: Text.ElideRight
                }
                Label {
                    width: parent.width
                    text: metric.value
                    color: window.ink
                    font.family: "Space Grotesk"
                    font.pixelSize: 25
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
            }
        }
    }

    header: ToolBar {
        implicitHeight: 152
        padding: 0
        background: Rectangle { color: window.ink }
        contentItem: Column {
            Item {
                width: parent.width
                height: 36
                Rectangle { anchors.fill: parent; color: window.paper }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onPressed: window.startSystemMove()
                    onDoubleClicked: window.visibility === Window.Maximized ? window.showNormal() : window.showMaximized()
                }
                RowLayout {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 9
                    Rectangle { implicitWidth: 14; implicitHeight: 14; color: window.yellow; border.color: window.ink; border.width: 2 }
                    Rectangle { implicitWidth: 14; implicitHeight: 14; color: window.blue; border.color: window.ink; border.width: 2 }
                    Label { text: qsTr("OPENCODE SNAPSHOT TOOL"); color: window.ink; font.family: "Space Grotesk"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 1 }
                }
                Row {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    WindowButton { text: "—"; onClicked: window.showMinimized() }
                    WindowButton { text: window.visibility === Window.Maximized ? "▣" : "□"; onClicked: window.visibility === Window.Maximized ? window.showNormal() : window.showMaximized() }
                    WindowButton { text: "×"; hoverColor: window.red; onClicked: window.close() }
                }
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 3; color: window.ink }
            }
            Item {
                width: parent.width
                height: 116
                Rectangle { x: 22; y: 14; width: 14; height: 88; color: window.yellow }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 56
                    anchors.rightMargin: 24
                    spacing: 14
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: -3
                        Label {
                            text: qsTr("OPENCODE / SNAPSHOT")
                            color: window.white
                            font.family: "Space Grotesk"
                            font.pixelSize: 38
                            font.weight: Font.Bold
                            font.letterSpacing: -1.2
                        }
                        Label {
                            text: qsTr("FORM FOLLOWS FUNCTION  ·  SEE IT BEFORE YOU CLEAN IT")
                            color: window.yellow
                            font.family: "Inter"
                            font.pixelSize: 11
                            font.weight: Font.Bold
                            font.letterSpacing: 1.5
                        }
                    }
                    BrutalButton {
                        text: qsTr("Settings")
                        fillColor: window.paper
                        hoverForegroundColor: window.paper
                        onClicked: settingsDialog.open()
                    }
                    BrutalButton {
                        text: snapshotController.busy ? qsTr("Working…") : qsTr("Scan")
                        enabled: !snapshotController.busy
                        onClicked: snapshotController.scan()
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            MetricCard { label: qsTr("Snapshot storage"); value: snapshotController.formatBytes(snapshotController.totalBytes); accent: window.yellow }
            MetricCard { label: qsTr("Repositories"); value: snapshotController.repositoryCount.toString(); accent: window.blue }
            MetricCard { label: qsTr("Snapshot records"); value: snapshotController.snapshotCount.toString(); accent: window.yellow }
            MetricCard { label: qsTr("Retain / release"); value: snapshotController.keepCount + " / " + snapshotController.dropCount; accent: window.blue }
            MetricCard { label: qsTr("Direct file estimate"); value: snapshotController.formatBytes(snapshotController.estimatedReclaimableBytes); accent: window.red }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: 14
                color: window.paper
                Rectangle { anchors.centerIn: parent; width: 3; height: 54; color: window.ink }
            }

            BrutalPanel {
                SplitView.preferredWidth: 430
                SplitView.minimumWidth: 330
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 54
                        color: window.blue
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 15
                            anchors.rightMargin: 15
                            Label { text: qsTr("REPOSITORIES"); color: window.white; font.family: "Space Grotesk"; font.pixelSize: 18; font.weight: Font.Bold; Layout.fillWidth: true }
                            Label { text: snapshotController.repositoryCount; color: window.white; font.family: "Space Grotesk"; font.pixelSize: 20; font.weight: Font.Bold }
                        }
                    }
                    BrutalField {
                        id: repositorySearch
                        Layout.fillWidth: true
                        Layout.leftMargin: 13
                        Layout.rightMargin: 13
                        Layout.topMargin: 7
                        placeholderText: qsTr("FILTER BY PROJECT OR PATH")
                    }
                    ListView {
                        id: repositoryList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: 12
                        clip: true
                        spacing: 7
                        model: snapshotController.repositories
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: Rectangle {
                            required property var modelData
                            width: repositoryList.width - (repositoryList.ScrollBar.vertical.visible ? 10 : 0)
                            height: visible ? 88 : 0
                            visible: repositorySearch.text.length === 0
                                     || modelData.name.toLowerCase().includes(repositorySearch.text.toLowerCase())
                                     || modelData.worktree.toLowerCase().includes(repositorySearch.text.toLowerCase())
                            color: snapshotController.selectedRepository === modelData.index ? window.yellow : repoMouse.containsMouse ? "#e4ded3" : window.paper
                            border.color: window.ink
                            border.width: snapshotController.selectedRepository === modelData.index ? 3 : 2
                            MouseArea {
                                id: repoMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: snapshotController.selectedRepository = modelData.index
                            }
                            Column {
                                anchors.fill: parent
                                anchors.margins: 11
                                spacing: 5
                                Label { width: parent.width; text: modelData.name; color: window.ink; elide: Text.ElideMiddle; font.family: "Space Grotesk"; font.pixelSize: 15; font.weight: Font.Bold }
                                Label { width: parent.width; text: modelData.worktree || qsTr("Worktree unknown"); color: window.muted; elide: Text.ElideMiddle; font.pixelSize: 11 }
                                Row {
                                    spacing: 13
                                    Label { text: modelData.bytesText; color: window.blue; font.weight: Font.Bold; font.pixelSize: 11 }
                                    Label { text: qsTr("%1 RECORDS").arg(modelData.snapshots); color: window.ink; font.pixelSize: 10; font.weight: Font.Bold }
                                    Label { text: qsTr("%1 RELEASE").arg(modelData.dropped); color: modelData.dropped ? window.red : window.ink; font.pixelSize: 10; font.weight: Font.Bold }
                                }
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: repositoryList.count === 0
                            text: qsTr("RUN A SCAN TO DISCOVER REPOSITORIES")
                            color: window.muted
                            font.weight: Font.Bold
                        }
                    }
                }
            }

            BrutalPanel {
                id: repositoryDetailPanel
                property var details: snapshotController.selectedRepositoryDetails
                property var analysis: snapshotController.repositoryAnalysis
                SplitView.fillWidth: true
                SplitView.minimumWidth: 610
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 54
                        color: window.yellow
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 15
                            anchors.rightMargin: 15
                            Label { text: qsTr("REPOSITORY STORAGE"); color: window.ink; font.family: "Space Grotesk"; font.pixelSize: 18; font.weight: Font.Bold; Layout.fillWidth: true }
                            Label { text: repositoryDetailPanel.details.valid ? repositoryDetailPanel.details.totalBytesText : "—"; color: window.ink; font.family: "Space Grotesk"; font.pixelSize: 20; font.weight: Font.Bold }
                        }
                    }
                    TabBar {
                        id: repositoryTabs
                        Layout.fillWidth: true
                        implicitHeight: 36
                        spacing: 0
                        background: Rectangle { color: window.ink }
                        BrutalTabButton { text: qsTr("Overview"); onClicked: repositoryTabs.currentIndex = 0 }
                        BrutalTabButton { text: qsTr("Paths"); onClicked: repositoryTabs.currentIndex = 1 }
                        BrutalTabButton { text: qsTr("Objects"); onClicked: repositoryTabs.currentIndex = 2 }
                        BrutalTabButton { text: qsTr("Snapshots"); onClicked: repositoryTabs.currentIndex = 3 }
                        BrutalTabButton { text: qsTr("Reclaim"); onClicked: repositoryTabs.currentIndex = 4 }
                    }
                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: repositoryTabs.currentIndex
                        Rectangle {
                            color: window.paper
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 15
                                anchors.rightMargin: 15
                                anchors.topMargin: 11
                                anchors.bottomMargin: 10
                                spacing: 8
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 14
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1
                                        Label { Layout.fillWidth: true; text: repositoryDetailPanel.details.name || ""; color: window.ink; elide: Text.ElideMiddle; font.family: "Space Grotesk"; font.pixelSize: 14; font.weight: Font.Bold }
                                        Label { Layout.fillWidth: true; text: repositoryDetailPanel.details.worktree || qsTr("Worktree unknown"); color: window.muted; elide: Text.ElideMiddle; font.pixelSize: 10 }
                                    }
                                    ColumnLayout {
                                        spacing: 2
                                        Label { Layout.alignment: Qt.AlignRight; text: qsTr("%1 LOOSE  /  %2 PACKED").arg(repositoryDetailPanel.details.looseObjects || 0).arg(repositoryDetailPanel.details.packedObjects || 0); color: window.ink; font.pixelSize: 9; font.weight: Font.Bold }
                                        BrutalButton { implicitWidth: 132; implicitHeight: 31; text: snapshotController.analysisBusy ? qsTr("Analyzing…") : repositoryDetailPanel.analysis.ready ? qsTr("Refresh analysis") : qsTr("Deep analyze"); fillColor: window.blue; foregroundColor: window.white; hoverForegroundColor: window.blue; enabled: !snapshotController.busy; onClicked: snapshotController.analyzeSelectedRepository() }
                                    }
                                }
                                Row {
                                    id: storageBar
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 18
                                    Repeater {
                                        model: repositoryDetailPanel.details.categories || []
                                        delegate: Rectangle {
                                            required property var modelData
                                            width: storageBar.width * modelData.percent / 100
                                            height: storageBar.height
                                            color: modelData.color
                                            border.color: window.ink
                                            border.width: modelData.bytes > 0 ? 1 : 0
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 7
                                    Repeater {
                                        model: repositoryDetailPanel.details.categories || []
                                        delegate: Rectangle {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            implicitHeight: 55
                                            color: window.paper
                                            border.color: window.ink
                                            border.width: 2
                                            Rectangle { width: 7; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; color: modelData.color }
                                            Column {
                                                anchors.fill: parent
                                                anchors.leftMargin: 14
                                                anchors.rightMargin: 6
                                                anchors.topMargin: 6
                                                spacing: 1
                                                Label { width: parent.width; text: modelData.label.toUpperCase(); color: window.ink; elide: Text.ElideRight; font.pixelSize: 8; font.weight: Font.Bold }
                                                Label { width: parent.width; text: modelData.bytesText + "  ·  " + Math.round(modelData.percent) + "%"; color: window.ink; elide: Text.ElideRight; font.family: "Space Grotesk"; font.pixelSize: 12; font.weight: Font.Bold }
                                            }
                                        }
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 70
                                    color: repositoryDetailPanel.details.duplicateWorktrees > 0 ? window.yellow : "#e8e1d6"
                                    border.color: window.ink
                                    border.width: 2
                                    Rectangle { width: 7; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; color: window.blue }
                                    Label { anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 9; anchors.topMargin: 6; anchors.bottomMargin: 6; text: repositoryDetailPanel.details.explanation || ""; color: window.ink; wrapMode: Text.Wrap; maximumLineCount: 4; elide: Text.ElideRight; font.pixelSize: 9; font.weight: Font.Medium; verticalAlignment: Text.AlignVCenter }
                                }
                            }
                        }
                        Item {
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 7
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: qsTr("CURRENT TREE PATH WEIGHT"); color: window.ink; font.family: "Space Grotesk"; font.pixelSize: 12; font.weight: Font.Bold; Layout.fillWidth: true }
                                    Label { visible: repositoryDetailPanel.analysis.ready; text: qsTr("PACKED / EXPANDED"); color: window.muted; font.pixelSize: 8; font.weight: Font.Bold }
                                }
                                ListView {
                                    id: pathUsageList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    visible: repositoryDetailPanel.analysis.ready
                                    clip: true
                                    spacing: 4
                                    model: repositoryDetailPanel.analysis.paths || []
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: pathUsageList.width
                                        height: 37
                                        color: window.paper
                                        border.color: window.ink
                                        border.width: 2
                                        Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: Math.max(6, parent.width * modelData.percent / 100); color: "#c9d7ff" }
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 10
                                            anchors.rightMargin: 9
                                            Label { Layout.fillWidth: true; text: modelData.path; color: window.ink; elide: Text.ElideMiddle; font.family: "Space Grotesk"; font.pixelSize: 10; font.weight: Font.Bold }
                                            Label { text: modelData.bytesText + " / " + modelData.expandedText; color: window.ink; font.pixelSize: 9; font.weight: Font.Bold }
                                            Label { text: qsTr("%1 OBJ").arg(modelData.objects); color: window.muted; font.pixelSize: 8; font.weight: Font.Bold }
                                        }
                                    }
                                }
                                ColumnLayout {
                                    Layout.alignment: Qt.AlignCenter
                                    visible: !repositoryDetailPanel.analysis.ready
                                    Label { text: snapshotController.analysisBusy ? qsTr("ANALYZING PACKS…") : qsTr("MAP PACKED BYTES BACK TO PROJECT PATHS"); color: window.muted; font.weight: Font.Bold }
                                    BrutalButton { Layout.alignment: Qt.AlignHCenter; text: qsTr("Deep analyze"); enabled: !snapshotController.busy; onClicked: snapshotController.analyzeSelectedRepository() }
                                }
                            }
                        }
                        Item {
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 7
                                Label { text: qsTr("LARGEST LOCAL GIT BLOBS"); color: window.ink; font.family: "Space Grotesk"; font.pixelSize: 12; font.weight: Font.Bold }
                                ListView {
                                    id: objectUsageList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    visible: repositoryDetailPanel.analysis.ready
                                    clip: true
                                    spacing: 4
                                    model: repositoryDetailPanel.analysis.objects || []
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: objectUsageList.width
                                        height: 42
                                        color: modelData.current ? window.paper : modelData.retained ? "#fff0ad" : "#ffd7d1"
                                        border.color: window.ink
                                        border.width: 2
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 9
                                            anchors.rightMargin: 9
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 0
                                                Label { Layout.fillWidth: true; text: modelData.path; color: window.ink; elide: Text.ElideMiddle; font.pixelSize: 10; font.weight: Font.Bold }
                                                Label { text: modelData.shortOid + "  ·  " + (modelData.current ? qsTr("CURRENT") : modelData.retained ? qsTr("RETAINED HISTORY") : qsTr("UNPROTECTED HISTORY")); color: modelData.current ? window.blue : modelData.retained ? window.ink : window.red; font.family: "monospace"; font.pixelSize: 8; font.weight: Font.Bold }
                                            }
                                            Label { text: modelData.packedText + " / " + modelData.expandedText; color: window.ink; font.pixelSize: 9; font.weight: Font.Bold }
                                        }
                                    }
                                }
                                ColumnLayout {
                                    Layout.alignment: Qt.AlignCenter
                                    visible: !repositoryDetailPanel.analysis.ready
                                    Label { text: snapshotController.analysisBusy ? qsTr("VERIFYING OBJECTS…") : qsTr("INSPECT COMPRESSED BLOBS AND REACHABILITY"); color: window.muted; font.weight: Font.Bold }
                                    BrutalButton { Layout.alignment: Qt.AlignHCenter; text: qsTr("Deep analyze"); enabled: !snapshotController.busy; onClicked: snapshotController.analyzeSelectedRepository() }
                                }
                            }
                        }
                        Item {
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 0
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 36
                                    color: window.ink
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 13
                                        anchors.rightMargin: 13
                                        Label { text: qsTr("SNAPSHOT RECORDS"); color: window.white; font.family: "Space Grotesk"; font.pixelSize: 12; font.weight: Font.Bold; Layout.fillWidth: true }
                                        Rectangle { implicitWidth: 11; implicitHeight: 11; color: window.yellow; border.color: window.white; border.width: 1 }
                                        Label { text: qsTr("RETAINED"); color: window.white; font.pixelSize: 8; font.weight: Font.Bold }
                                        Rectangle { implicitWidth: 11; implicitHeight: 11; color: window.red; border.color: window.white; border.width: 1 }
                                        Label { text: qsTr("RELEASED"); color: window.white; font.pixelSize: 8; font.weight: Font.Bold }
                                    }
                                }
                                ListView {
                                    id: snapshotList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.margins: 12
                                    clip: true
                                    spacing: 7
                                    model: snapshotController.snapshots
                                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: snapshotList.width - (snapshotList.ScrollBar.vertical.visible ? 10 : 0)
                                        height: 88
                                        color: window.paper
                                        border.color: window.ink
                                        border.width: 2
                                        Rectangle { width: 10; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; color: modelData.keep ? window.yellow : window.red }
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 23
                                            anchors.rightMargin: 12
                                            anchors.topMargin: 9
                                            anchors.bottomMargin: 9
                                            spacing: 14
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 3
                                                Label { Layout.fillWidth: true; text: modelData.title; color: window.ink; elide: Text.ElideRight; font.family: "Space Grotesk"; font.pixelSize: 14; font.weight: Font.Bold }
                                                Label { text: modelData.shortHash + "  /  " + modelData.time + "  /  " + modelData.source.toUpperCase(); color: window.muted; font.family: "monospace"; font.pixelSize: 10 }
                                                Label { text: modelData.keep ? modelData.reason.toUpperCase() : qsTr("OUTSIDE RETENTION POLICY"); color: modelData.keep ? window.ink : window.red; font.pixelSize: 10; font.weight: Font.Bold }
                                            }
                                            Label { text: qsTr("%1 REFS\n%2 SESSIONS").arg(modelData.references).arg(modelData.sessions); horizontalAlignment: Text.AlignRight; color: window.ink; font.pixelSize: 10; font.weight: Font.Bold }
                                        }
                                        ToolTip.visible: snapshotHover.hovered
                                        ToolTip.text: modelData.hash
                                        HoverHandler { id: snapshotHover }
                                    }
                                    Label { anchors.centerIn: parent; visible: snapshotList.count === 0; text: qsTr("SELECT A REPOSITORY TO INSPECT SNAPSHOTS"); color: window.muted; font.weight: Font.Bold }
                                }
                            }
                        }
                        Item {
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8
                                RowLayout {
                                    Layout.fillWidth: true
                                    Rectangle {
                                        Layout.fillWidth: true; implicitHeight: 74; color: "#e8e1d6"; border.color: window.ink; border.width: 2
                                        Column { anchors.fill: parent; anchors.margins: 9; spacing: 3
                                            Label { text: qsTr("SAFE PROJECT GC"); color: window.ink; font.family: "Space Grotesk"; font.pixelSize: 11; font.weight: Font.Bold }
                                            Label { width: parent.width; text: repositoryDetailPanel.analysis.ready ? qsTr("Estimated unprotected objects: %1").arg(repositoryDetailPanel.analysis.estimatedReclaimableText) : qsTr("Run deep analysis for a project estimate"); color: window.muted; wrapMode: Text.Wrap; font.pixelSize: 9 }
                                        }
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true; implicitHeight: 74; color: "#ffd7d1"; border.color: window.ink; border.width: 2
                                        Column { anchors.fill: parent; anchors.margins: 9; spacing: 3
                                            Label { text: qsTr("RESET SNAPSHOT HISTORY"); color: window.red; font.family: "Space Grotesk"; font.pixelSize: 11; font.weight: Font.Bold }
                                            Label { width: parent.width; text: repositoryDetailPanel.analysis.ready ? qsTr("Current-only estimate: %1. Discards old Undo trees.").arg(repositoryDetailPanel.analysis.resetReclaimableText) : qsTr("Preserve current state, discard old Undo trees. OpenCode must be closed."); color: window.ink; wrapMode: Text.Wrap; font.pixelSize: 9 }
                                        }
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 67
                                    color: window.yellow
                                    border.color: window.ink
                                    border.width: 2
                                    Label { anchors.fill: parent; anchors.margins: 9; text: qsTr("Project reset will be enabled only after a dedicated preview proves that the current index tree can be recreated. Existing session Undo history for this project will no longer work after purge."); color: window.ink; wrapMode: Text.Wrap; font.pixelSize: 9; font.weight: Font.Bold }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    BrutalButton { text: repositoryDetailPanel.analysis.ready ? qsTr("Refresh analysis") : qsTr("Analyze first"); fillColor: window.blue; foregroundColor: window.white; hoverForegroundColor: window.blue; enabled: !snapshotController.busy; onClicked: snapshotController.analyzeSelectedRepository() }
                                    Item { Layout.fillWidth: true }
                                    BrutalButton {
                                        text: snapshotController.hasProjectPlan && snapshotController.projectPlanMode === "safe" ? qsTr("Review safe plan") : qsTr("Preview safe GC")
                                        fillColor: window.yellow
                                        enabled: !snapshotController.busy
                                        onClicked: snapshotController.hasProjectPlan && snapshotController.projectPlanMode === "safe" ? projectConfirmDialog.open() : snapshotController.previewProjectCleanup()
                                    }
                                    BrutalButton {
                                        text: snapshotController.hasProjectPlan && snapshotController.projectPlanMode === "reset" ? qsTr("Review reset plan") : qsTr("Preview history reset")
                                        fillColor: window.red
                                        foregroundColor: window.white
                                        hoverForegroundColor: window.red
                                        enabled: !snapshotController.busy && repositoryDetailPanel.analysis.ready
                                        onClicked: snapshotController.hasProjectPlan && snapshotController.projectPlanMode === "reset" ? projectConfirmDialog.open() : snapshotController.previewProjectReset()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: 88
            Rectangle { x: 5; y: 5; width: parent.width - 5; height: parent.height - 5; color: window.ink }
            Rectangle {
                width: parent.width - 5
                height: parent.height - 5
                color: window.paper
                border.color: window.ink
                border.width: 3
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 10
                    BusyIndicator { running: snapshotController.busy; visible: running; implicitWidth: 32; implicitHeight: 32; palette.highlight: window.blue }
                    Rectangle { implicitWidth: 8; Layout.fillHeight: true; color: window.blue }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: qsTr("BATCH OPERATIONS"); color: window.ink; font.family: "Space Grotesk"; font.pixelSize: 14; font.weight: Font.Bold; font.letterSpacing: 0.9 }
                        Label { text: snapshotController.status; color: window.muted; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 10; font.weight: Font.Medium; maximumLineCount: 2; elide: Text.ElideRight }
                    }
                    BatchButton {
                        step: "01"
                        actionLabel: qsTr("BATCH PREVIEW")
                        detail: snapshotController.repositoryCount > 0
                                ? qsTr("%1 REPOSITORIES · READ ONLY").arg(snapshotController.repositoryCount)
                                : qsTr("SCAN REQUIRED")
                        fillColor: window.blue
                        foregroundColor: window.white
                        hoverForegroundColor: window.blue
                        enabled: !snapshotController.busy && snapshotController.repositoryCount > 0
                        onClicked: snapshotController.previewCleanup()
                    }
                    Label { text: ">"; color: snapshotController.hasPlan ? window.ink : window.muted; font.family: "Space Grotesk"; font.pixelSize: 24; font.weight: Font.Bold }
                    BatchButton {
                        step: "02"
                        actionLabel: qsTr("BATCH CLEAN")
                        detail: snapshotController.hasPlan
                                ? qsTr("%1 TREES READY TO RELEASE").arg(snapshotController.planRemoveTrees)
                                : qsTr("LOCKED · RUN PREVIEW FIRST")
                        fillColor: window.red
                        foregroundColor: window.white
                        hoverForegroundColor: window.red
                        enabled: !snapshotController.busy && snapshotController.hasPlan
                        onClicked: confirmDialog.open()
                    }
                }
            }
        }
    }

    Dialog {
        id: settingsDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(window.width - 80, 820)
        padding: 24
        closePolicy: Popup.CloseOnEscape
        Overlay.modal: Rectangle { color: "#991a1a1a" }
        background: Rectangle { color: window.paper; border.color: window.ink; border.width: 3 }
        header: Rectangle {
            implicitHeight: 66
            color: window.blue
            border.color: window.ink
            border.width: 3
            Label { anchors.fill: parent; anchors.margins: 17; text: qsTr("SCAN / RETENTION SETTINGS"); color: window.white; font.family: "Space Grotesk"; font.pixelSize: 22; font.weight: Font.Bold; verticalAlignment: Text.AlignVCenter }
        }
        footer: Item {
            implicitHeight: 68
            BrutalButton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Close"); onClicked: settingsDialog.close() }
        }
        contentItem: ColumnLayout {
            spacing: 13
            Label { text: qsTr("SNAPSHOT DIRECTORY"); color: window.ink; font.weight: Font.Bold; font.letterSpacing: 0.8 }
            RowLayout {
                Layout.fillWidth: true
                BrutalField { Layout.fillWidth: true; text: snapshotController.snapshotRoot; onEditingFinished: snapshotController.snapshotRoot = text }
                BrutalButton { text: qsTr("Browse…"); fillColor: window.paper; hoverForegroundColor: window.paper; onClicked: snapshotController.chooseSnapshotRoot() }
            }
            Label { text: qsTr("OPENCODE DATABASE"); color: window.ink; font.weight: Font.Bold; font.letterSpacing: 0.8 }
            RowLayout {
                Layout.fillWidth: true
                BrutalField { Layout.fillWidth: true; text: snapshotController.databasePath; onEditingFinished: snapshotController.databasePath = text }
                BrutalButton { text: qsTr("Browse…"); fillColor: window.paper; hoverForegroundColor: window.paper; onClicked: snapshotController.chooseDatabase() }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Retain snapshots from the last"); color: window.ink; Layout.fillWidth: true }
                NumberStepper { value: snapshotController.recentDays; minimum: 0; maximum: 3650; onValueEdited: snapshotController.recentDays = nextValue }
                Label { text: qsTr("days"); color: window.ink; font.weight: Font.Bold }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("If none are recent, retain newest per repository"); color: window.ink; Layout.fillWidth: true }
                NumberStepper { value: snapshotController.fallbackCount; minimum: 1; maximum: 1000; onValueEdited: snapshotController.fallbackCount = nextValue }
            }
            BrutalCheckBox { Layout.fillWidth: true; text: qsTr("Run full Git garbage collection"); checked: snapshotController.fullGc; onToggled: snapshotController.fullGc = checked }
            BrutalCheckBox { Layout.fillWidth: true; text: qsTr("Remove Git LFS objects not referenced by retained trees"); checked: snapshotController.pruneLfs; onToggled: snapshotController.pruneLfs = checked }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Remove temporary packs and empty index locks older than"); color: window.ink; Layout.fillWidth: true }
                NumberStepper { value: snapshotController.staleFileHours; minimum: 1; maximum: 8760; onValueEdited: snapshotController.staleFileHours = nextValue }
                Label { text: qsTr("hours"); color: window.ink; font.weight: Font.Bold }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: settingsNote.implicitHeight + 22
                color: window.yellow
                border.color: window.ink
                border.width: 2
                Label {
                    id: settingsNote
                    anchors.fill: parent
                    anchors.margins: 11
                    text: qsTr("A setting change invalidates the current cleanup preview. Path changes require another scan; retention changes update the current result immediately.")
                    color: window.ink
                    wrapMode: Text.Wrap
                    font.weight: Font.Bold
                }
            }
        }
    }

    Dialog {
        id: confirmDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 620
        padding: 24
        closePolicy: Popup.CloseOnEscape
        Overlay.modal: Rectangle { color: "#991a1a1a" }
        background: Rectangle { color: window.paper; border.color: window.ink; border.width: 3 }
        header: Rectangle {
            implicitHeight: 66
            color: window.red
            border.color: window.ink
            border.width: 3
            Label { anchors.fill: parent; anchors.margins: 17; text: qsTr("CONFIRM BATCH CLEANUP"); color: window.white; font.family: "Space Grotesk"; font.pixelSize: 22; font.weight: Font.Bold; verticalAlignment: Text.AlignVCenter }
        }
        footer: Item {
            implicitHeight: 72
            RowLayout {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12
                BrutalButton { text: qsTr("Cancel"); fillColor: window.paper; hoverForegroundColor: window.paper; onClicked: confirmDialog.reject() }
                BrutalButton {
                    text: qsTr("Batch clean")
                    fillColor: window.red
                    foregroundColor: window.white
                    hoverForegroundColor: window.red
                    onClicked: {
                        confirmDialog.accept()
                        snapshotController.executeCleanup()
                    }
                }
            }
        }
        contentItem: ColumnLayout {
            spacing: 14
            Label { Layout.fillWidth: true; text: qsTr("This applies the reviewed plan across all %1 repositories. Retained trees are protected with private Git refs before unreachable Git/LFS data is pruned.").arg(snapshotController.repositoryCount); color: window.ink; wrapMode: Text.Wrap }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: warningLabel.implicitHeight + 24
                color: window.yellow
                border.color: window.ink
                border.width: 3
                Label { id: warningLabel; anchors.fill: parent; anchors.margins: 12; text: qsTr("Close OpenCode or ensure it is idle before continuing. Cleanup cannot be undone from this tool."); color: window.ink; wrapMode: Text.Wrap; font.weight: Font.Bold }
            }
            Label { text: qsTr("PROTECT %1 TREES  /  RELEASE %2 TREES").arg(snapshotController.planKeepTrees).arg(snapshotController.planRemoveTrees); color: window.red; font.family: "Space Grotesk"; font.pixelSize: 18; font.weight: Font.Bold }
            Label { Layout.fillWidth: true; text: qsTr("Directly removable LFS/temp files: %1. Git pack savings are measured after cleanup.").arg(snapshotController.formatBytes(snapshotController.estimatedReclaimableBytes)); color: window.blue; wrapMode: Text.Wrap; font.weight: Font.Bold }
        }
    }

    Dialog {
        id: projectConfirmDialog
        property bool resetMode: snapshotController.projectPlanMode === "reset"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(window.width - 80, 760)
        padding: 24
        closePolicy: Popup.CloseOnEscape
        onOpened: {
            projectClosedCheck.checked = false
            projectNameConfirm.text = ""
        }
        Overlay.modal: Rectangle { color: "#991a1a1a" }
        background: Rectangle { color: window.paper; border.color: window.ink; border.width: 3 }
        header: Rectangle {
            implicitHeight: 70
            color: projectConfirmDialog.resetMode ? window.red : window.yellow
            border.color: window.ink
            border.width: 3
            Label {
                anchors.fill: parent
                anchors.margins: 17
                text: projectConfirmDialog.resetMode ? qsTr("CONFIRM HISTORY RESET") : qsTr("CONFIRM PROJECT CLEANUP")
                color: projectConfirmDialog.resetMode ? window.white : window.ink
                font.family: "Space Grotesk"
                font.pixelSize: 22
                font.weight: Font.Bold
                verticalAlignment: Text.AlignVCenter
            }
        }
        footer: Item {
            implicitHeight: 76
            RowLayout {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12
                BrutalButton { text: qsTr("Cancel"); fillColor: window.paper; hoverForegroundColor: window.paper; onClicked: projectConfirmDialog.reject() }
                BrutalButton {
                    text: projectConfirmDialog.resetMode ? qsTr("Reset history") : qsTr("Clean project")
                    fillColor: projectConfirmDialog.resetMode ? window.red : window.yellow
                    foregroundColor: projectConfirmDialog.resetMode ? window.white : window.ink
                    hoverForegroundColor: projectConfirmDialog.resetMode ? window.red : window.yellow
                    enabled: projectClosedCheck.checked && (!projectConfirmDialog.resetMode || projectNameConfirm.text === repositoryDetailPanel.details.name)
                    onClicked: {
                        projectConfirmDialog.accept()
                        snapshotController.executeProjectAction()
                    }
                }
            }
        }
        contentItem: ColumnLayout {
            spacing: 13
            Label { text: qsTr("SELECTED PROJECT"); color: window.ink; font.weight: Font.Bold; font.letterSpacing: 0.8 }
            Label { Layout.fillWidth: true; text: repositoryDetailPanel.details.name; color: window.blue; wrapMode: Text.WrapAnywhere; font.family: "monospace"; font.weight: Font.Bold }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: projectActionWarning.implicitHeight + 24
                color: projectConfirmDialog.resetMode ? "#ffd7d1" : window.yellow
                border.color: window.ink
                border.width: 3
                Label {
                    id: projectActionWarning
                    anchors.fill: parent
                    anchors.margins: 12
                    text: projectConfirmDialog.resetMode
                          ? qsTr("Keep the live index tree and remove historical snapshot objects. Old session Undo operations for this project may stop working. This cannot be undone by this tool.")
                          : qsTr("Keep the current and retained trees, then prune only unprotected Git/LFS data. The measured result is shown after cleanup.")
                    color: window.ink
                    wrapMode: Text.Wrap
                    font.weight: Font.Bold
                }
            }
            Label {
                Layout.fillWidth: true
                text: projectConfirmDialog.resetMode
                      ? qsTr("CURRENT-ONLY ESTIMATE %1  /  RELEASE %2 KNOWN TREES").arg(repositoryDetailPanel.analysis.resetReclaimableText).arg(snapshotController.projectPlanRemoveTrees)
                      : qsTr("SAFE ESTIMATE %1  /  RELEASE %2 KNOWN TREES").arg(repositoryDetailPanel.analysis.estimatedReclaimableText).arg(snapshotController.projectPlanRemoveTrees)
                color: projectConfirmDialog.resetMode ? window.red : window.blue
                wrapMode: Text.Wrap
                font.family: "Space Grotesk"
                font.pixelSize: 16
                font.weight: Font.Bold
            }
            BrutalCheckBox {
                id: projectClosedCheck
                Layout.fillWidth: true
                text: qsTr("I closed OpenCode and no OpenCode process is using this project")
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: projectConfirmDialog.resetMode
                spacing: 6
                Label { Layout.fillWidth: true; text: qsTr("TYPE THE EXACT PROJECT NAME TO ENABLE RESET"); color: window.red; wrapMode: Text.Wrap; font.weight: Font.Bold; font.letterSpacing: 0.5 }
                BrutalField { id: projectNameConfirm; Layout.fillWidth: true; placeholderText: repositoryDetailPanel.details.name }
            }
            Label { Layout.fillWidth: true; text: qsTr("Execution also refuses a history reset if Git lock files are present. The current index tree is checked again and protected immediately before cleanup."); color: window.muted; wrapMode: Text.Wrap; font.pixelSize: 10 }
        }
    }

    Component.onCompleted: snapshotController.scan()

    Rectangle { anchors.fill: parent; color: "transparent"; border.color: window.ink; border.width: 3; z: 1000 }
    MouseArea { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 5; cursorShape: Qt.SizeHorCursor; z: 1001; onPressed: window.startSystemResize(Qt.LeftEdge) }
    MouseArea { anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 5; cursorShape: Qt.SizeHorCursor; z: 1001; onPressed: window.startSystemResize(Qt.RightEdge) }
    MouseArea { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 5; cursorShape: Qt.SizeVerCursor; z: 1001; onPressed: window.startSystemResize(Qt.TopEdge) }
    MouseArea { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 5; cursorShape: Qt.SizeVerCursor; z: 1001; onPressed: window.startSystemResize(Qt.BottomEdge) }
}

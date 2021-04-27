// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

import QtQuick
import DBusSysIWD
import DBusSysSystemdNetworkd

Rectangle {
    id: root
    color: "black"
    anchors.fill: parent

    component QIWDText: Text {
        font.pointSize: d.decentFontHeight
        color: "white"
    }

    component QIWDLabel: Rectangle {
        color: "black"
        height: d.decentFontHeight + 10
        width: d.decentFontHeight*2
        property alias text: labelText.text
        QIWDText {
            id: labelText
            anchors.centerIn: parent
        }
    }


    component QIWDButton: Rectangle {
        property alias text: buttonText.text
        property int padding: 30

        function onButtonPressed() {
            console.log("button pressed")
        }

        color: "hotpink"
        height: buttonText.height + padding
        width: buttonText.width + padding
        border { color: "#40FF0000"; width: 1; }
        QIWDText {
            id: buttonText
            text: "forget"
            anchors.centerIn: parent
        }
        MouseArea {
            anchors.fill: parent
            onPressed: {
                parent.onButtonPressed()
            }
        }
    }

    IWD {
        id: iwd
    }

    SystemdNetworkd {
        id: networkState
        verboseLogging: true
    }

    QtObject {
        id: d
        property var networkId: ""
        property var ssid: ""

        property bool showMetadata: false
        property int decentFontHeight: root.height > 0 ? root.height/24 : 10
    }

    function onPasswordEntered() {
        iwd.preparePassword(d.networkId, passwordInput.text)
        iwd.connectNetwork(d.networkId)
        passwordEntry.visible = false
    }

    Item {
        anchors { fill: parent; topMargin: 15; leftMargin: 6; rightMargin: 7; bottomMargin: 7 }


        QIWDButton {
            id: stateButton
            text: iwd.scanning ? "scanning" : iwd.state
            anchors { top: parent.top; left: parent.left; right: parent.right }
            function onButtonPressed() {
                d.showMetadata = !d.showMetadata
            }
            QIWDText {
                visible: iwd.connected
                anchors { right: parent.right; rightMargin: 20; verticalCenter: parent.verticalCenter }
                text: iwd.signalStrengthLevel
                color: "black"
            }
        }

        Flickable {
            clip: true
            anchors { left: parent.left; right: parent.right; top: stateButton.bottom; bottom: buttonRow.top }
            contentHeight: manifesto.height
            contentWidth: manifesto.width
            Column {
                id: manifesto
                visible: d.showMetadata
                Repeater {
                    model: [ "model", "name", "powered", "vendor", "adapters", "devices", "supportedModes", "possibleStates" ]
                    delegate: QIWDText {
                        text: modelData + ":" + iwd[modelData]
                    }
                }
                Repeater {
                    model: [ "operationalState", "carrierState", "addressState", "onlineState", "linkInterfaces" ]
                    delegate: QIWDText {
                        text: {
                            const enums = {
                                operationalState: SystemdNetworkd.OperationalState,
                                carrierState: SystemdNetworkd.CarrierState,
                                addressState: SystemdNetworkd.AddressState,
                                onlineState: SystemdNetworkd.OnlineState
                            }
                            const value = networkState[modelData]
                            return modelData + ":" + (modelData === "linkInterfaces"
                                ? value : Qt.enumValueToString(enums[modelData], value))
                        }
                    }
                }
                Column {
                    visible: iwd.connected
                    QIWDText {
                        text: "Connected Network"
                    }
                    Repeater {
                        model: [ "ssid", "autoconnect", "hidden", "signalStrength", "type" ]
                        delegate: QIWDText {
                            text: modelData + ":" + iwd.connectedNetworkData[modelData]
                        }
                    }
                }
            }
        }

        ListView {
            visible: !d.showMetadata
            clip: true
            model: iwd.networks
            anchors { left: parent.left; right: parent.right; top: stateButton.bottom; bottom: buttonRow.top }
            delegate: Item {
                id: wifiDelegate
                height: root.height/5
                width: root.width

                enabled: type !== "8021x"
                opacity: enabled ? 1.0 : 0.5

                property bool connectable: known || type === "open"

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (connectable) {
                            console.log("Connecting to known or open network")
                            iwd.connectNetwork(networkId)
                            return
                        }

                        if (connected) {
                            console.log("Being told to connect to connected network")
                            return
                        }

                        d.networkId = networkId
                        d.ssid = ssid
                        passwordEntry.visible = true
                    }
                }

                Rectangle {
                    id: networkStatus
                    height: parent.height
                    width: 10
                    color: connected ? "blue"
                                     : connectable ? "green" : "invisible"
                    anchors { left: parent.left; }
                }

                QIWDText {
                    id: ssidLabel
                    text: ssid
                    anchors { left: networkStatus.right; leftMargin: 5; verticalCenter: parent.verticalCenter }
                }

                Row {
                    height: parent.height
                    anchors { right: parent.right; rightMargin: 10 }
                    spacing: 10
                    Row {
                        visible: known
                        QIWDButton {
                            text: "forget"
                            function onButtonPressed() {
                                iwd.forgetNetwork(networkId)
                            }
                        }
                        QIWDButton {
                            text: autoconnect ? "autoconnect" : "manual"
                            function onButtonPressed() {
                                iwd.setAutoConnectNetwork(networkId, !autoconnect)
                            }
                        }
                    }
                    QIWDLabel {
                        color: "black"
                        height: parent.height
                        text: signalStrength
                    }
                }
            }
        }

        Row {
            id: buttonRow
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: visibleChildren.length > 0 ? scanButton.height : 0
            QIWDButton {
                id: scanButton
                visible: !iwd.scanning
                text: "Scan"
                width: parent.width/parent.visibleChildren.length
                function onButtonPressed() {
                    iwd.scan()
                }
            }
            QIWDButton {
                text: "Disconnect"
                visible: iwd.connected
                width: parent.width/parent.visibleChildren.length
                function onButtonPressed() {
                    iwd.disconnectNetwork()
                }
            }
        }
        Item {
            id: passwordEntry
            visible: false

            anchors.fill: parent

            onVisibleChanged: visible ? passwordInput.forceActiveFocus() : passwordInput.text = ""

            Rectangle {
                color: "black"
                opacity: 0.9
                anchors.fill: parent
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        passwordEntry.visible = false
                    }
                }
            }

            Column {
                id: passwordDialog
                anchors.centerIn: parent
                width: passwordLabel.width > 200 ? passwordLabel.width : 200
                spacing: 10
                QIWDText {
                    id: passwordLabel
                    text: d.ssid
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                QIWDText {
                    text: "password"
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Rectangle {
                    color: "white"
                    height: childrenRect.height
                    width: passwordDialog.width
                    TextInput {
                        id: passwordInput
                        color: "black"
                        echoMode: TextInput.Password
                        //width: implicitWidth > passwordLabel.width ? implicitWidth : passwordLabel.width
                        width: passwordLabel.width
                        onAccepted: root.onPasswordEntered()
                        Keys.onEscapePressed: passwordEntry.visible = false
                    }
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }
}

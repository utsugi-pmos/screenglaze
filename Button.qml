// SPDX-License-Identifier: LGPL-2.0-or-later
//
// One action in the bottom row: a round icon with its name underneath.
//
// Round-with-a-label rather than a plain toolbar button because this sheet
// appears over anything at all, and a label is what stops "the arrow one" from
// being a guess. It is also how every phone draws this exact row.
import QtQuick

Item {
	id: root

	property string shape                     // one of Glaze.shape*
	property string text
	property color tint: Glaze.ink
	property bool highlighted: false
	signal clicked()

	implicitWidth: Math.max(Glaze.touch, label.implicitWidth + 12)
	implicitHeight: disc.height + label.height + 8

	Rectangle {
		id: disc
		width: Glaze.touch
		height: width
		radius: width / 2
		anchors.horizontalCenter: parent.horizontalCenter
		anchors.top: parent.top
		color: root.highlighted ? Glaze.accent
			: (tap.pressed ? Glaze.discDown : Glaze.disc)

		// The press is felt before it is seen: the colour change alone is too
		// slow to acknowledge a tap on a touchscreen.
		scale: tap.pressed ? 0.92 : 1.0
		Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
		Behavior on color { ColorAnimation { duration: 90 } }

		Image {
			anchors.centerIn: parent
			width: 24
			height: 24
			sourceSize: Qt.size(48, 48)   // rendered at 2x so it stays crisp
			source: Glaze.icon(root.shape, root.tint)
			smooth: true
		}
	}

	Text {
		id: label
		anchors.top: disc.bottom
		anchors.topMargin: 8
		anchors.horizontalCenter: parent.horizontalCenter
		text: root.text
		color: Glaze.inkSoft
		font.pixelSize: 13
	}

	MouseArea {
		id: tap
		anchors.fill: parent
		onClicked: {
			// Every tap buzzes. On a sheet that covers the screen there is no
			// other confirmation that the press landed on the right circle.
			app.feedback("button-pressed")
			root.clicked()
		}
	}
}

// SPDX-License-Identifier: LGPL-2.0-or-later
//
// The share sheet, shaped like a phone.
//
// WHY NOT THE PORTAL'S "OPEN WITH", which is what was here before.
// ----------------------------------------------------------------
// org.freedesktop.portal.OpenURI with 'ask' opens Plasma's application chooser:
// a desktop window that does not fit in 1080x2400. And it is useless here anyway
// -- on this phone the ONLY application that declares it knows how to open a PNG
// is Koko, the photo viewer. "Open with" is not "share".
//
// WHAT DOES EXIST ON LINUX. There are no intents like on Android: no program can
// declare itself a "share target" with its own screen to pick a contact. The
// closest thing is Purpose, KDE's framework, and it turns out it was already
// installed with 16 targets -- Telegram among them, and its one opens Telegram's
// "send to" where you choose the chat. Which is exactly what was missing.
//
// Purpose ships its own AlternativesView, but it looks like a desktop widget.
// Here the model is used and the list is painted by hand: tall rows, big icons,
// sliding up from the bottom.
import QtQuick
import org.kde.purpose as Purpose

Item {
	id: root

	// file:// of what is being shared. Empty while there is nothing.
	property url target
	property real input: 0        // 0 off screen, 1 all the way up

	signal closed(bool shared)

	// THE TARGETS THAT BEHAVE WELL ON A PHONE, and only those.
	//
	// Purpose offers 16, but several deliver the file by opening a desktop
	// application's window. Telegram's, for example, runs
	// 'Telegram -sendpath <file>': the handoff works, but what comes up is
	// Telegram's dialog to pick a chat, and the mobile desktop does not know how
	// to handle it -- it floats there with no way to close it. That is Telegram's
	// UI; it cannot be fixed from here.
	//
	// The ones in this list either open no window at all, or open one of Plasma's
	// own choosers, which is adapted to this screen.
	//
	// To add one: look at 'ls /usr/lib/qt6/plugins/kf6/purpose/' and TRY IT on the
	// phone before keeping it, which is exactly the step that was missing.
	readonly property var allowed: [
		"clipboard",      // copy to the clipboard: opens nothing
		"saveas",         // save as: Plasma's file chooser
		"bluetooth",      // send over Bluetooth: Plasma's chooser
		"kdeconnect",     // send it to another device: own menu
		"kdeconnectsms"
	]

	function open_it(url) {
		target = url
		input = 0
		visible = true
		slideUp.restart()
	}

	visible: false

	// The backdrop dims and swallows taps: below it is the screenshot, and a tap
	// that got through would close the screenshot sheet behind it.
	Rectangle {
		anchors.fill: parent
		color: "#000000"
		opacity: 0.55 * root.input
		MouseArea {
			anchors.fill: parent
			onClicked: root.leave(false)
		}
	}

	function leave(shared) {
		slideDown.shared = shared
		slideDown.restart()
	}

	NumberAnimation {
		id: slideUp
		target: root; property: "input"; from: 0; to: 1
		duration: Glaze.quick; easing.type: Easing.OutCubic
	}
	SequentialAnimation {
		id: slideDown
		property bool shared: false
		NumberAnimation {
			target: root; property: "input"; to: 0
			duration: Glaze.quick; easing.type: Easing.InCubic
		}
		ScriptAction {
			script: {
				root.visible = false
				root.closed(slideDown.shared)
			}
		}
	}

	// --- the sheet ------------------------------------------------------------
	Rectangle {
		id: sheet
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		// Rises up from below the edge. Natural height -- AlternativesView exposes
		// its list's height in implicitHeight -- but never more than two thirds of
		// the screen: with 16 targets it would run off the top.
		height: Math.min(64 + alternatives.implicitHeight + 24,
			root.height * 0.66)
		y: root.height - height * root.input
		color: Glaze.sheet
		topLeftRadius: Glaze.radius
		topRightRadius: Glaze.radius

		// The handle. It does nothing -- there is no drag gesture -- but it is
		// what says "this rises from the bottom and can be closed", and without it
		// the sheet looks like a window that appeared out of nowhere.
		Rectangle {
			id: handle
			anchors.horizontalCenter: parent.horizontalCenter
			anchors.top: parent.top
			anchors.topMargin: 10
			width: 38
			height: 4
			radius: 2
			color: Glaze.inkSoft
			opacity: 0.5
		}

		Text {
			id: header
			anchors.top: handle.bottom
			anchors.topMargin: 14
			anchors.left: parent.left
			anchors.leftMargin: 22
			text: qsTr("Share")
			color: Glaze.ink
			font.pixelSize: 17
			font.bold: true
		}

		Purpose.AlternativesView {
			id: alternatives
			anchors.top: header.bottom
			anchors.topMargin: 12
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.bottom: parent.bottom
			clip: true

			// "Export" is the type of the share targets. The mimeType is needed as
			// well as the URL: without it, plugins that only accept images are not
			// filtered out and targets that would fail on use show up.
			pluginType: "Export"
			inputData: {
				"urls": [ root.target.toString() ],
				"mimeType": "image/png"
			}

			// Purpose switches to its progress view by itself when a job starts.
			// When it finishes, the sheet goes away.
			onRunningChanged: if (!running && wasUsed) root.leave(true)
			property bool wasUsed: false

			delegate: Item {
				id: row
				// The names have to match the roles of Purpose's model.
				// 'actionDisplay' already comes translated by the plugin -- the
				// Telegram one brings "Send via Telegram".
				required property string actionDisplay
				required property string iconName
				required property string pluginId
				required property int index

				// The ones not in the list collapse to zero height instead of being
				// filtered into a separate model: AlternativesView carries its own
				// proxy inside and does not let you attach a filter. Since the list's
				// height is the sum of the delegates, collapsing shrinks the sheet
				// just as if they did not exist -- and the indices createJob()
				// expects stay the same.
				readonly property bool allowed:
					root.allowed.indexOf(pluginId) >= 0

				width: ListView.view ? ListView.view.width : 0
				height: allowed ? 62 : 0
				visible: allowed

				Rectangle {
					anchors.fill: parent
					color: toque.pressed ? Glaze.discDown : "transparent"
				}

				Image {
					id: icon
					anchors.left: parent.left
					anchors.leftMargin: 22
					anchors.verticalCenter: parent.verticalCenter
					width: 30
					height: 30
					// From the icon theme, served by the provider main.cpp
					// registers. In bare QtQuick there is no way to paint an icon by
					// name, and these names are set by each plugin.
					source: "image://icon/" + row.iconName
					sourceSize: Qt.size(60, 60)
					smooth: true
				}

				Text {
					anchors.left: icon.right
					anchors.leftMargin: 18
					anchors.right: parent.right
					anchors.rightMargin: 22
					anchors.verticalCenter: parent.verticalCenter
					text: row.actionDisplay
					color: Glaze.ink
					font.pixelSize: 15
					elide: Text.ElideRight
				}

				MouseArea {
					id: toque
					anchors.fill: parent
					onClicked: {
						app.feedback("button-pressed")
						alternatives.wasUsed = true
						alternatives.createJob(row.index)
					}
				}
			}
		}
	}
}

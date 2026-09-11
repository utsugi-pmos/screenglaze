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
	property url objetivo
	property real input: 0        // 0 off screen, 1 all the way up

	signal cerrado(bool compartido)

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
		objetivo = url
		input = 0
		visible = true
		subir.restart()
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
			onClicked: root.salir(false)
		}
	}

	function salir(compartido) {
		bajar.compartido = compartido
		bajar.restart()
	}

	NumberAnimation {
		id: subir
		target: root; property: "input"; from: 0; to: 1
		duration: Glaze.quick; easing.type: Easing.OutCubic
	}
	SequentialAnimation {
		id: bajar
		property bool compartido: false
		NumberAnimation {
			target: root; property: "input"; to: 0
			duration: Glaze.quick; easing.type: Easing.InCubic
		}
		ScriptAction {
			script: {
				root.visible = false
				root.cerrado(bajar.compartido)
			}
		}
	}

	// --- the sheet ------------------------------------------------------------
	Rectangle {
		id: hoja
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		// Rises up from below the edge. Natural height -- AlternativesView exposes
		// its list's height in implicitHeight -- but never more than two thirds of
		// the screen: with 16 targets it would run off the top.
		height: Math.min(64 + alternativas.implicitHeight + 24,
			root.height * 0.66)
		y: root.height - height * root.input
		color: Glaze.sheet
		topLeftRadius: Glaze.radius
		topRightRadius: Glaze.radius

		// The handle. It does nothing -- there is no drag gesture -- but it is
		// what says "this rises from the bottom and can be closed", and without it
		// the sheet looks like a window that appeared out of nowhere.
		Rectangle {
			id: tirador
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
			id: cabecera
			anchors.top: tirador.bottom
			anchors.topMargin: 14
			anchors.left: parent.left
			anchors.leftMargin: 22
			text: qsTr("Share")
			color: Glaze.ink
			font.pixelSize: 17
			font.bold: true
		}

		Purpose.AlternativesView {
			id: alternativas
			anchors.top: cabecera.bottom
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
				"urls": [ root.objetivo.toString() ],
				"mimeType": "image/png"
			}

			// Purpose switches to its progress view by itself when a job starts.
			// When it finishes, the sheet goes away.
			onRunningChanged: if (!running && seHaUsado) root.salir(true)
			property bool seHaUsado: false

			delegate: Item {
				id: fila
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
				readonly property bool permitido:
					root.allowed.indexOf(pluginId) >= 0

				width: ListView.view ? ListView.view.width : 0
				height: permitido ? 62 : 0
				visible: permitido

				Rectangle {
					anchors.fill: parent
					color: toque.pressed ? Glaze.discDown : "transparent"
				}

				Image {
					id: icono
					anchors.left: parent.left
					anchors.leftMargin: 22
					anchors.verticalCenter: parent.verticalCenter
					width: 30
					height: 30
					// From the icon theme, served by the provider main.cpp
					// registers. In bare QtQuick there is no way to paint an icon by
					// name, and these names are set by each plugin.
					source: "image://icono/" + fila.iconName
					sourceSize: Qt.size(60, 60)
					smooth: true
				}

				Text {
					anchors.left: icono.right
					anchors.leftMargin: 18
					anchors.right: parent.right
					anchors.rightMargin: 22
					anchors.verticalCenter: parent.verticalCenter
					text: fila.actionDisplay
					color: Glaze.ink
					font.pixelSize: 15
					elide: Text.ElideRight
				}

				MouseArea {
					id: toque
					anchors.fill: parent
					onClicked: {
						app.feedback("button-pressed")
						alternativas.seHaUsado = true
						alternativas.createJob(fila.index)
					}
				}
			}
		}
	}
}

// SPDX-License-Identifier: LGPL-2.0-or-later
//
// The overlay. Invisible almost all of the time -- this process spends its life
// waiting for phone-keyconfig to call shoot() over D-Bus, and this window only
// exists for the few seconds after that call.
//
// It is a LAYER-SHELL surface, not an ordinary window. That is what lets it
// cover the panel and the app underneath without ever becoming an entry in the
// task switcher, which is exactly how a phone's screenshot sheet behaves. An
// ordinary window would appear in the switcher, could be swiped away
// half-drawn, and would put the screenshot sheet in the list of things you can
// alt-tab to.
//
// THE ORDER OF THE FLASH. The white flash plays HERE, after the picture is
// already on disk -- never before. This window is a window like any other, so a
// flash drawn before the shutter would be photographed along with everything
// else. What happens at the moment the buttons are pressed is the shutter click
// and the buzz (see Backend::shoot), because sound and vibration are the only
// feedback a camera cannot capture.
import QtQuick
import org.kde.layershell as LayerShell

Window {
	id: root

	visible: false
	color: "transparent"
	flags: Qt.FramelessWindowHint

	// Cover the whole output: anchored to all four edges, the compositor sizes
	// the surface for us.
	LayerShell.Window.layer: LayerShell.Window.LayerOverlay
	LayerShell.Window.anchors: LayerShell.Window.AnchorTop
		| LayerShell.Window.AnchorBottom
		| LayerShell.Window.AnchorLeft
		| LayerShell.Window.AnchorRight
	// -1 means "do not reserve any space, and do not respect anyone else's".
	// With the default the panel would push the overlay down and the picture
	// would not line up with what was photographed.
	LayerShell.Window.exclusionZone: -1
	LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityOnDemand
	LayerShell.Window.scope: "screenglaze"

	// A size for the case where layer-shell is off (SCREENGLAZE_SIN_CAPA=1).
	// With it on, the compositor overrides both.
	width: Screen.width
	height: Screen.height

	property real scrimAmount: 0
	property bool cropping: false

	Behavior on scrimAmount {
		NumberAnimation { duration: Glaze.quick; easing.type: Easing.OutQuad }
	}

	Connections {
		target: app
		function onShowSheet() { root.open() }
		function onHideSheet() { root.close() }
	}

	function open() {
		// Stop the previous exit dead. Its tail sets visible = false after the
		// toast, and a second screenshot taken during that second and a half
		// would be shown and then immediately hidden by an animation that
		// belonged to the one before it.
		outro.stop()
		toast.opacity = 0
		cropping = false
		sheet.opacity = 1
		sheet.scale = 1
		visible = true
		requestActivate()
		scrimAmount = Glaze.scrimOpacity
		flash.play()
		sheet.enter()
	}

	function close() {
		if (!visible)
			return
		outro.restart()
	}

	// --- the dimmed backdrop ------------------------------------------------
	Rectangle {
		anchors.fill: parent
		color: Glaze.scrim
		opacity: root.scrimAmount
	}

	// Tapping the backdrop does NOT dismiss. Every other sheet on a phone can
	// be dismissed by tapping outside, but here that gesture would throw away
	// a screenshot you have not looked at yet, and the four buttons are
	// unambiguous. It swallows the tap so it cannot reach the app underneath.
	MouseArea {
		anchors.fill: parent
		onClicked: app.feedback("button-pressed")
	}

	// --- the sheet ----------------------------------------------------------
	Sheet {
		id: sheet
		anchors.fill: parent
		visible: !root.cropping
		transformOrigin: Item.Center
		onShareRequested: {
			// Save first: what gets shared is the copy in the gallery, not the
			// throwaway file in ~/.cache we are about to delete.
			const u = app.shareUrl()
			if (u.length > 0)
				share.open_it(u)
		}
		onCropRequested: {
			cropper.picture = sheet.pictureRect
			cropper.reset()
			root.cropping = true
		}
	}

	// --- the cropper --------------------------------------------------------
	Cropper {
		id: cropper
		anchors.fill: parent
		visible: root.cropping
		onCancelled: root.cropping = false
		onAccepted: (x, y, w, h) => {
			// If the crop is refused -- too small to be meaningful -- stay in
			// the cropper rather than closing it as if something had happened.
			if (app.crop(x, y, w, h)) {
				root.cropping = false
				// settle(), not enter(): replaying the shrink would blow the
				// cropped picture back up to full screen first, which looks
				// like the crop had been undone.
				sheet.settle()
			}
		}
	}

	// --- the share sheet ------------------------------------------------------
	// Above the screenshot sheet and below the flash.
	Share {
		id: share
		anchors.fill: parent
		onClosed: (shared) => app.sharedDone(shared)
	}

	// --- the flash ----------------------------------------------------------
	// Last in the file, so it is on top of everything else.
	Rectangle {
		id: flash
		anchors.fill: parent
		color: "#ffffff"
		opacity: 0
		// It must never eat a tap: by the time anyone could touch it, it is
		// already invisible but still there.
		visible: opacity > 0

		function play() { blink.restart() }

		SequentialAnimation {
			id: blink
			// In fast, out slow. A symmetric fade reads as the screen glitching;
			// this reads as a shutter.
			NumberAnimation {
				target: flash; property: "opacity"; from: 0; to: 0.85
				duration: Glaze.flashIn; easing.type: Easing.OutQuad
			}
			NumberAnimation {
				target: flash; property: "opacity"; to: 0
				duration: Glaze.flashOut; easing.type: Easing.InQuad
			}
		}
	}

	// --- the little message after saving ------------------------------------
	Rectangle {
		id: toast
		anchors.horizontalCenter: parent.horizontalCenter
		anchors.bottom: parent.bottom
		anchors.bottomMargin: 64
		width: Math.min(parent.width - 48, toastText.implicitWidth + 40)
		height: toastText.implicitHeight + 26
		radius: height / 2
		color: "#e6202124"
		opacity: 0
		visible: opacity > 0

		Text {
			id: toastText
			anchors.centerIn: parent
			width: parent.width - 40
			text: app.notice
			color: Glaze.ink
			font.pixelSize: 14
			horizontalAlignment: Text.AlignHCenter
			elide: Text.ElideMiddle
		}
	}

	// --- leaving ------------------------------------------------------------
	SequentialAnimation {
		id: outro
		ParallelAnimation {
			NumberAnimation {
				target: root; property: "scrimAmount"; to: 0
				duration: Glaze.quick
			}
			NumberAnimation {
				target: sheet; property: "opacity"; to: 0
				duration: Glaze.quick
			}
			// Shrinks a little as it goes, the reverse of how it arrived.
			NumberAnimation {
				target: sheet; property: "scale"; to: 0.94
				duration: Glaze.quick; easing.type: Easing.InQuad
			}
		}
		ScriptAction {
			script: {
				root.cropping = false
				// Nothing to say: go straight out. Holding an empty window open
				// for the length of a toast would be a visible pause.
				if (app.notice.length === 0)
					root.visible = false
			}
		}
		// The toast outlives the sheet, so the "Saved to Screenshots" notice is
		// readable after the picture it refers to has gone.
		NumberAnimation {
			target: toast; property: "opacity"; to: 1
			duration: 140
		}
		PauseAnimation { duration: 1500 }
		NumberAnimation {
			target: toast; property: "opacity"; to: 0
			duration: 220
		}
		ScriptAction { script: root.visible = false }
	}

	// Escape does what Close does. There is no keyboard on this phone, but
	// there is over ssh, and that is where this gets debugged.
	Item {
		focus: true
		Keys.onEscapePressed: root.cropping ? root.cropping = false : app.discard()
	}
}

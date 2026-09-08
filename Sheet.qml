// SPDX-License-Identifier: LGPL-2.0-or-later
//
// The picture, and the four things you can do with it.
//
// THE ANIMATION IS THE POINT. What a phone does, and what this copies, is show
// the screenshot at exactly the size and place it was taken -- filling the
// screen, because that is what was photographed -- and then shrink it into a
// card. That single movement is what says "the thing you were looking at is now
// an object you are holding", and it is why the sheet does not need a title.
//
// It is driven by ONE number, `progress`, going 0 -> 1. Everything else is a
// binding on it: size, position, the border appearing, the corners rounding.
// Animating four properties separately is how these end up subtly out of step.
import QtQuick

Item {
	id: root

	// 0 = filling the screen, as taken. 1 = settled into the card.
	property real progress: 0
	// The button row comes in slightly late, so the eye follows the picture
	// first and finds the buttons already there when it looks down.
	property real rowIn: 0

	signal cropRequested()
	signal shareRequested()

	// The screenshot has the screen's own shape, so this is very close to
	// width/height -- but not exactly, and using the real thing is what keeps
	// the picture from being a pixel off when it lands.
	readonly property real aspect: app.imageHeight > 0
		? app.imageWidth / app.imageHeight : 1

	readonly property real pad: 18
	readonly property real areaW: Math.max(1, width - pad * 2)
	readonly property real areaH: Math.max(1, row.y - pad - pad)

	// Fit the picture into what is left above the buttons.
	readonly property real fitW: Math.min(areaW, areaH * aspect)
	readonly property real fitH: aspect > 0 ? fitW / aspect : areaH
	readonly property real fitX: pad + (areaW - fitW) / 2
	readonly property real fitY: pad + (areaH - fitH) / 2

	// How much bigger the card has to be to cover the screen. This is the
	// scale it STARTS at.
	readonly property real startScale:
		Math.max(width / Math.max(fitW, 1), height / Math.max(fitH, 1))

	// The dark edge only exists once it has landed. At progress 0 it is zero,
	// so what covers the screen is the bare screenshot -- a border there would
	// scale up with everything else and read as a black frame round the shot.
	readonly property real edge: 5 * progress

	// Where the picture really is on screen, for the cropper to line up with.
	readonly property rect pictureRect: Qt.rect(fitX, fitY, fitW, fitH)

	function enter() {
		progress = 0
		rowIn = 0
		entry.restart()
	}

	// Already in place -- used after a crop, where the picture changed but the
	// card did not arrive from anywhere.
	function settle() {
		entry.stop()
		progress = 1
		rowIn = 1
	}

	ParallelAnimation {
		id: entry
		NumberAnimation {
			target: root; property: "progress"; from: 0; to: 1
			duration: Glaze.settle
			// OutCubic and not an elastic bounce: this is a photograph coming
			// to rest, not a toy. A bounce here makes it look cheap.
			easing.type: Easing.OutCubic
		}
		SequentialAnimation {
			PauseAnimation { duration: Math.round(Glaze.settle * 0.45) }
			NumberAnimation {
				target: root; property: "rowIn"; from: 0; to: 1
				duration: Glaze.quick; easing.type: Easing.OutCubic
			}
		}
	}

	Rectangle {
		id: card
		x: root.fitX - root.edge
		width: root.fitW + root.edge * 2
		height: root.fitH + root.edge * 2
		// Slide from the middle of the screen down to where it belongs. At
		// progress 0 the card's centre sits on the screen's centre, which is
		// what makes the scaled-up version line up with what was photographed.
		y: root.fitY - root.edge
			+ (root.height / 2 - (root.fitY + root.fitH / 2)) * (1 - root.progress)

		transformOrigin: Item.Center
		scale: 1 + (root.startScale - 1) * (1 - root.progress)

		radius: Glaze.radiusSmall * root.progress
		color: "#0a0a0c"

		Image {
			anchors.fill: parent
			anchors.margins: root.edge
			source: app.image
			fillMode: Image.PreserveAspectFit
			// The file is new every time, so caching it would only cost memory.
			cache: false
			// LOADED IN THE BACKGROUND, and that is a fix rather than a detail.
			// Decoding a 1080x2400 PNG takes a couple of hundred milliseconds,
			// and done on the GUI thread it happened BEFORE the window could be
			// shown -- so it landed squarely in the gap the user waits through.
			// Off-thread, the sheet appears at once and the picture arrives a
			// frame or two later, underneath a flash that is still white.
			asynchronous: true
			// Never decode bigger than it will be drawn. At full size this is a
			// 10 MB texture to show something a third of that.
			sourceSize.width: Math.round(root.fitW * 2)
			sourceSize.height: Math.round(root.fitH * 2)
			smooth: true
		}
	}

	Row {
		id: row
		anchors.horizontalCenter: parent.horizontalCenter
		anchors.bottom: parent.bottom
		// Slides up as it fades in. 34 px is about a thumb's worth of travel:
		// enough to read as movement, not so much that it looks thrown.
		anchors.bottomMargin: 24 - 34 * (1 - root.rowIn)
		opacity: root.rowIn
		spacing: Math.max(10,
			(root.width - root.pad * 2 - 4 * Glaze.touch) / 5)

		Button {
			shape: Glaze.shapeShare
			text: qsTr("Share")
			onClicked: root.shareRequested()
		}
		Button {
			shape: Glaze.shapeSave
			text: qsTr("Save")
			onClicked: app.save()
		}
		Button {
			shape: Glaze.shapeCrop
			text: qsTr("Crop")
			onClicked: root.cropRequested()
		}
		Button {
			shape: Glaze.shapeClose
			text: qsTr("Close")
			// Red, because this one throws the screenshot away. It is the only
			// button on the sheet that loses anything.
			tint: Glaze.danger
			onClicked: app.discard()
		}
	}
}

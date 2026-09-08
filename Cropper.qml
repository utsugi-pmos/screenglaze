// SPDX-License-Identifier: LGPL-2.0-or-later
//
// Drag the corners, keep what is inside.
//
// It draws the picture at EXACTLY the rectangle the sheet was using, so opening
// and closing the cropper does not move the image a single pixel. That is the
// difference between "a tool opened over the photo" and "the photo jumped".
//
// The selection is kept as four edges in screen coordinates rather than as an
// x/y/w/h rectangle. With a rectangle, dragging the left handle means changing
// x and width together, and every clamp has to be written twice.
import QtQuick

Item {
	id: root

	// Where the picture actually is, in this item's coordinates.
	property rect picture: Qt.rect(0, 0, width, height)

	// Normalised to the picture: 0..1, which is what the backend crops with.
	signal accepted(real x, real y, real w, real h)
	signal cancelled()

	property real selL: 0
	property real selT: 0
	property real selR: 0
	property real selB: 0

	// Below this a crop is a mis-tap rather than an intention. It is also
	// roughly the smallest thing two fingers can position.
	readonly property real minimum: 48

	function reset() {
		// Start just inside the picture rather than on its edge, so that all
		// four handles are visible and obviously grabbable.
		const inset = Math.min(picture.width, picture.height) * 0.08
		selL = picture.x + inset
		selT = picture.y + inset
		selR = picture.x + picture.width - inset
		selB = picture.y + picture.height - inset
	}

	function clamp(v, lo, hi) {
		return Math.max(lo, Math.min(hi, v))
	}

	Image {
		// Positioned by hand rather than anchored, to match the sheet exactly.
		x: root.picture.x
		y: root.picture.y
		width: root.picture.width
		height: root.picture.height
		source: app.image
		fillMode: Image.PreserveAspectFit
		cache: false
		smooth: true
	}

	// --- everything outside the selection goes dark -------------------------
	// Four rectangles rather than one with a hole: Qt Quick has no hole, and
	// four bindings are cheaper than a mask texture the size of the screen.
	Repeater {
		model: 4
		Rectangle {
			color: "#000000"
			opacity: 0.62
			x: index === 0 || index === 1 ? 0 : (index === 2 ? 0 : root.selR)
			y: index === 0 ? 0 : (index === 1 ? root.selB : root.selT)
			width: index <= 1 ? root.width
				: (index === 2 ? root.selL : root.width - root.selR)
			height: index === 0 ? root.selT
				: (index === 1 ? root.height - root.selB : root.selB - root.selT)
		}
	}

	// --- the selection ------------------------------------------------------
	Item {
		x: root.selL
		y: root.selT
		width: root.selR - root.selL
		height: root.selB - root.selT

		Rectangle {
			anchors.fill: parent
			color: "transparent"
			border.color: Glaze.ink
			border.width: 2
		}

		// Rule of thirds. Two lines each way, the same guides a camera draws --
		// they are what make the crop feel like a photographic decision.
		Repeater {
			model: 2
			Rectangle {
				color: Glaze.ink
				opacity: 0.32
				width: parent.width
				height: 1
				y: parent.height * (index + 1) / 3
			}
		}
		Repeater {
			model: 2
			Rectangle {
				color: Glaze.ink
				opacity: 0.32
				height: parent.height
				width: 1
				x: parent.width * (index + 1) / 3
			}
		}

		// Drag the middle to move the whole selection.
		MouseArea {
			anchors.fill: parent
			preventStealing: true
			property real lastX: 0
			property real lastY: 0
			onPressed: (m) => {
				const p = mapToItem(root, m.x, m.y)
				lastX = p.x
				lastY = p.y
			}
			onPositionChanged: (m) => {
				const p = mapToItem(root, m.x, m.y)
				let dx = p.x - lastX
				let dy = p.y - lastY
				// Clamp the MOVEMENT, not the edges. Clamping each edge on its
				// own lets the box change shape when it hits a wall.
				dx = root.clamp(dx, root.picture.x - root.selL,
					root.picture.x + root.picture.width - root.selR)
				dy = root.clamp(dy, root.picture.y - root.selT,
					root.picture.y + root.picture.height - root.selB)
				root.selL += dx
				root.selR += dx
				root.selT += dy
				root.selB += dy
				lastX = p.x
				lastY = p.y
			}
		}
	}

	// --- the four corner handles -------------------------------------------
	Repeater {
		model: 4
		Item {
			// 0 top-left, 1 top-right, 2 bottom-right, 3 bottom-left.
			//
			// NOT called 'right' and 'bottom'. QQuickItem already has those --
			// they are the anchor lines -- and they are FINAL, so declaring
			// them here does not shadow them, it makes the whole component fail
			// to load with "Cannot override FINAL property". The error names
			// the line, not the reason.
			readonly property bool atRight: index === 1 || index === 2
			readonly property bool atBottom: index === 2 || index === 3

			width: 52
			height: 52
			x: (atRight ? root.selR : root.selL) - width / 2
			y: (atBottom ? root.selB : root.selT) - height / 2

			Rectangle {
				anchors.centerIn: parent
				width: 20
				height: 20
				radius: 10
				color: Glaze.ink
				border.color: "#40000000"
				border.width: 1
				scale: grab.pressed ? 1.35 : 1
				Behavior on scale { NumberAnimation { duration: 90 } }
			}

			MouseArea {
				id: grab
				anchors.fill: parent
				preventStealing: true
				property real offX: 0
				property real offY: 0
				onPressed: (m) => {
					offX = m.x - width / 2
					offY = m.y - height / 2
				}
				onPositionChanged: (m) => {
					const p = mapToItem(root, m.x - offX, m.y - offY)
					if (atRight)
						root.selR = root.clamp(p.x, root.selL + root.minimum,
							root.picture.x + root.picture.width)
					else
						root.selL = root.clamp(p.x, root.picture.x,
							root.selR - root.minimum)
					if (atBottom)
						root.selB = root.clamp(p.y, root.selT + root.minimum,
							root.picture.y + root.picture.height)
					else
						root.selT = root.clamp(p.y, root.picture.y,
							root.selB - root.minimum)
				}
			}
		}
	}

	// --- confirm or back out ------------------------------------------------
	Row {
		anchors.horizontalCenter: parent.horizontalCenter
		anchors.bottom: parent.bottom
		anchors.bottomMargin: 24
		spacing: 48

		Button {
			shape: Glaze.shapeClose
			text: qsTr("Cancelar")
			onClicked: root.cancelled()
		}
		Button {
			shape: Glaze.shapeCheck
			text: qsTr("Recortar")
			highlighted: true
			onClicked: {
				if (root.picture.width <= 0 || root.picture.height <= 0)
					return
				root.accepted((root.selL - root.picture.x) / root.picture.width,
					(root.selT - root.picture.y) / root.picture.height,
					(root.selR - root.selL) / root.picture.width,
					(root.selB - root.selT) / root.picture.height)
			}
		}
	}
}

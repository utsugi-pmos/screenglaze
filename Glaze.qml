// SPDX-License-Identifier: LGPL-2.0-or-later
//
// Every colour, corner and duration in Screenglaze, in one file.
//
// It does NOT follow the desktop theme, and that is on purpose. This is an
// overlay that appears over whatever you were looking at -- a photo, a map, a
// white web page -- and it has to read the same over all of them. Phones do
// the same: the screenshot sheet is always dark, whatever the app under it.
//
// The durations are the part worth being careful with. They are what makes the
// thing feel like a phone rather than a dialog box, and they are all tuned
// against one rule: nothing waits for anything else. See main.qml.
pragma Singleton
import QtQuick

QtObject {
	// --- surfaces -----------------------------------------------------------
	// Near-black rather than grey: this panel floats over the screenshot, and
	// on an OLED the darker it is the more the picture looks like it is lit
	// from within.
	readonly property color sheet: "#1b1b1f"
	readonly property color scrim: "#000000"
	// 0.8, not less. At 0.72 a bright wallpaper still read through clearly
	// enough to compete with the picture -- measured against the default Plasma
	// one, which is about as bright as they come.
	readonly property real scrimOpacity: 0.8

	readonly property color ink: "#ffffff"
	readonly property color inkSoft: "#b9bcc4"

	// The button discs are OPAQUE, not a white wash over the scrim. Translucent
	// ones let whatever is underneath show through the middle of the circle --
	// with the dock sitting right there, that meant app icons appearing inside
	// the buttons. A control has to look like a solid object to read as one.
	readonly property color disc: "#2c2d33"
	readonly property color discDown: "#41434c"

	// The one accent. Only the crop confirmation uses it, because it is the
	// only button whose meaning is "commit" rather than "choose".
	readonly property color accent: "#4285f4"
	readonly property color danger: "#ea4335"

	// --- shape --------------------------------------------------------------
	readonly property int radius: 22
	readonly property int radiusSmall: 14
	readonly property int touch: 60          // smallest thing worth aiming at

	// --- timings ------------------------------------------------------------
	// The flash is fast and asymmetric: in almost instantly, out slower. That
	// is what a real shutter looks like, and a symmetric fade reads as a
	// screen glitch instead.
	readonly property int flashIn: 60
	readonly property int flashOut: 260
	// The shrink. Long enough to be followed by the eye, short enough that it
	// is over before you could get annoyed by it.
	readonly property int settle: 340
	readonly property int quick: 180

	// --- icons --------------------------------------------------------------
	// Drawn here as SVG rather than pulled from the icon theme. A themed icon
	// needs XDG_DATA_DIRS, a theme that actually contains the name, and a
	// fallback when it does not -- all three of which PocoNav had to fight, and
	// the failure mode is a button that renders as an empty circle. These
	// cannot go missing.
	function icon(shape, color) {
		return "data:image/svg+xml;utf8," + encodeURIComponent(
			'<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" '
			+ 'viewBox="0 0 24 24" fill="none" stroke="' + (color || ink) + '" '
			+ 'stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round">'
			+ shape + '</svg>')
	}

	readonly property string shapeShare:
		'<circle cx="18" cy="5" r="3"/><circle cx="6" cy="12" r="3"/>'
		+ '<circle cx="18" cy="19" r="3"/><line x1="8.6" y1="13.5" x2="15.4" y2="17.5"/>'
		+ '<line x1="15.4" y1="6.5" x2="8.6" y2="10.5"/>'
	readonly property string shapeSave:
		'<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>'
		+ '<polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/>'
	readonly property string shapeCrop:
		'<path d="M6.13 1L6 16a2 2 0 0 0 2 2h15"/>'
		+ '<path d="M1 6.13L16 6a2 2 0 0 1 2 2v15"/>'
	readonly property string shapeClose:
		'<line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/>'
	readonly property string shapeCheck:
		'<polyline points="20 6 9 17 4 12"/>'
}

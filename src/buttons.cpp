// SPDX-License-Identifier: LGPL-2.0-or-later
#include "buttons.h"

#include <QDebug>
#include <QDir>
#include <QSocketNotifier>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

// evdev reports capabilities as a bitmap of longs; there is no helper for it.
constexpr int bitsPerLong = 8 * sizeof(long);
inline bool testBit(const unsigned long *map, int bit)
{
	return (map[bit / bitsPerLong] >> (bit % bitsPerLong)) & 1UL;
}

// Android's SCREENSHOT_CHORD_KEY_TIMEOUT. Long enough that two fingers landing
// "at the same time" always fall inside it, short enough that a press replayed
// at the end of it does not feel late.
constexpr int chordWindowMs = 150;

const char *virtualName = "Screenglaze passthrough";

} // namespace

Buttons::Buttons(Mode mode, QObject *parent)
	: QObject(parent)
	, m_mode(mode)
{
	m_volume.code = KEY_VOLUMEDOWN;

	discover();

	if (m_mode == Mode::Inspect) {
		// Just ask whether it COULD be done: open /dev/uinput and let go
		// without creating a device or grabbing anything.
		const int probe = ::open("/dev/uinput", O_WRONLY | O_CLOEXEC);
		m_replay = probe >= 0;
		if (probe >= 0)
			::close(probe);
		return;
	}

	m_volume.window.setSingleShot(true);
	m_volume.window.setInterval(chordWindowMs);
	connect(&m_volume.window, &QTimer::timeout, this, &Buttons::expire);

	// AFTER discover(), always. The virtual device advertises volume-down, so a
	// scan running later would find it, open it, and read back its own replayed
	// events forever.
	m_replay = openUinput();
	if (m_replay)
		grabVolume(true);
	else
		qWarning("screenglaze: without /dev/uinput the volume key cannot be"
			" replayed, so it is not taken: the volume panel will show up in the"
			" screenshot.");
}

Buttons::~Buttons()
{
	// Anything still held back is replayed on the way out rather than dropped,
	// so shutting down mid-press does not eat a keystroke.
	if (m_volume.pending)
		replay(m_volume.code, 1);
	if (m_volume.pending || m_volume.forwarded)
		replay(m_volume.code, 0);

	// Releasing explicitly is belt and braces: the kernel drops every grab when
	// the file descriptor closes. That property is what means a crash, a
	// kill -9 or a stopped service can never leave the phone with a dead power
	// button.
	grabPower(false);
	grabVolume(false);

	for (const Device &d : std::as_const(m_devices)) {
		delete d.notifier;
		::close(d.fd);
	}
	if (m_uinput >= 0) {
		::ioctl(m_uinput, UI_DEV_DESTROY);
		::close(m_uinput);
	}
}

void Buttons::discover()
{
	const QDir dir(QStringLiteral("/dev/input"));
	QStringList names = dir.entryList({QStringLiteral("event*")}, QDir::System);
	// event2, event10 -- plain sorting puts event10 before event2. Harmless
	// here, but it makes the startup log confusing to read.
	std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
		return a.mid(5).toInt() < b.mid(5).toInt();
	});

	for (const QString &entry : std::as_const(names)) {
		const QString path = dir.filePath(entry);
		const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue; // not ours to read: that is the udev rule doing its job

		unsigned long types[EV_MAX / bitsPerLong + 1] = {};
		unsigned long keys[KEY_MAX / bitsPerLong + 1] = {};
		if (::ioctl(fd, EVIOCGBIT(0, sizeof(types)), types) < 0
			|| ::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0) {
			::close(fd);
			continue;
		}

		// Skip anything with absolute axes. On this phone that is only the
		// touchscreen, and grabbing a touchscreen would be catastrophic.
		if (testBit(types, EV_ABS)) {
			::close(fd);
			continue;
		}

		char name[256] = {};
		if (::ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0)
			qstrncpy(name, "?", sizeof(name));

		// Never take our own virtual device, however we got here.
		if (qstrcmp(name, virtualName) == 0) {
			::close(fd);
			continue;
		}

		Device d;
		d.hasPower = testBit(keys, KEY_POWER);
		d.hasVolumeDown = testBit(keys, KEY_VOLUMEDOWN);
		if (!d.hasPower && !d.hasVolumeDown) {
			::close(fd);
			continue;
		}

		d.fd = fd;
		d.name = QString::fromLocal8Bit(name);
		d.notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
		connect(d.notifier, &QSocketNotifier::activated, this, [this, fd] { read(fd); });
		m_devices.append(d);

		QStringList what;
		if (d.hasPower)
			what << QStringLiteral("power");
		if (d.hasVolumeDown)
			what << QStringLiteral("volume-down");
		m_watching << QStringLiteral("%1 (%2): %3").arg(entry, d.name, what.join(QStringLiteral(", ")));
	}
}

bool Buttons::openUinput()
{
	m_uinput = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (m_uinput < 0) {
		qWarning("screenglaze: cannot open /dev/uinput: %s", strerror(errno));
		return false;
	}

	uinput_setup setup = {};
	setup.id.bustype = BUS_VIRTUAL;
	setup.id.vendor = 0x1d6b;
	setup.id.product = 0x0001;
	setup.id.version = 1;
	qstrncpy(setup.name, virtualName, sizeof(setup.name));

	// Volume-down and NOTHING else -- in particular, not KEY_POWER.
	//
	// An earlier version declared the power key here so it could replay it.
	// Plasma ignores a power key from a virtual device (see the header), so all
	// that achieved was a phone that could not be locked. Declaring a key we
	// cannot make anyone act on is worse than not declaring it: it makes the
	// device look like a power switch to udev while doing nothing.
	if (::ioctl(m_uinput, UI_SET_EVBIT, EV_KEY) < 0
		|| ::ioctl(m_uinput, UI_SET_KEYBIT, KEY_VOLUMEDOWN) < 0
		|| ::ioctl(m_uinput, UI_DEV_SETUP, &setup) < 0
		|| ::ioctl(m_uinput, UI_DEV_CREATE) < 0) {
		qWarning("screenglaze: could not create the virtual device: %s", strerror(errno));
		::close(m_uinput);
		m_uinput = -1;
		return false;
	}
	return true;
}

void Buttons::grabVolume(bool on)
{
	if (on == m_holdVolume)
		return;
	bool any = false;
	for (const Device &d : std::as_const(m_devices)) {
		// Volume-down and NOT power. On a phone where both sit on one
		// gpio-keys node this grabs nothing, which is the right answer: holding
		// that node permanently would swallow the power button forever.
		if (!d.hasVolumeDown || d.hasPower)
			continue;
		if (::ioctl(d.fd, EVIOCGRAB, on ? 1 : 0) == 0)
			any = true;
		else if (on)
			qWarning("screenglaze: could not grab %s: %s",
				qPrintable(d.name), strerror(errno));
	}
	m_holdVolume = on && any;
}

// Is the power key pressed RIGHT NOW?
//
// EVIOCGKEY returns the current state of the device's keys, not events. It is the
// only way to know whether we arrived late: if we only looked at the events, a
// key that was already down before grabbing the device would be invisible until
// its autorepeat.
bool Buttons::powerIsDown() const
{
	unsigned long keys[KEY_MAX / bitsPerLong + 1] = {};
	for (const Device &d : std::as_const(m_devices)) {
		if (!d.hasPower)
			continue;
		if (::ioctl(d.fd, EVIOCGKEY(sizeof(keys)), keys) < 0)
			continue;
		if (testBit(keys, KEY_POWER))
			return true;
	}
	return false;
}

void Buttons::grabPower(bool on)
{
	if (on == m_holdPower)
		return;
	bool any = false;
	for (const Device &d : std::as_const(m_devices)) {
		if (!d.hasPower)
			continue;
		if (::ioctl(d.fd, EVIOCGRAB, on ? 1 : 0) == 0)
			any = true;
		else if (on)
			qWarning("screenglaze: could not grab %s: %s",
				qPrintable(d.name), strerror(errno));
	}
	// Only remember it if something actually took it, or the release would be
	// skipped and the power button would stay ours.
	m_holdPower = on && any;
}

void Buttons::replay(int code, int value)
{
	if (m_uinput < 0)
		return;
	input_event ev[2] = {};
	ev[0].type = EV_KEY;
	ev[0].code = code;
	ev[0].value = value;
	ev[1].type = EV_SYN;
	ev[1].code = SYN_REPORT;
	if (::write(m_uinput, ev, sizeof(ev)) < 0)
		qWarning("screenglaze: could not replay key %d: %s", code, strerror(errno));
}

void Buttons::read(int fd)
{
	input_event events[32];
	for (;;) {
		const ssize_t n = ::read(fd, events, sizeof(events));
		if (n <= 0)
			return;

		for (int i = 0; i < int(n / sizeof(input_event)); ++i) {
			const input_event &e = events[i];
			if (e.type != EV_KEY)
				continue;
			if (e.code == KEY_VOLUMEDOWN)
				onVolume(e.value);
			else if (e.code == KEY_POWER)
				onPower(e.value);
		}
	}
}

void Buttons::onVolume(int value)
{
	const bool wasDown = m_volume.down;
	m_volume.down = value != 0;

	// Take the power button the instant volume-down goes down, so that it is
	// already ours before the user can reach it. This is the ONLY window in
	// which power is held, and it is why the chord wants volume first.
	if (m_volume.down && !wasDown) {
		grabPower(true);

		// DID WE ARRIVE LATE? That is: when we took the key, was it already pressed?
		//
		// It happens when both are pressed at once and the kernel delivers the
		// power one first. Then its PRESS has already reached Plasma, and by
		// grabbing the device we steal its RELEASE -- Plasma is left thinking you
		// are still holding it down, its long-press timer expires and it brings up
		// the power-off panel. Measured in the log: "Power key pressed short" 0.85 s
		// before each capture.
		//
		// A press that Plasma already has cannot be taken away from it: it does not
		// attend to reinjected power keys. The only thing that can be done is to
		// hand the device back AS SOON AS POSSIBLE so the real release reaches it,
		// and what was a hung long press becomes a short one. The lit screen is
		// lost, but the power-off panel does not appear.
		if (m_holdPower && powerIsDown()) {
			m_powerDown = true;
			fire();                 // the capture happens all the same
			grabPower(false);       // and the real release goes back to Plasma
			m_powerDown = false;
		}
	}

	if (m_replay) {
		if (value == 1) {
			m_volume.pending = true;
			m_volume.forwarded = false;
			m_volume.window.start();
		} else if (value == 2) {
			// Autorepeat: only repeat what the system has seen a press for.
			if (m_volume.forwarded)
				replay(m_volume.code, 2);
		} else {
			m_volume.window.stop();
			if (m_volume.pending) {
				// Tapped and released inside the window with no power key: an
				// ordinary short volume press. Replay both halves now.
				replay(m_volume.code, 1);
				replay(m_volume.code, 0);
				m_volume.pending = false;
			} else if (m_volume.forwarded) {
				replay(m_volume.code, 0);
				m_volume.forwarded = false;
			}
		}
	}

	if (m_volume.down && m_powerDown)
		fire();
	// Hand the power button back only when BOTH are up. Releasing it while
	// power is still held would leave the system having missed the press but
	// seeing the release.
	if (!m_volume.down && !m_powerDown) {
		m_fired = false;
		grabPower(false);
	}
}

void Buttons::onPower(int value)
{
	// Reaching here at all means the key was grabbed, which only happens while
	// volume-down is held. A power press on its own never comes through here:
	// it goes straight to Plasma and locks the screen, as it always has.
	m_powerDown = value != 0;

	if (m_powerDown && m_volume.down)
		fire();

	if (!m_volume.down && !m_powerDown) {
		m_fired = false;
		grabPower(false);
	}
}

void Buttons::fire()
{
	if (m_fired)
		return;
	m_fired = true;

	// Cancel the volume press outright if it has not gone out yet: that is what
	// keeps the volume still and the slider out of the picture. If it HAS
	// already been replayed it is public knowledge and still owes its release,
	// so it is left alone.
	if (m_volume.pending) {
		m_volume.window.stop();
		m_volume.pending = false;
	}
	Q_EMIT combo();
}

void Buttons::expire()
{
	// 150 ms and no power key: it was a real volume press after all.
	if (!m_volume.pending)
		return;
	m_volume.pending = false;
	m_volume.forwarded = true;
	replay(m_volume.code, 1);
}

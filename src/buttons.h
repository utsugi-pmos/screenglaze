// SPDX-License-Identifier: LGPL-2.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>

class QSocketNotifier;

// Watches the two physical buttons and reports the Android chord:
// volume-down and power pressed together, IN EITHER ORDER.
//
// It reads evdev directly instead of asking the compositor. KWin can only bind
// a shortcut to a key plus MODIFIERS; volume-down and power are both ordinary
// keys, so "these two at once" is not something a global shortcut can express.
//
// HOW IT WORKS, and why the two keys are treated differently
// ----------------------------------------------------------
// VOLUME-DOWN is held permanently and every press is buffered for 150 ms:
//
//   power arrives inside the window  ->  it was a screenshot. The press is
//       cancelled outright, so the volume does not move and Plasma's volume
//       slider never appears to be photographed.
//   the window expires               ->  a real volume press. It is replayed
//       through a uinput device, 150 ms late, and behaves as always.
//
// POWER is only held WHILE VOLUME-DOWN IS DOWN, and it is never replayed.
//
// That asymmetry is not tidiness, it is a measured limit. A version that held
// the power key permanently and replayed it broke locking the screen and the
// press-and-hold power menu, because PLASMA DOES NOT ACT ON A POWER KEY COMING
// FROM A VIRTUAL DEVICE. PowerDevil holds a `block` inhibitor on
// handle-power-key, so logind only writes "Power key pressed short" in the
// journal and does nothing -- and PowerDevil itself ignored the replayed key.
// Tested directly, with the backlight as witness: a synthetic power press of
// 0 ms and one of 120 ms both left the screen exactly as it was.
//
// (That is also the trap in the obvious verification: logind LOGGING the key
// proves the event arrived, not that anything acted on it. It logs precisely
// because it has been told not to act.)
//
// The cost of the asymmetry is that the chord has an ORDER: volume-down first,
// then power. Pressing power first locks the screen instead, exactly as it
// always did. A phone that cannot be locked or powered off is a worse bug than
// a chord that wants one particular order, so this is the way round it stays
// until there is a way to drive Plasma's power actions directly.
//
// The volume half depends on /dev/uinput. Without it volume-down is not grabbed
// either, and the slider comes back into the pictures: eating the user's volume
// key would be no better than eating the power one.
class Buttons : public QObject
{
	Q_OBJECT

public:
	enum class Mode {
		// Take the keys and act on them. What the service does.
		Watch,
		// Look, but touch nothing. --comprobar runs while the real service is
		// already up, and a diagnostic that grabbed the keys would STEAL them
		// from the running copy and hand them back on exit, leaving the service
		// silently unable to hold what it thinks it holds. A check that breaks
		// the thing it is checking is worse than no check.
		Inspect,
	};

	// An enum class and not a bool, deliberately. As `Buttons(bool, QObject*)`
	// this took `new Buttons(this)` -- the existing, correct-looking call --
	// and silently read the pointer as `passive = true`, because any pointer
	// converts to bool. It compiled, ran, logged the devices it had found, and
	// grabbed nothing at all. An enum class does not convert from a pointer.
	explicit Buttons(Mode mode = Mode::Watch, QObject *parent = nullptr);
	~Buttons() override;

	QStringList watching() const { return m_watching; }
	bool ready() const { return !m_devices.isEmpty(); }
	// Whether presses can be held back and replayed, i.e. whether the chord
	// works in both orders and keeps the volume slider out of the picture.
	bool canReplay() const { return m_replay; }

Q_SIGNALS:
	void combo();

private:
	struct Device {
		int fd = -1;
		QSocketNotifier *notifier = nullptr;
		bool hasPower = false;
		bool hasVolumeDown = false;
		QString name;
	};

	// One of the two keys, and what we currently owe the rest of the system
	// on its behalf.
	struct Key {
		int code = 0;
		bool down = false;       // physically held right now
		bool pending = false;    // press buffered, not replayed, not cancelled
		// Press WAS replayed, so its release has to be replayed too. Getting
		// this wrong leaves the compositor believing a key is held forever --
		// and for the power key that would mean a phone that thinks you are
		// holding the power button down.
		bool forwarded = false;
		QTimer window;
	};

	void discover();
	void read(int fd);
	bool openUinput();
	// Volume devices, held for as long as the service runs.
	void grabVolume(bool on);
	// Power devices, held only for the length of a chord.
	void grabPower(bool on);
	void replay(int code, int value);
	bool powerIsDown() const;

	void onVolume(int value);
	void onPower(int value);
	void fire();
	void expire();

	QList<Device> m_devices;
	QStringList m_watching;

	Mode m_mode = Mode::Watch;
	int m_uinput = -1;
	bool m_replay = false;
	bool m_holdVolume = false;
	bool m_holdPower = false;
	// One screenshot per chord, cleared when both keys come up. Without it the
	// power key's autorepeat fires a burst while the user is still letting go.
	bool m_fired = false;

	Key m_volume;
	// Power carries no buffer: it is never held back and never replayed, so
	// only its up/down state matters.
	bool m_powerDown = false;
};

// SPDX-License-Identifier: LGPL-2.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

// Takes the picture, and hands back a file that belongs to us.
//
// WHY SPECTACLE AND NOT KWIN DIRECTLY
// -----------------------------------
// KWin exposes org.kde.KWin.ScreenShot2, which would give raw pixels down a
// pipe in a few milliseconds. It refuses us: it compares the caller's
// /proc/<pid>/exe against the real /usr/bin/spectacle and answers
// NoAuthorized to everyone else. Measured, not assumed -- renaming the caller
// makes no difference, because it is the path that is checked.
//
// The xdg-desktop-portal route works but asks the user to confirm every shot
// on screen, which is not a screenshot key.
//
// So Spectacle takes it, and we take it off Spectacle's hands. Cold that costs
// about 1.3 s, almost all of it Qt starting up; with the process already alive
// it is about 0.55 s. That is why warmUp() exists and why it is called again
// after every shot.
class Capture : public QObject
{
	Q_OBJECT

public:
	explicit Capture(QObject *parent = nullptr);

	// Start Spectacle now so the next combo does not pay for it. Safe to call
	// when it is already running.
	void warmUp();
	void request();

	QString stagingDir() const { return m_staging; }

Q_SIGNALS:
	// Absolute path of a PNG that is ours to move, crop or delete.
	void ready(const QString &file);
	void failed(const QString &reason);

private Q_SLOTS:
	void onTaken(const QString &where);
	void onFailed(const QString &reason);
	void onNameOwnerChanged(const QString &name, const QString &was, const QString &now);

private:
	void give(const QString &reason);

	QString m_staging;
	bool m_waiting = false;
	bool m_keepWarm = false;
	QTimer m_deadline;
};

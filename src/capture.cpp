// SPDX-License-Identifier: LGPL-2.0-or-later
#include "capture.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QUrl>

namespace {
const auto service = QStringLiteral("org.kde.Spectacle");
const auto path = QStringLiteral("/");
const auto iface = QStringLiteral("org.kde.Spectacle");
} // namespace

Capture::Capture(QObject *parent)
	: QObject(parent)
{
	// A subdirectory of the cache, not the cache itself. Qt puts its own shader
	// cache in CacheLocation, so the bare directory is shared with something
	// else -- fine today, because only one path is ever tracked, but it is the
	// kind of thing that makes a future "clean up leftovers" delete the wrong
	// thing.
	m_staging = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
		+ QStringLiteral("/pending");
	QDir().mkpath(m_staging);

	auto bus = QDBusConnection::sessionBus();
	bus.connect(service, path, iface, QStringLiteral("ScreenshotTaken"),
		this, SLOT(onTaken(QString)));
	bus.connect(service, path, iface, QStringLiteral("ScreenshotFailed"),
		this, SLOT(onFailed(QString)));

	// Keep Spectacle awake. SCREENGLAZE_CALIENTE=0 turns it off.
	//
	// This is a genuine trade, measured on this phone:
	//
	//   Spectacle asleep  ->  1.4-1.9 s per screenshot,   0 MB
	//   Spectacle awake   ->  0.55 s per screenshot,   ~84 MB PSS held
	//
	// It dies 2-3 seconds after each capture on its own, so without this every
	// single screenshot pays the cold start -- waking it right after a shot
	// does not help, because at that moment it is still registered and the
	// check says "already there".
	//
	// ON by default, and that was the other way round at first. The deciding
	// argument is not the memory, it is that the shutter click now plays with
	// the flash rather than on the key press: at 1.4 s the press gets no
	// feedback at all for a second and a half, which feels broken. Either the
	// wait is short or the click has to come early and out of step. 84 MB out
	// of this phone's 5.5 GB buys the short wait.
	// SCREENGLAZE_WARM was SCREENGLAZE_CALIENTE; both are honoured so a script
	// or a unit that already sets the old one keeps working.
	const char *warm = qEnvironmentVariableIsEmpty("SCREENGLAZE_WARM")
		? "SCREENGLAZE_CALIENTE" : "SCREENGLAZE_WARM";
	if (qEnvironmentVariableIsEmpty(warm)
		|| qEnvironmentVariableIntValue(warm) != 0) {
		m_keepWarm = true;
		bus.connect(QStringLiteral("org.freedesktop.DBus"),
			QStringLiteral("/org/freedesktop/DBus"),
			QStringLiteral("org.freedesktop.DBus"),
			QStringLiteral("NameOwnerChanged"),
			this, SLOT(onNameOwnerChanged(QString, QString, QString)));
	}

	// Spectacle answers with a signal, not a reply, so nothing would ever tell
	// us it had died mid-shot. Without this the sheet simply never appears and
	// the next combo is ignored forever, because m_waiting stays true.
	m_deadline.setSingleShot(true);
	m_deadline.setInterval(10000);
	connect(&m_deadline, &QTimer::timeout, this, [this] {
		give(QStringLiteral("Spectacle did not answer"));
	});
}

void Capture::warmUp()
{
	auto *bus = QDBusConnection::sessionBus().interface();
	if (!bus)
		return;
	if (bus->isServiceRegistered(service))
		return;

	// StartServiceByName by hand rather than QDBusConnectionInterface::
	// startService(), which BLOCKS until the service is up. That is the 1.3 s
	// Qt launch this whole function exists to get out of the way -- called from
	// the tail of a screenshot it would freeze the sheet the moment it appeared.
	QDBusMessage m = QDBusMessage::createMethodCall(
		QStringLiteral("org.freedesktop.DBus"),
		QStringLiteral("/org/freedesktop/DBus"),
		QStringLiteral("org.freedesktop.DBus"),
		QStringLiteral("StartServiceByName"));
	m << service << uint(0);
	QDBusConnection::sessionBus().asyncCall(m);
}

void Capture::request()
{
	if (m_waiting)
		return;
	m_waiting = true;
	m_deadline.start();

	// FullScreen is declared no-reply, so this is a send and not a call.
	// The integer is "include the mouse pointer"; on a phone there is none.
	QDBusMessage m = QDBusMessage::createMethodCall(service, path, iface,
		QStringLiteral("FullScreen"));
	m << int(0);
	QDBusConnection::sessionBus().send(m);
}

void Capture::onTaken(const QString &where)
{
	if (!m_waiting)
		return; // somebody else drove Spectacle; not our screenshot
	m_deadline.stop();
	m_waiting = false;

	// The signal carries a plain path on this build, but the same signal is
	// documented as a URL elsewhere. Accept both rather than break on an update.
	QString source = where;
	if (source.startsWith(QStringLiteral("file:")))
		source = QUrl(source).toLocalFile();

	if (!QFile::exists(source)) {
		Q_EMIT failed(QStringLiteral("Spectacle said %1 and there is nothing there").arg(source));
		return;
	}

	// Move it out of the gallery immediately.
	//
	// Spectacle saves where the user configured it to, which is the pictures
	// folder. If we left it there, "Save" would be a button that does
	// nothing and "Close" would have to delete a file the user never asked us
	// to put there. Staging it first makes both buttons honest.
	const QString target = QStringLiteral("%1/screenshot-%2.png").arg(m_staging,
		QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")));

	QFile::remove(target);
	if (!QFile::rename(source, target)) {
		// Different filesystem, or the gallery is on removable storage. Copying
		// is slower but never wrong; only give up if that fails too.
		if (!QFile::copy(source, target)) {
			Q_EMIT failed(QStringLiteral("could not move the capture to %1").arg(m_staging));
			return;
		}
		QFile::remove(source);
	}

	Q_EMIT ready(target);
}

void Capture::onNameOwnerChanged(const QString &name, const QString &was, const QString &now)
{
	if (!m_keepWarm || name != service)
		return;
	// Gone: bring it back. The small delay is not politeness, it is to avoid
	// racing its own shutdown -- starting it while it is still tearing itself
	// down gets the dying instance, which then exits and leaves nothing.
	if (!was.isEmpty() && now.isEmpty())
		QTimer::singleShot(500, this, &Capture::warmUp);
}

void Capture::onFailed(const QString &reason)
{
	if (!m_waiting)
		return;
	m_deadline.stop();
	m_waiting = false;
	give(reason);
}

void Capture::give(const QString &reason)
{
	m_waiting = false;
	Q_EMIT failed(reason);
}

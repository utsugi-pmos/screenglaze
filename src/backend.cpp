// SPDX-License-Identifier: LGPL-2.0-or-later
#include "backend.h"

#include "capture.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>

#include <fcntl.h>
#include <unistd.h>   // ::close, for the descriptor handed to the portal

namespace {

// Where a saved screenshot ends up. A folder of its own inside Pictures, the
// same shape Android uses, so the gallery groups them without any tagging.
QString galleryDir()
{
	QString base = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
	if (base.isEmpty())
		base = QDir::homePath();
	// The folder used to be called "Capturas". A phone that has been taking
	// screenshots for months has them all in there, and switching the name
	// would split the user's own pictures across two folders with nothing
	// saying why. So: keep using the old one where it already exists, and only
	// name new installations in English.
	const QString old = base + QStringLiteral("/Capturas");
	if (QDir(old).exists())
		return old;
	return base + QStringLiteral("/Screenshots");
}

} // namespace

Backend::Backend(QObject *parent)
	: QObject(parent)
	, m_capture(new Capture(this))
{
	// The buttons are not watched here any more. phone-keyconfig owns the
	// power and volume keys and calls shoot() over D-Bus when it sees the
	// chord; this object only has to be on the bus and ready to take the call.
	connect(m_capture, &Capture::ready, this, &Backend::onReady);
	connect(m_capture, &Capture::failed, this, &Backend::onFailed);

	m_idle.setSingleShot(true);
	m_idle.setInterval(45000);
	connect(&m_idle, &QTimer::timeout, this, [this] {
		if (!m_sheetUp)
			return;
		// Save rather than discard. Losing a screenshot because you looked away
		// is the one outcome nobody would have chosen.
		save();
	});

	watchScreenLock();

	// Pay Spectacle's start-up cost now, while nobody is waiting for it.
	m_capture->warmUp();
}

void Backend::onCombo()
{
	shoot();
}

void Backend::shoot()
{
	if (m_busy)
		return;

	// A locked screen photographs as a blank rectangle -- KWin blanks it on
	// purpose -- so the combo would quietly fill the gallery with black PNGs.
	if (m_locked)
		return;

	m_busy = true;
	// Kept permanently, not added for one debugging session. The delay between
	// pressing the buttons and seeing the flash is the thing most likely to be
	// complained about, and "it feels slow" is not something you can act on --
	// this says which of the two halves, Spectacle or us, actually took it.
	m_clock.start();

	// NOTHING SOUNDS HERE, and it used to.
	//
	// The shutter click fired on the key press, which is where a phone puts it
	// -- but a phone's picture is taken in a fifth of a second and this one
	// takes half. The result was a click, then a long nothing, then a flash:
	// two halves of one event, far enough apart to read as two separate things
	// going wrong. The click now plays with the flash, in onReady().
	//
	// There is nothing to put here instead. The only feedback that could not
	// end up inside the picture is sound or vibration, feedbackd's theme on
	// this device is sound-only for these events, and there is no
	// button-pressed.oga in the sound theme to play. So the press is silent and
	// the wait has to be short instead -- see Capture, which now keeps
	// Spectacle awake by default for exactly this reason.

	int settle = 0;
	if (m_sheetUp) {
		// A second combo while the sheet is up must photograph what is BEHIND
		// it, not our own buttons. Take the overlay down and give the
		// compositor a couple of frames to actually repaint without it.
		Q_EMIT hideSheet();
		m_sheetUp = false;
		m_idle.stop();
		settle = 250;
	}

	QTimer::singleShot(settle, this, [this] { m_capture->request(); });
}

void Backend::onReady(const QString &file)
{
	const qint64 captured = m_clock.elapsed();
	setImage(file);
	m_sheetUp = true;
	m_busy = false;
	m_idle.start();

	// The shutter, HERE, one line before the flash. The QML plays the white
	// flash off showSheet(), so the click and the light land together and read
	// as one event -- which is the whole point of moving it off the key press.
	feedback(QStringLiteral("screen-capture"));
	Q_EMIT showSheet();

	qInfo("screenglaze: capture in %lld ms (sheet at %lld ms)",
		captured, m_clock.elapsed());

	// Spectacle exits on its own after a while. Start it again now so the next
	// combo is the fast path rather than the 1.3 s one.
	m_capture->warmUp();
}

void Backend::onFailed(const QString &reason)
{
	m_busy = false;
	qWarning("screenglaze: the capture failed: %s", qPrintable(reason));
	say(QStringLiteral("Could not capture: %1").arg(reason));
}

void Backend::setImage(const QString &file)
{
	m_file = file;
	m_image = file.isEmpty() ? QString() : QUrl::fromLocalFile(file).toString();

	m_size = QSize();
	if (!file.isEmpty()) {
		// Read only the header. Decoding a 2400x1080 PNG just to learn its
		// shape would cost more than the screenshot did.
		QImageReader reader(file);
		m_size = reader.size();
	}
	Q_EMIT imageChanged();
}

void Backend::watchScreenLock()
{
	const auto service = QStringLiteral("org.freedesktop.ScreenSaver");
	const auto path = QStringLiteral("/org/freedesktop/ScreenSaver");

	// Follow the signal instead of asking at shutter time. Asking would mean a
	// blocking round trip between the buttons going down and the click coming
	// out -- normally a millisecond, but a stalled screensaver would turn the
	// shutter into a pause, and it is the one thing that must not be late.
	QDBusConnection::sessionBus().connect(service, path, service,
		QStringLiteral("ActiveChanged"), this, SLOT(onLockChanged(bool)));

	// One reading now, so the first screenshot is not taken on the assumption
	// that the screen was unlocked when the service started.
	QDBusMessage m = QDBusMessage::createMethodCall(service, path, service,
		QStringLiteral("GetActive"));
	auto *watch = new QDBusPendingCallWatcher(
		QDBusConnection::sessionBus().asyncCall(m), this);
	connect(watch, &QDBusPendingCallWatcher::finished, this,
		[this](QDBusPendingCallWatcher *w) {
			const QDBusPendingReply<bool> r = *w;
			if (r.isValid())
				m_locked = r.value();
			w->deleteLater();
		});
}

void Backend::onLockChanged(bool locked)
{
	m_locked = locked;
}

QString Backend::moveToGallery()
{
	if (m_file.isEmpty() || !QFile::exists(m_file))
		return {};

	const QString dir = galleryDir();
	if (!QDir().mkpath(dir)) {
		say(QStringLiteral("Could not create %1").arg(dir));
		return {};
	}

	const QString target = dir + QLatin1Char('/') + QFileInfo(m_file).fileName();
	if (!QFile::rename(m_file, target)) {
		if (!QFile::copy(m_file, target)) {
			say(QStringLiteral("Could not save to %1").arg(dir));
			return {};
		}
		QFile::remove(m_file);
	}
	m_file.clear();
	return target;
}

void Backend::save()
{
	const QString saved = moveToGallery();
	if (saved.isEmpty())
		return;
	say(QStringLiteral("Saved to %1").arg(QFileInfo(saved).dir().dirName()));
	finish();
}

QString Backend::shareUrl()
{
	// Saves first and returns the saved copy, which is what gets shared.
	//
	// Giving a path inside ~/.cache would be handing the other application a file
	// we are about to delete. Phones do the same: what you share is the screenshot
	// that is already in the gallery.
	//
	// NOTHING IS OPENED HERE ANY MORE. Before, this called the OpenURI portal with
	// 'ask', which brings up Plasma's "open with": a desktop window that does not
	// fit on this screen, and which besides offered a single entry -- on this phone
	// Koko is the only thing that declares it can open a PNG. "Open with" is not
	// "share". Who shows the destinations now is Share.qml, with Purpose's model;
	// this only leaves the file in its place.
	const QString saved = moveToGallery();
	if (saved.isEmpty())
		return {};
	m_idle.stop();   // the share sheet rules while it is open
	return QUrl::fromLocalFile(saved).toString();
}

void Backend::sharedDone(bool shared)
{
	const QString where = QFileInfo(galleryDir()).fileName();
	say(shared ? QStringLiteral("Shared, and saved to %1").arg(where)
		   : QStringLiteral("Saved to %1").arg(where));
	finish();
}

void Backend::discard()
{
	if (!m_file.isEmpty())
		QFile::remove(m_file);
	m_file.clear();
	say(QStringLiteral("Screenshot discarded"));
	finish();
}

bool Backend::crop(qreal x, qreal y, qreal w, qreal h)
{
	if (m_file.isEmpty())
		return false;

	QImage img(m_file);
	if (img.isNull())
		return false;

	QRect r(qRound(x * img.width()), qRound(y * img.height()),
		qRound(w * img.width()), qRound(h * img.height()));
	r = r.intersected(img.rect());
	// A crop of nothing is a tap that missed, not a request to make an empty
	// file. Say no and leave the picture as it was.
	if (r.width() < 8 || r.height() < 8)
		return false;

	const QImage cut = img.copy(r);
	// A new name, not the same one: QML's Image caches by URL, and rewriting
	// the file underneath it would leave the old picture on screen.
	const QString target = QStringLiteral("%1/crop-%2.png")
		.arg(m_capture->stagingDir(),
			QString::number(QDateTime::currentMSecsSinceEpoch()));
	if (!cut.save(target, "PNG"))
		return false;

	QFile::remove(m_file);
	setImage(target);
	m_idle.start();
	return true;
}

void Backend::feedback(const QString &event)
{
	QDBusMessage m = QDBusMessage::createMethodCall(
		QStringLiteral("org.sigxcpu.Feedback"),
		QStringLiteral("/org/sigxcpu/Feedback"),
		QStringLiteral("org.sigxcpu.Feedback"),
		QStringLiteral("TriggerFeedback"));
	m << QStringLiteral("org.surya.Screenglaze") << event << QVariantMap() << int(-1);
	// Asynchronous: the click has to start now, and waiting for feedbackd to
	// confirm it would put the very delay we are trying to hide back in.
	QDBusConnection::sessionBus().asyncCall(m);
}

void Backend::say(const QString &text)
{
	m_notice = text;
	Q_EMIT noticeChanged();
}

void Backend::finish()
{
	m_idle.stop();
	m_sheetUp = false;
	Q_EMIT hideSheet();
	setImage(QString());
}

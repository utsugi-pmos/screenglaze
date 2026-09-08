// SPDX-License-Identifier: LGPL-2.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QSize>
#include <QString>
#include <QTimer>

class Buttons;
class Capture;

// Everything the QML is allowed to do, and the order the pieces happen in.
//
// The order matters more than it looks. The flash CANNOT come first: our own
// overlay is a window like any other, so a white rectangle drawn before the
// shutter would end up inside the picture. What happens instead is what a
// phone actually does -- the click and the buzz fire immediately, because
// neither of them can be photographed, and the flash plays when the picture is
// already safely on disk.
class Backend : public QObject
{
	Q_OBJECT
	// The name the object answers to on the bus. Without it QtDBus derives one
	// from the C++ class -- "local.Backend" -- and a caller asking for
	// org.surya.Screenglaze is told there is no such interface, on an object
	// that is very much there. The service name being right makes it look like
	// the method is missing rather than renamed.
	Q_CLASSINFO("D-Bus Interface", "org.surya.Screenglaze")

	// file:// URL of the pending capture, or empty. QML feeds this to Image.
	Q_PROPERTY(QString image READ image NOTIFY imageChanged)
	Q_PROPERTY(int imageWidth READ imageWidth NOTIFY imageChanged)
	Q_PROPERTY(int imageHeight READ imageHeight NOTIFY imageChanged)
	Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
	Q_PROPERTY(bool armed READ armed CONSTANT)

public:
	explicit Backend(QObject *parent = nullptr);

	QString image() const { return m_image; }
	int imageWidth() const { return m_size.width(); }
	int imageHeight() const { return m_size.height(); }
	QString notice() const { return m_notice; }
	bool armed() const;

	// Take one now, as if the combo had been pressed. This is what the D-Bus
	// entry point and the launcher icon call. Q_SCRIPTABLE is what puts it --
	// and only it -- on the bus; the other invokables stay private to the QML.
	Q_INVOKABLE Q_SCRIPTABLE void shoot();

	Q_INVOKABLE void save();
	// Saves and returns the file:// of the saved copy, so Share.qml can pass it
	// to Purpose. Empty if it could not be saved.
	Q_INVOKABLE QString shareUrl();
	// Called by Share.qml when it closes, whether it shared or not.
	Q_INVOKABLE void sharedDone(bool shared);
	Q_INVOKABLE void discard();
	// Rectangle in 0..1 of the current image. Normalised so the QML never has
	// to know the real pixel size or worry about device scaling.
	Q_INVOKABLE bool crop(qreal x, qreal y, qreal w, qreal h);

	// Fires a feedbackd event: sound, vibration and LED at once, from the
	// device's own theme. "screen-capture" is a real entry in the default
	// theme, so this is the shutter click and the buzz in one call.
	Q_INVOKABLE void feedback(const QString &event);

private Q_SLOTS:
	// Reached through the old-style SLOT() macro from QDBusConnection::connect,
	// which is why it is a real slot and not a lambda.
	void onLockChanged(bool locked);

Q_SIGNALS:
	void imageChanged();
	void noticeChanged();
	// The QML window listens to these instead of binding to a property, so
	// that showing the sheet can also restart its animations.
	void showSheet();
	void hideSheet();

private:
	void onCombo();
	void onReady(const QString &file);
	void onFailed(const QString &reason);
	void setImage(const QString &file);
	void watchScreenLock();
	// Returns the saved path, or empty on failure.
	QString moveToGallery();
	void say(const QString &text);
	void finish();

	Buttons *m_buttons = nullptr;
	Capture *m_capture = nullptr;

	QString m_file;   // absolute path in the staging dir
	QString m_image;  // the same thing as a file:// URL, for QML
	QSize m_size;
	QString m_notice;

	bool m_busy = false;
	bool m_sheetUp = false;
	// Kept up to date by a signal rather than asked for when the buttons are
	// pressed. A blocking D-Bus call sits between the press and the shutter
	// click, and the click is the one thing that has to be instant.
	bool m_locked = false;
	// Nothing is ever lost by walking away: if the sheet is ignored it saves
	// itself rather than sitting on top of the screen forever.
	QTimer m_idle;
	// From the buttons going down to the sheet being up. Reported every time.
	QElapsedTimer m_clock;
};

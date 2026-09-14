// SPDX-License-Identifier: LGPL-2.0-or-later
#include "backend.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>

namespace {
const auto busName = QStringLiteral("org.surya.Screenglaze");

// Theme icons, by name, for QML.
//
// Plain QtQuick cannot paint an icon by its name -- there is no image provider
// for that -- and the sheet's buttons dodge it by carrying their SVG inside. But
// the names of the share destinations are set by EACH Purpose plugin
// ("telegram", "mail-message", "document-save"...), so there is nothing to put
// inside there: they have to be resolved against the theme.
//
// QIcon is already configured in main() with its search paths and with breeze as
// a fallback, which is what is needed for this to find something when starting
// from systemd.
class ThemeIcons : public QQuickImageProvider
{
public:
	ThemeIcons()
		: QQuickImageProvider(QQuickImageProvider::Pixmap)
	{
	}

	QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requested) override
	{
		const int side = requested.width() > 0 ? requested.width() : 48;
		QPixmap p = QIcon::fromTheme(id).pixmap(side, side);
		// A destination without an icon would still be usable, but a row with a
		// gap where there should be something reads as "this is broken".
		if (p.isNull())
			p = QIcon::fromTheme(QStringLiteral("emblem-shared")).pixmap(side, side);
		if (size)
			*size = p.size();
		return p;
	}
};
} // namespace

int main(int argc, char *argv[])
{
	const QStringList args = [argc, argv] {
		QStringList l;
		for (int i = 1; i < argc; ++i)
			l << QString::fromLocal8Bit(argv[i]);
		return l;
	}();

	// The overlay has to sit ON TOP of whatever was being photographed --
	// including the panel and the task switcher -- and go away without ever
	// becoming an entry in the task bar. That is what the layer-shell protocol
	// is for, and this environment variable is exactly what
	// LayerShellQt::Shell::useLayerShell() sets. Setting it directly saves
	// linking a library to write one line.
	//
	// It has to happen BEFORE QGuiApplication, which is when the Wayland
	// platform plugin picks its shell integration.
	//
	// The escape hatch is deliberate: if a KWin update ever breaks layer-shell,
	// SCREENGLAZE_NO_LAYER_SHELL=1 turns the overlay back into an ordinary window and
	// the application still works, just less prettily.
	//
	// SCREENGLAZE_NO_LAYER_SHELL was SCREENGLAZE_SIN_CAPA; both still work.
	if (qEnvironmentVariableIsEmpty("SCREENGLAZE_NO_LAYER_SHELL")
		&& qEnvironmentVariableIsEmpty("SCREENGLAZE_SIN_CAPA"))
		qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");

	// Wayland reports the wrong physical DPI on this phone, and rounding the
	// scale factor makes the buttons come out a size that does not match the
	// rest of the system. Same reason as PocoNav.
	QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
		Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

	QGuiApplication app(argc, argv);
	QCoreApplication::setApplicationName(QStringLiteral("screenglaze"));

	// Only one instance. The bus name is the whole entry point now: the key
	// chord lives in phone-keyconfig, which calls shoot() on
	// org.surya.Screenglaze, and a second copy could not own that name --
	// being a service that restarts itself, that is a state you could easily
	// end up in by hand.
	auto bus = QDBusConnection::sessionBus();
	if (!bus.registerService(busName)) {
		// Already running: hand the request over and get out of the way. That
		// makes `screenglaze` from a terminal behave as a shutter button
		// instead of starting a rival copy.
		QDBusMessage m = QDBusMessage::createMethodCall(busName,
			QStringLiteral("/"), busName, QStringLiteral("shoot"));
		bus.call(m, QDBus::Block, 3000);
		std::printf("screenglaze was already running: I asked it for a capture\n");
		return 0;
	}

	// The icon theme has to be named explicitly and its search paths added by
	// hand. A bare QGuiApplication inherits nothing from Plasma, and started
	// from systemd there is no XDG_DATA_DIRS either, so every themed icon comes
	// out empty. PocoNav learned this the hard way; the fix is the same.
	QStringList iconPaths = QIcon::themeSearchPaths();
	const QStringList candidates = {
		QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
			+ QStringLiteral("/icons"),
		QStringLiteral("/usr/local/share/icons"),
		QStringLiteral("/usr/share/icons"),
	};
	for (const QString &d : candidates)
		if (!iconPaths.contains(d))
			iconPaths << d;
	QIcon::setThemeSearchPaths(iconPaths);
	QIcon::setFallbackThemeName(QStringLiteral("breeze"));
	if (QIcon::themeName().isEmpty())
		QIcon::setThemeName(QStringLiteral("breeze"));

	QGuiApplication::setDesktopFileName(QStringLiteral("screenglaze"));

	// Closing the sheet closes the only window there is. Without this the
	// process would exit with it, and the service would restart in a loop for
	// as long as the user kept taking screenshots.
	QGuiApplication::setQuitOnLastWindowClosed(false);

	Backend backend;
	bus.registerObject(QStringLiteral("/"), &backend,
		QDBusConnection::ExportScriptableInvokables);

	QQmlApplicationEngine engine;
	engine.addImageProvider(QStringLiteral("icon"), new ThemeIcons);
	engine.rootContext()->setContextProperty(QStringLiteral("app"), &backend);
	engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Screenglaze/main.qml")));
	if (engine.rootObjects().isEmpty())
		return 1;

	// --capture: take one straight away. Handy for testing over ssh, without
	// phone-keyconfig in the loop. --capturar, its old name, still works.
	if (args.contains(QStringLiteral("--capture"))
		|| args.contains(QStringLiteral("--capturar")))
		QTimer::singleShot(500, &backend, &Backend::shoot);

	return app.exec();
}

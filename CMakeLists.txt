# Screenglaze: the screenshot key combo, and the sheet that follows it.
#
# Built as a real binary rather than a shell script for one reason: the key
# combo has to be watched from a process that never dies, and that process also
# has to own an EVIOCGRAB on the power button for the few milliseconds the combo
# lasts. A script cannot hold a grab across two devices without racing itself.
cmake_minimum_required(VERSION 3.20)
project(screenglaze VERSION 1.2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# DBus is not optional here: it is how the screenshot is actually taken. See
# capture.cpp for why KWin's own interface cannot be used directly.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick DBus)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(screenglaze
	src/main.cpp
	src/backend.cpp
	src/backend.h
	src/buttons.cpp
	src/buttons.h
	src/capture.cpp
	src/capture.h
)

# Glaze is a singleton: CMake has to be told, or the module registers it as an
# ordinary type and every `Glaze.ink` in the QML is silently undefined.
#
# It is NOT called Palette, and that is not a style choice. QtQuick has had its
# own `Palette` value type since 6.6, so `import QtQuick` brings one into scope
# and it wins. The symptom is not an error about a clash -- it is a screenful of
# "Unable to assign [undefined]", because every property was read off the wrong
# type. A name nothing else can claim is worth more here than a tidy one.
set_source_files_properties(Glaze.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(screenglaze
	URI Screenglaze
	VERSION 1.0
	QML_FILES
		main.qml
		Glaze.qml
		Sheet.qml
		Cropper.qml
		Button.qml
		Share.qml
)

target_link_libraries(screenglaze PRIVATE
	Qt6::Core
	Qt6::Gui
	Qt6::Qml
	Qt6::Quick
	Qt6::DBus
)

include(GNUInstallDirs)
install(TARGETS screenglaze
	BUNDLE DESTINATION .
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES screenglaze.desktop
	DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES screenglaze.svg
	DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

# The udev rule is what makes the two buttons readable at all. It has to be
# packaged, not left to the setup script: without it the service starts, finds
# no readable device and does nothing, which looks exactly like a working
# install until you press the combo.
install(FILES 71-screenglaze.rules
	DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d)

# The user service. Installed disabled; `systemctl --user enable` is the setup
# script's job, so that installing the package never starts watching keys
# behind the user's back.
install(FILES screenglaze.service
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/user)

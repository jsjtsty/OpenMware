#include "MainWindow.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	// An empty organization keeps the data folder at <AppData>/OpenMware (not OpenMware/OpenMware).
	QCoreApplication::setOrganizationName(QString());
	QCoreApplication::setApplicationName(QStringLiteral("OpenMware"));
	QGuiApplication::setDesktopFileName(QStringLiteral("openmware"));
	MainWindow window;
	window.show();
	// Development aid: OMW_LAUNCHER_SHOT=file.png saves a screenshot of the window and quits.
	if (const QByteArray shot = qgetenv("OMW_LAUNCHER_SHOT"); !shot.isEmpty()) {
		QTimer::singleShot(800, &app, [&window, &app, shot] {
			window.grab().save(QString::fromLocal8Bit(shot));
			app.quit();
		});
	}
	return app.exec();
}

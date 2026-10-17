#include "Player.h"

#include "Library.h"
#include "Strings.h"

#include <QFile>
#include <QFileInfo>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

Player::Player(Library& library, QObject* parent) : QObject(parent), library_(library) {}

QString Player::start(const Game& game) {
	if (process_) return QString();
	const QString exe = library_.playerPath();
	if (!QFileInfo::exists(exe)) return T("error_player_missing").arg(QFileInfo(exe).fileName());
	const QString folder = library_.gameFolder(game);
	if (!QFileInfo(folder).isDir()) return T("error_folder_unavailable");

	auto* p = new QProcess(this);
	p->setProgram(exe);
	p->setArguments({folder, QStringLiteral("--keys"), library_.keysPath(game), QStringLiteral("--data-dir"), library_.dataPath(game),
	                 QStringLiteral("--shaders"), library_.shadersPath()});
	p->setProcessChannelMode(QProcess::MergedChannels);
	p->setStandardOutputFile(library_.logPath(game));
#ifdef Q_OS_WIN
	p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
	const QString id = game.id;
	connect(p, &QProcess::finished, this, [this, p, id](int code, QProcess::ExitStatus status) {
		const double seconds = startedAt_.msecsTo(QDateTime::currentDateTime()) / 1000.0;
		const bool asked = stopping_;
		process_ = nullptr;
		id_.clear();
		stopping_ = false;
		p->deleteLater();
		QString error;
		if (!asked && (status == QProcess::CrashExit || code != 0)) {
			error = T("error_player_exit").arg(status == QProcess::CrashExit ? -1 : code);
			if (const Game* g = library_.find(id)) {
				QFile log(library_.logPath(*g));
				if (log.open(QIODevice::ReadOnly)) {
					const QByteArray all = log.readAll();
					error += QStringLiteral("\n\n") + QString::fromUtf8(all.right(500));
				}
			}
		}
		emit finished(id, seconds, error);
	});
	p->start();
	if (!p->waitForStarted(10000)) {
		const QString why = p->errorString();
		delete p;
		return T("error_player_start").arg(why);
	}
	process_ = p;
	id_ = id;
	startedAt_ = QDateTime::currentDateTime();
	stopping_ = false;
	emit started(id);
	return QString();
}

void Player::stop() {
	if (!process_) return;
	stopping_ = true;
	process_->terminate(); // asks the window to close on Windows, SIGTERM on Linux
	QProcess* p = process_;
	QTimer::singleShot(4000, p, [p] { if (p->state() != QProcess::NotRunning) p->kill(); });
}

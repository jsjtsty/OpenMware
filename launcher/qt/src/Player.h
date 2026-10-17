#pragma once
#include <QDateTime>
#include <QObject>
#include <QProcess>

class Library;
struct Game;

// Runs one game in the openmware-player child process (crash isolation: the toolkit and the engine's SDL window do not share
// a process or a main loop).
class Player : public QObject {
	Q_OBJECT
public:
	Player(Library& library, QObject* parent = nullptr);

	bool running() const { return process_ != nullptr; }
	QString playingId() const { return id_; }

	// Returns an error message, or an empty string when the game was started.
	QString start(const Game& game);
	void stop();

signals:
	void started(const QString& id);
	// error is empty after a normal exit or a stop from the launcher.
	void finished(const QString& id, double seconds, const QString& error);

private:
	Library& library_;
	QProcess* process_ = nullptr;
	QString id_;
	QDateTime startedAt_;
	bool stopping_ = false;
};

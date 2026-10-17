#pragma once
#include <QDateTime>
#include <QDir>
#include <QObject>
#include <QString>
#include <QStringList>
#include <vector>

// One game of the library. App-owned files live in <library root>/Games/<id>/ : game/ (when copied), keys.txt, data/ (saves),
// last-run.log. A linked game is opened from the folder the user picked, nothing is duplicated.
struct Game {
	QString id;
	QString name;
	QDateTime addedAt;
	QDateTime lastPlayedAt;
	double playSeconds = 0;
	bool copied = true;
	QString linkedPath; // when !copied

	double coverHue() const; // 0...1, stable per game
};

enum class GameStatus { Ready, KeysMissing, Incomplete };

struct ProbeResult {
	bool ok = false;
	int archives = 0;
	QString message;
};

namespace Keys {
// One 256-bit key (64 hex digits) per line.
QStringList parse(const QString& text);
int count(const QString& file);
} // namespace Keys

class Library : public QObject {
	Q_OBJECT
public:
	explicit Library(QObject* parent = nullptr);

	const std::vector<Game>& games() const { return games_; }
	const Game* find(const QString& id) const;
	QString root() const { return root_; }

	QString folder(const Game& g) const;
	QString keysPath(const Game& g) const;
	QString dataPath(const Game& g) const;
	QString logPath(const Game& g) const;
	QString gameFolder(const Game& g) const; // where the .npk files are
	QString playerPath() const;
	QString shadersPath() const;

	GameStatus status(const Game& g) const;
	int archiveCount(const Game& g, qint64* bytes = nullptr) const;
	qint64 saveBytes(const Game& g) const;
	int keyCount(const Game& g) const { return Keys::count(keysPath(g)); }
	ProbeResult probe(const Game& g) const;

	static QStringList archives(const QString& dir); // absolute paths of the .npk files in a folder
	static bool isGameFolder(const QString& dir);
	static qint64 freeBytes(const QString& dir);

	// Adding: createGame makes the folders, the caller copies the archives (when copied), then commit() stores the game.
	Game createGame(const QString& name, bool copied, const QString& linkedPath);
	QString copyTarget(const Game& g) const { return gameFolder(g); }
	bool commit(const Game& g, const QString& keysText);
	void discard(const Game& g);

	void rename(const QString& id, const QString& name);
	int setKeys(const QString& id, const QString& text); // returns the number of valid keys, 0 = file left alone
	void remove(const QString& id);
	void clearSaves(const QString& id);
	bool exportSaves(const QString& id, const QString& zipPath) const;
	void markPlayed(const QString& id);
	void addPlayTime(const QString& id, double seconds);

signals:
	void changed();

private:
	void load();
	void save() const;
	Game* findMutable(const QString& id);

	QString root_;
	std::vector<Game> games_;
};

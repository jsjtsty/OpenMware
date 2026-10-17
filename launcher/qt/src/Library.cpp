#include "Library.h"

#include "ZipWriter.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUuid>

double Game::coverHue() const {
	quint32 h = 2166136261u;
	for (const char c : id.toUtf8()) h = (h ^ static_cast<quint8>(c)) * 16777619u;
	return (h % 360) / 360.0;
}

namespace Keys {

QStringList parse(const QString& text) {
	static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{64}$"));
	QStringList out;
	for (const QString& line : text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts)) {
		const QString t = line.trimmed();
		if (hex.match(t).hasMatch()) out << t.toLower();
	}
	return out;
}

int count(const QString& file) {
	QFile f(file);
	if (!f.open(QIODevice::ReadOnly)) return 0;
	return parse(QString::fromUtf8(f.readAll())).size();
}

} // namespace Keys

namespace {

qint64 folderBytes(const QString& dir) {
	qint64 total = 0;
	QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) { it.next(); total += it.fileInfo().size(); }
	return total;
}

} // namespace

Library::Library(QObject* parent) : QObject(parent) {
	root_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	QDir().mkpath(root_ + QStringLiteral("/Games"));
	load();
}

const Game* Library::find(const QString& id) const {
	for (const Game& g : games_) if (g.id == id) return &g;
	return nullptr;
}

Game* Library::findMutable(const QString& id) {
	for (Game& g : games_) if (g.id == id) return &g;
	return nullptr;
}

QString Library::folder(const Game& g) const { return root_ + QStringLiteral("/Games/") + g.id; }
QString Library::keysPath(const Game& g) const { return folder(g) + QStringLiteral("/keys.txt"); }
QString Library::dataPath(const Game& g) const { return folder(g) + QStringLiteral("/data"); }
QString Library::logPath(const Game& g) const { return folder(g) + QStringLiteral("/last-run.log"); }
QString Library::gameFolder(const Game& g) const { return g.copied ? folder(g) + QStringLiteral("/game") : g.linkedPath; }

QString Library::playerPath() const {
#ifdef Q_OS_WIN
	return QCoreApplication::applicationDirPath() + QStringLiteral("/openmware-player.exe");
#else
	return QCoreApplication::applicationDirPath() + QStringLiteral("/openmware-player");
#endif
}

QString Library::shadersPath() const { return QCoreApplication::applicationDirPath() + QStringLiteral("/shaders"); }

QStringList Library::archives(const QString& dir) {
	QStringList out;
	const QFileInfoList list = QDir(dir).entryInfoList({QStringLiteral("*.npk")}, QDir::Files, QDir::Name | QDir::IgnoreCase);
	for (const QFileInfo& f : list) out << f.absoluteFilePath();
	return out;
}

bool Library::isGameFolder(const QString& dir) {
	for (const QString& f : archives(dir)) if (QFileInfo(f).fileName().compare(QStringLiteral("script.npk"), Qt::CaseInsensitive) == 0) return true;
	return false;
}

qint64 Library::freeBytes(const QString& dir) {
	QDir().mkpath(dir);
	return QStorageInfo(dir).bytesAvailable();
}

GameStatus Library::status(const Game& g) const {
	if (!isGameFolder(gameFolder(g))) return GameStatus::Incomplete;
	return keyCount(g) == 0 ? GameStatus::KeysMissing : GameStatus::Ready;
}

int Library::archiveCount(const Game& g, qint64* bytes) const {
	const QStringList files = archives(gameFolder(g));
	if (bytes) {
		*bytes = 0;
		for (const QString& f : files) *bytes += QFileInfo(f).size();
	}
	return files.size();
}

qint64 Library::saveBytes(const Game& g) const { return folderBytes(dataPath(g)); }

ProbeResult Library::probe(const Game& g) const {
	ProbeResult r;
	QProcess p;
	p.setProgram(playerPath());
	p.setArguments({gameFolder(g), QStringLiteral("--probe"), QStringLiteral("--keys"), keysPath(g)});
	p.start();
	if (!p.waitForFinished(30000)) {
		p.kill();
		r.message = QStringLiteral("timeout");
		return r;
	}
	const QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
	if (out.startsWith(QStringLiteral("ok "))) {
		r.ok = true;
		r.archives = out.mid(3).toInt();
	} else {
		r.message = out.startsWith(QStringLiteral("error ")) ? out.mid(6) : (out.isEmpty() ? p.errorString() : out);
	}
	return r;
}

// --- storage -----------------------------------------------------------------------------------

void Library::load() {
	QFile f(root_ + QStringLiteral("/library.json"));
	if (!f.open(QIODevice::ReadOnly)) return;
	const QJsonArray list = QJsonDocument::fromJson(f.readAll()).array();
	for (const QJsonValue& v : list) {
		const QJsonObject o = v.toObject();
		Game g;
		g.id = o.value(QStringLiteral("id")).toString();
		if (g.id.isEmpty() || !QDir(folder(g)).exists()) continue;
		g.name = o.value(QStringLiteral("name")).toString();
		g.addedAt = QDateTime::fromString(o.value(QStringLiteral("addedAt")).toString(), Qt::ISODate);
		g.lastPlayedAt = QDateTime::fromString(o.value(QStringLiteral("lastPlayedAt")).toString(), Qt::ISODate);
		g.playSeconds = o.value(QStringLiteral("playSeconds")).toDouble();
		g.copied = o.value(QStringLiteral("copied")).toBool(true);
		g.linkedPath = o.value(QStringLiteral("linkedPath")).toString();
		games_.push_back(g);
	}
}

void Library::save() const {
	QJsonArray list;
	for (const Game& g : games_) {
		QJsonObject o;
		o[QStringLiteral("id")] = g.id;
		o[QStringLiteral("name")] = g.name;
		o[QStringLiteral("addedAt")] = g.addedAt.toString(Qt::ISODate);
		if (g.lastPlayedAt.isValid()) o[QStringLiteral("lastPlayedAt")] = g.lastPlayedAt.toString(Qt::ISODate);
		o[QStringLiteral("playSeconds")] = g.playSeconds;
		o[QStringLiteral("copied")] = g.copied;
		if (!g.copied) o[QStringLiteral("linkedPath")] = g.linkedPath;
		list.append(o);
	}
	QFile f(root_ + QStringLiteral("/library.json"));
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(list).toJson());
}

Game Library::createGame(const QString& name, bool copied, const QString& linkedPath) {
	Game g;
	g.id = QUuid::createUuid().toString(QUuid::Id128).left(8);
	g.name = name;
	g.addedAt = QDateTime::currentDateTime();
	g.copied = copied;
	g.linkedPath = copied ? QString() : linkedPath;
	QDir().mkpath(dataPath(g));
	if (copied) QDir().mkpath(gameFolder(g));
	return g;
}

bool Library::commit(const Game& g, const QString& keysText) {
	const QStringList keys = Keys::parse(keysText);
	if (!keys.isEmpty()) {
		QFile f(keysPath(g));
		if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
		f.write((keys.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
	}
	games_.push_back(g);
	save();
	emit changed();
	return true;
}

void Library::discard(const Game& g) { QDir(folder(g)).removeRecursively(); }

void Library::rename(const QString& id, const QString& name) {
	Game* g = findMutable(id);
	const QString trimmed = name.trimmed();
	if (!g || trimmed.isEmpty() || trimmed == g->name) return;
	g->name = trimmed;
	save();
	emit changed();
}

int Library::setKeys(const QString& id, const QString& text) {
	const Game* g = find(id);
	if (!g) return 0;
	const QStringList keys = Keys::parse(text);
	if (keys.isEmpty()) return 0;
	QFile f(keysPath(*g));
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write((keys.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
	emit changed();
	return keys.size();
}

void Library::remove(const QString& id) {
	const Game* g = find(id);
	if (!g) return;
	QDir(folder(*g)).removeRecursively();
	std::erase_if(games_, [&](const Game& x) { return x.id == id; });
	save();
	emit changed();
}

void Library::clearSaves(const QString& id) {
	const Game* g = find(id);
	if (!g) return;
	QDir(dataPath(*g)).removeRecursively();
	QDir().mkpath(dataPath(*g));
	emit changed();
}

bool Library::exportSaves(const QString& id, const QString& zipPath) const {
	const Game* g = find(id);
	return g && zipFolder(dataPath(*g), zipPath);
}

void Library::markPlayed(const QString& id) {
	if (Game* g = findMutable(id)) {
		g->lastPlayedAt = QDateTime::currentDateTime();
		save();
		emit changed();
	}
}

void Library::addPlayTime(const QString& id, double seconds) {
	if (Game* g = findMutable(id)) {
		g->playSeconds += seconds;
		save();
		emit changed();
	}
}

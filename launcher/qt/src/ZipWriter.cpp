#include "ZipWriter.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>

#include <array>
#include <vector>

namespace {

quint32 crc32(const QByteArray& data) {
	static const std::array<quint32, 256> table = [] {
		std::array<quint32, 256> t{};
		for (quint32 i = 0; i < 256; i++) {
			quint32 c = i;
			for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			t[i] = c;
		}
		return t;
	}();
	quint32 c = 0xFFFFFFFFu;
	for (const char b : data) c = table[(c ^ static_cast<quint8>(b)) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

void put16(QByteArray& b, quint16 v) { b.append(char(v & 0xFF)).append(char(v >> 8)); }
void put32(QByteArray& b, quint32 v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }

struct Entry {
	QByteArray name;
	quint32 crc, size, offset;
	quint16 time, date;
};

} // namespace

bool zipFolder(const QString& folder, const QString& zipPath) {
	QFile out(zipPath);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
	std::vector<Entry> entries;
	const QDir base(folder);
	QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const QString path = it.next();
		QFile f(path);
		if (!f.open(QIODevice::ReadOnly)) return false;
		const QByteArray data = f.readAll();
		if (data.size() >= 0xFFFFFFFFLL) return false; // no zip64
		const QDateTime t = it.fileInfo().lastModified();
		Entry e;
		e.name = base.relativeFilePath(path).toUtf8();
		e.crc = crc32(data);
		e.size = static_cast<quint32>(data.size());
		e.offset = static_cast<quint32>(out.pos());
		e.time = static_cast<quint16>((t.time().hour() << 11) | (t.time().minute() << 5) | (t.time().second() / 2));
		e.date = static_cast<quint16>(((qMax(t.date().year(), 1980) - 1980) << 9) | (t.date().month() << 5) | t.date().day());
		QByteArray h;
		put32(h, 0x04034b50);
		put16(h, 20);
		put16(h, 0x0800); // names are UTF-8
		put16(h, 0);      // stored
		put16(h, e.time);
		put16(h, e.date);
		put32(h, e.crc);
		put32(h, e.size);
		put32(h, e.size);
		put16(h, static_cast<quint16>(e.name.size()));
		put16(h, 0);
		h.append(e.name);
		if (out.write(h) != h.size() || out.write(data) != data.size()) return false;
		entries.push_back(e);
	}
	const quint32 centralStart = static_cast<quint32>(out.pos());
	for (const Entry& e : entries) {
		QByteArray h;
		put32(h, 0x02014b50);
		put16(h, 20);
		put16(h, 20);
		put16(h, 0x0800);
		put16(h, 0);
		put16(h, e.time);
		put16(h, e.date);
		put32(h, e.crc);
		put32(h, e.size);
		put32(h, e.size);
		put16(h, static_cast<quint16>(e.name.size()));
		put16(h, 0);
		put16(h, 0);
		put16(h, 0);
		put16(h, 0);
		put32(h, 0);
		put32(h, e.offset);
		h.append(e.name);
		if (out.write(h) != h.size()) return false;
	}
	QByteArray end;
	put32(end, 0x06054b50);
	put16(end, 0);
	put16(end, 0);
	put16(end, static_cast<quint16>(entries.size()));
	put16(end, static_cast<quint16>(entries.size()));
	put32(end, static_cast<quint32>(out.pos()) - centralStart);
	put32(end, centralStart);
	put16(end, 0);
	return out.write(end) == end.size();
}

#include "GameDelegate.h"

#include "Library.h"
#include "Strings.h"

#include <QApplication>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace {
constexpr int kCardW = 180;
constexpr int kCoverH = 220;
constexpr int kCardH = kCoverH + 70;
} // namespace

GameModel::GameModel(Library& library, QObject* parent) : QAbstractListModel(parent), library_(library) {
	connect(&library_, &Library::changed, this, [this] {
		beginResetModel();
		endResetModel();
	});
}

int GameModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(library_.games().size()); }

QVariant GameModel::data(const QModelIndex& index, int role) const {
	if (!index.isValid() || index.row() >= rowCount()) return {};
	const Game& g = library_.games()[static_cast<size_t>(index.row())];
	if (role == Qt::UserRole) return g.id;
	if (role == Qt::DisplayRole) return g.name;
	return {};
}

QString GameModel::idAt(int row) const {
	return row >= 0 && row < rowCount() ? library_.games()[static_cast<size_t>(row)].id : QString();
}

int GameModel::rowOf(const QString& id) const {
	for (int i = 0; i < rowCount(); i++) if (library_.games()[static_cast<size_t>(i)].id == id) return i;
	return -1;
}

void GameModel::setPlaying(const QString& id) {
	playing_ = id;
	if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1));
}

GameDelegate::GameDelegate(Library& library, GameModel& model, QObject* parent)
	: QStyledItemDelegate(parent), library_(library), model_(model) {}

QSize GameDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const { return {kCardW + 24, kCardH + 24}; }

void GameDelegate::paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const {
	const Game* g = library_.find(index.data(Qt::UserRole).toString());
	if (!g) return;
	p->save();
	p->setRenderHint(QPainter::Antialiasing);
	p->setRenderHint(QPainter::TextAntialiasing);
	const QPalette& pal = option.palette;
	const bool selected = option.state & QStyle::State_Selected;
	const bool hover = option.state & QStyle::State_MouseOver;
	const QRectF card(option.rect.x() + 12, option.rect.y() + 12, kCardW, kCardH);

	// card background and selection ring
	QPainterPath cardPath;
	cardPath.addRoundedRect(card, 12, 12);
	QColor bg = pal.color(QPalette::Base);
	const bool dark = pal.color(QPalette::Window).lightness() < 128;
	bg = dark ? bg.lighter(hover ? 130 : 115) : bg.darker(hover ? 104 : 100);
	p->fillPath(cardPath, bg);
	if (selected) p->setPen(QPen(pal.color(QPalette::Highlight), 2.5));
	else p->setPen(QPen(pal.color(QPalette::Mid), 1));
	p->drawPath(cardPath);

	// generated cover: gradient from the game's hue, the first letter of the name
	const QRectF cover(card.x() + 8, card.y() + 8, card.width() - 16, kCoverH - 8);
	QPainterPath coverPath;
	coverPath.addRoundedRect(cover, 8, 8);
	const double hue = g->coverHue();
	QLinearGradient grad(cover.topLeft(), cover.bottomRight());
	grad.setColorAt(0, QColor::fromHslF(hue, 0.55, 0.62));
	grad.setColorAt(1, QColor::fromHslF(std::fmod(hue + 0.08, 1.0), 0.6, 0.36));
	p->fillPath(coverPath, grad);
	QFont big = option.font;
	big.setPixelSize(72);
	big.setBold(true);
	p->setFont(big);
	p->setPen(QColor(255, 255, 255, 235));
	const QString initial = g->name.isEmpty() ? QStringLiteral("?") : g->name.left(g->name.at(0).isHighSurrogate() ? 2 : 1).toUpper();
	p->drawText(cover, Qt::AlignCenter, initial);

	// status badge on the cover
	const GameStatus st = library_.status(*g);
	const bool playing = model_.playing() == g->id;
	QString badge;
	QColor badgeColor;
	if (playing) { badge = T("playing"); badgeColor = QColor(46, 160, 67); }
	else if (st == GameStatus::Ready) { badge = T("status_ready"); badgeColor = QColor(0, 0, 0, 150); }
	else if (st == GameStatus::KeysMissing) { badge = T("status_keys_missing"); badgeColor = QColor(214, 137, 16); }
	else { badge = T("status_incomplete"); badgeColor = QColor(192, 57, 43); }
	QFont small = option.font;
	small.setPixelSize(12);
	small.setBold(true);
	p->setFont(small);
	const QFontMetrics sfm(small);
	const QRectF badgeRect(cover.x() + 8, cover.y() + 8, sfm.horizontalAdvance(badge) + 18, sfm.height() + 6);
	QPainterPath badgePath;
	badgePath.addRoundedRect(badgeRect, badgeRect.height() / 2, badgeRect.height() / 2);
	p->fillPath(badgePath, badgeColor);
	p->setPen(Qt::white);
	p->drawText(badgeRect, Qt::AlignCenter, badge);

	// name and last played
	QFont nameFont = option.font;
	nameFont.setBold(true);
	nameFont.setPixelSize(15);
	p->setFont(nameFont);
	p->setPen(pal.color(QPalette::Text));
	const QRectF nameRect(card.x() + 10, card.y() + kCoverH + 4, card.width() - 20, 26);
	p->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(nameFont).elidedText(g->name, Qt::ElideRight, static_cast<int>(nameRect.width())));
	QFont sub = option.font;
	sub.setPixelSize(12);
	p->setFont(sub);
	QColor subColor = pal.color(QPalette::Text);
	subColor.setAlpha(150);
	p->setPen(subColor);
	const QString when = g->lastPlayedAt.isValid() ? T("last_played").arg(QLocale().toString(g->lastPlayedAt.date(), QLocale::ShortFormat)) : T("never_played");
	p->drawText(QRectF(card.x() + 10, card.y() + kCoverH + 30, card.width() - 20, 22), Qt::AlignLeft | Qt::AlignVCenter, when);
	p->restore();
}

#pragma once
#include <QAbstractListModel>
#include <QStyledItemDelegate>

class Library;

// The library as a list model; the item data is the game id.
class GameModel : public QAbstractListModel {
	Q_OBJECT
public:
	explicit GameModel(Library& library, QObject* parent = nullptr);
	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QString idAt(int row) const;
	int rowOf(const QString& id) const;
	void setPlaying(const QString& id);
	QString playing() const { return playing_; }

private:
	Library& library_;
	QString playing_;
};

// Paints a game as a card: generated cover, name, status and last played.
class GameDelegate : public QStyledItemDelegate {
public:
	GameDelegate(Library& library, GameModel& model, QObject* parent = nullptr);
	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
	Library& library_;
	GameModel& model_;
};

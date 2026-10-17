#pragma once
#include <QMainWindow>

class GameDelegate;
class GameModel;
class Library;
class Player;
class QAction;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QStackedWidget;

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	MainWindow();

protected:
	void closeEvent(QCloseEvent* event) override;

private:
	QString currentId() const;
	void selectionChanged();
	void refreshDetail();
	void updateActions();
	void play();
	void addGame();
	void showError(const QString& text);
	void verify();
	void replaceKeys();
	void exportSaves();
	void clearSaves();
	void removeGame();
	void showLog();
	void contextMenu(const QPoint& pos);

	Library* library_;
	Player* player_;
	GameModel* model_;
	GameDelegate* delegate_;
	QListView* list_;
	QStackedWidget* pages_;   // 0 empty library, 1 games
	QStackedWidget* detailStack_; // 0 hint, 1 detail
	QAction* addAction_;
	QAction* playAction_;
	QAction* stopAction_;
	QAction* aboutAction_;

	QLineEdit* nameEdit_;
	QLabel* statusLabel_;
	QLabel* sizeValue_;
	QLabel* archivesValue_;
	QLabel* storageValue_;
	QLabel* playTimeValue_;
	QLabel* saveValue_;
	QLabel* keysValue_;
	QPushButton* playButton_;
	QPushButton* verifyButton_;
	QPushButton* keysButton_;
	QPushButton* exportButton_;
	QPushButton* revealGameButton_;
	QPushButton* revealSavesButton_;
	QPushButton* logButton_;
	QPushButton* clearButton_;
	QPushButton* removeButton_;
};

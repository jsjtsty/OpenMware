#pragma once
#include <QDialog>

class Library;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// Add a game: pick the folder, name it, copy or link, import the keys.
class AddGameDialog : public QDialog {
	Q_OBJECT
public:
	AddGameDialog(Library& library, QWidget* parent = nullptr);
	QString addedId() const { return addedId_; }

private slots:
	void browse();
	void importKeys();
	void accept() override;

private:
	void setFolder(const QString& dir);
	void refresh();

	Library& library_;
	QLineEdit* folder_;
	QLabel* folderInfo_;
	QLineEdit* name_;
	QCheckBox* copy_;
	QLabel* copyInfo_;
	QPlainTextEdit* keys_;
	QLabel* keysInfo_;
	QPushButton* addButton_;
	QStringList archives_;
	qint64 totalBytes_ = 0;
	QString addedId_;
};

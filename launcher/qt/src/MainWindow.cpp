#include "MainWindow.h"

#include "AboutDialog.h"
#include "AddGameDialog.h"
#include "GameDelegate.h"
#include "Library.h"
#include "Player.h"
#include "Strings.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QLabel* valueLabel() {
	auto* l = new QLabel;
	l->setTextInteractionFlags(Qt::TextSelectableByMouse);
	l->setWordWrap(true);
	return l;
}

} // namespace

MainWindow::MainWindow() {
	setWindowTitle(T("app_name"));
	setWindowIcon(QIcon(QStringLiteral(":/icon.png")));
	resize(1100, 720);

	library_ = new Library(this);
	player_ = new Player(*library_, this);
	model_ = new GameModel(*library_, this);
	delegate_ = new GameDelegate(*library_, *model_, this);

	// --- toolbar ---
	auto* bar = addToolBar(T("library_title"));
	bar->setMovable(false);
	bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	addAction_ = bar->addAction(style()->standardIcon(QStyle::SP_FileDialogNewFolder), T("add_game"));
	addAction_->setShortcut(QKeySequence::New);
	playAction_ = bar->addAction(style()->standardIcon(QStyle::SP_MediaPlay), T("play"));
	stopAction_ = bar->addAction(style()->standardIcon(QStyle::SP_MediaStop), T("stop"));
	auto* spacer = new QWidget;
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	bar->addWidget(spacer);
	aboutAction_ = bar->addAction(style()->standardIcon(QStyle::SP_MessageBoxInformation), T("about"));

	// --- library grid (or the empty state) ---
	list_ = new QListView;
	list_->setModel(model_);
	list_->setItemDelegate(delegate_);
	list_->setViewMode(QListView::IconMode);
	list_->setResizeMode(QListView::Adjust);
	list_->setMovement(QListView::Static);
	list_->setUniformItemSizes(true);
	list_->setSelectionMode(QAbstractItemView::SingleSelection);
	list_->setMouseTracking(true);
	list_->setFrameShape(QFrame::NoFrame);
	list_->setContextMenuPolicy(Qt::CustomContextMenu);
	list_->setSpacing(0);

	auto* empty = new QWidget;
	{
		auto* v = new QVBoxLayout(empty);
		v->addStretch(1);
		auto* icon = new QLabel;
		icon->setPixmap(QPixmap(QStringLiteral(":/icon.png")).scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
		icon->setAlignment(Qt::AlignCenter);
		auto* title = new QLabel(T("empty_title"));
		QFont f = title->font();
		f.setPointSizeF(f.pointSizeF() * 1.5);
		f.setBold(true);
		title->setFont(f);
		title->setAlignment(Qt::AlignCenter);
		auto* body = new QLabel(T("empty_body"));
		body->setWordWrap(true);
		body->setAlignment(Qt::AlignCenter);
		body->setMaximumWidth(420);
		auto* button = new QPushButton(T("add_game"));
		button->setMinimumHeight(34);
		connect(button, &QPushButton::clicked, this, &MainWindow::addGame);
		v->addWidget(icon);
		v->addWidget(title);
		v->addWidget(body, 0, Qt::AlignHCenter);
		v->addSpacing(8);
		v->addWidget(button, 0, Qt::AlignHCenter);
		v->addStretch(1);
	}
	pages_ = new QStackedWidget;
	pages_->addWidget(empty);
	pages_->addWidget(list_);

	// --- detail panel ---
	auto* detail = new QWidget;
	{
		auto* v = new QVBoxLayout(detail);
		v->setContentsMargins(18, 18, 18, 18);
		v->setSpacing(10);
		nameEdit_ = new QLineEdit;
		QFont nf = nameEdit_->font();
		nf.setPointSizeF(nf.pointSizeF() * 1.4);
		nf.setBold(true);
		nameEdit_->setFont(nf);
		nameEdit_->setFrame(false);
		nameEdit_->setToolTip(T("detail_rename"));
		statusLabel_ = new QLabel;
		v->addWidget(nameEdit_);
		v->addWidget(statusLabel_);

		playButton_ = new QPushButton(T("play"));
		playButton_->setMinimumHeight(38);
		playButton_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
		v->addWidget(playButton_);

		auto* form = new QFormLayout;
		form->setLabelAlignment(Qt::AlignLeft);
		sizeValue_ = valueLabel();
		archivesValue_ = valueLabel();
		storageValue_ = valueLabel();
		playTimeValue_ = valueLabel();
		saveValue_ = valueLabel();
		keysValue_ = valueLabel();
		form->addRow(T("detail_size"), sizeValue_);
		form->addRow(T("detail_archives"), archivesValue_);
		form->addRow(T("detail_storage"), storageValue_);
		form->addRow(T("detail_keys"), keysValue_);
		form->addRow(T("detail_play_time"), playTimeValue_);
		form->addRow(T("detail_save_size"), saveValue_);
		v->addSpacing(6);
		v->addLayout(form);
		v->addSpacing(6);

		verifyButton_ = new QPushButton(T("detail_verify"));
		keysButton_ = new QPushButton(T("detail_keys_replace"));
		exportButton_ = new QPushButton(T("detail_export_saves"));
		revealGameButton_ = new QPushButton(T("detail_reveal_game"));
		revealSavesButton_ = new QPushButton(T("detail_reveal_saves"));
		logButton_ = new QPushButton(T("detail_log"));
		clearButton_ = new QPushButton(T("detail_clear_saves"));
		removeButton_ = new QPushButton(T("detail_remove"));
		for (QPushButton* b : {verifyButton_, keysButton_, exportButton_, revealGameButton_, revealSavesButton_, logButton_}) v->addWidget(b);
		v->addSpacing(8);
		for (QPushButton* b : {clearButton_, removeButton_}) {
			b->setStyleSheet(QStringLiteral("QPushButton { color: #c0392b; }"));
			v->addWidget(b);
		}
		v->addStretch(1);
	}
	auto* hint = new QLabel(T("select_hint"));
	hint->setAlignment(Qt::AlignCenter);
	detailStack_ = new QStackedWidget;
	detailStack_->addWidget(hint);
	detailStack_->addWidget(detail);
	detailStack_->setMinimumWidth(320);
	detailStack_->setMaximumWidth(420);

	auto* split = new QSplitter;
	split->addWidget(pages_);
	split->addWidget(detailStack_);
	split->setStretchFactor(0, 1);
	split->setChildrenCollapsible(false);
	setCentralWidget(split);

	// --- connections ---
	connect(addAction_, &QAction::triggered, this, &MainWindow::addGame);
	connect(playAction_, &QAction::triggered, this, &MainWindow::play);
	connect(playButton_, &QPushButton::clicked, this, [this] { player_->running() ? player_->stop() : play(); });
	connect(stopAction_, &QAction::triggered, player_, &Player::stop);
	connect(aboutAction_, &QAction::triggered, this, [this] { AboutDialog(this).exec(); });
	connect(list_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { selectionChanged(); });
	connect(list_, &QListView::doubleClicked, this, [this] { play(); });
	connect(list_, &QListView::customContextMenuRequested, this, &MainWindow::contextMenu);
	connect(library_, &Library::changed, this, [this] {
		pages_->setCurrentIndex(library_->games().empty() ? 0 : 1);
		refreshDetail();
		updateActions();
	});
	connect(nameEdit_, &QLineEdit::editingFinished, this, [this] {
		if (!currentId().isEmpty()) library_->rename(currentId(), nameEdit_->text());
	});
	connect(verifyButton_, &QPushButton::clicked, this, &MainWindow::verify);
	connect(keysButton_, &QPushButton::clicked, this, &MainWindow::replaceKeys);
	connect(exportButton_, &QPushButton::clicked, this, &MainWindow::exportSaves);
	connect(revealGameButton_, &QPushButton::clicked, this, [this] {
		if (const Game* g = library_->find(currentId())) QDesktopServices::openUrl(QUrl::fromLocalFile(library_->gameFolder(*g)));
	});
	connect(revealSavesButton_, &QPushButton::clicked, this, [this] {
		if (const Game* g = library_->find(currentId())) QDesktopServices::openUrl(QUrl::fromLocalFile(library_->dataPath(*g)));
	});
	connect(logButton_, &QPushButton::clicked, this, &MainWindow::showLog);
	connect(clearButton_, &QPushButton::clicked, this, &MainWindow::clearSaves);
	connect(removeButton_, &QPushButton::clicked, this, &MainWindow::removeGame);

	connect(player_, &Player::started, this, [this](const QString& id) {
		model_->setPlaying(id);
		library_->markPlayed(id);
		if (const Game* g = library_->find(id)) statusBar()->showMessage(T("playing") + QStringLiteral(": ") + g->name);
		updateActions();
		refreshDetail();
	});
	connect(player_, &Player::finished, this, [this](const QString& id, double seconds, const QString& error) {
		model_->setPlaying(QString());
		library_->addPlayTime(id, seconds);
		statusBar()->clearMessage();
		updateActions();
		refreshDetail();
		raise();
		activateWindow();
		if (!error.isEmpty()) showError(error);
	});

	pages_->setCurrentIndex(library_->games().empty() ? 0 : 1);
	if (!library_->games().empty()) list_->setCurrentIndex(model_->index(0));
	selectionChanged();
}

QString MainWindow::currentId() const { return model_->idAt(list_->currentIndex().row()); }

void MainWindow::selectionChanged() {
	refreshDetail();
	updateActions();
}

void MainWindow::updateActions() {
	const Game* g = library_->find(currentId());
	const bool running = player_->running();
	playAction_->setEnabled(g && !running && library_->status(*g) == GameStatus::Ready);
	stopAction_->setEnabled(running);
}

void MainWindow::refreshDetail() {
	const Game* g = library_->find(currentId());
	detailStack_->setCurrentIndex(g ? 1 : 0);
	if (!g) return;
	if (!nameEdit_->hasFocus()) nameEdit_->setText(g->name);
	const GameStatus st = library_->status(*g);
	const bool running = player_->running();
	const bool thisRunning = running && player_->playingId() == g->id;
	QString text;
	QString color;
	switch (st) {
	case GameStatus::Ready: text = T("status_ready"); color = QStringLiteral("#2e8b57"); break;
	case GameStatus::KeysMissing: text = T("status_keys_missing"); color = QStringLiteral("#d68910"); break;
	case GameStatus::Incomplete: text = T("status_incomplete"); color = QStringLiteral("#c0392b"); break;
	}
	if (thisRunning) { text = T("playing"); color = QStringLiteral("#2e8b57"); }
	statusLabel_->setText(QStringLiteral("● ") + text);
	statusLabel_->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(color));

	qint64 bytes = 0;
	const int archives = library_->archiveCount(*g, &bytes);
	sizeValue_->setText(formatBytes(bytes));
	archivesValue_->setText(QString::number(archives));
	storageValue_->setText(g->copied ? T("storage_copied") : T("storage_linked").arg(QDir::toNativeSeparators(g->linkedPath)));
	keysValue_->setText(T("detail_keys_count").arg(library_->keyCount(*g)));
	playTimeValue_->setText(g->playSeconds < 60 ? T("never_played") : formatDuration(g->playSeconds));
	const qint64 saves = library_->saveBytes(*g);
	saveValue_->setText(saves ? formatBytes(saves) : T("detail_no_saves"));

	playButton_->setText(thisRunning ? T("stop") : T("play"));
	playButton_->setIcon(style()->standardIcon(thisRunning ? QStyle::SP_MediaStop : QStyle::SP_MediaPlay));
	playButton_->setEnabled(thisRunning || (!running && st == GameStatus::Ready));
	playButton_->setToolTip(st == GameStatus::KeysMissing ? T("cannot_play_keys") : st == GameStatus::Incomplete ? T("cannot_play_incomplete") : QString());
	for (QPushButton* b : {verifyButton_, keysButton_, clearButton_, removeButton_}) b->setEnabled(!thisRunning);
	exportButton_->setEnabled(saves > 0);
}

void MainWindow::showError(const QString& text) { QMessageBox::warning(this, T("error_title"), text); }

void MainWindow::play() {
	const Game* g = library_->find(currentId());
	if (!g || player_->running() || library_->status(*g) != GameStatus::Ready) return;
	const QString err = player_->start(*g);
	if (!err.isEmpty()) showError(err);
}

void MainWindow::addGame() {
	AddGameDialog dialog(*library_, this);
	if (dialog.exec() == QDialog::Accepted) {
		const int row = model_->rowOf(dialog.addedId());
		if (row >= 0) list_->setCurrentIndex(model_->index(row));
	}
}

void MainWindow::verify() {
	const Game* g = library_->find(currentId());
	if (!g) return;
	QApplication::setOverrideCursor(Qt::WaitCursor);
	const ProbeResult r = library_->probe(*g);
	QApplication::restoreOverrideCursor();
	if (r.ok) QMessageBox::information(this, g->name, T("verify_ok").arg(r.archives));
	else QMessageBox::warning(this, g->name, T("verify_failed").arg(r.message));
}

void MainWindow::replaceKeys() {
	const Game* g = library_->find(currentId());
	if (!g) return;
	const QString file = QFileDialog::getOpenFileName(this, T("choose_keys"), QString(), T("text_files"));
	if (file.isEmpty()) return;
	QFile f(file);
	if (!f.open(QIODevice::ReadOnly)) return;
	const int n = library_->setKeys(g->id, QString::fromUtf8(f.readAll()));
	if (n > 0) QMessageBox::information(this, g->name, T("keys_replaced").arg(n));
	else QMessageBox::warning(this, g->name, T("add_keys_invalid"));
}

void MainWindow::exportSaves() {
	const Game* g = library_->find(currentId());
	if (!g) return;
	const QString zip = QFileDialog::getSaveFileName(this, T("save_zip"), g->name + QStringLiteral("-save.zip"), QStringLiteral("Zip (*.zip)"));
	if (zip.isEmpty()) return;
	if (library_->exportSaves(g->id, zip)) QMessageBox::information(this, g->name, T("saves_exported"));
	else showError(T("saves_export_failed"));
}

void MainWindow::clearSaves() {
	const Game* g = library_->find(currentId());
	if (!g) return;
	if (QMessageBox::question(this, g->name, T("confirm_clear_saves").arg(g->name)) == QMessageBox::Yes) library_->clearSaves(g->id);
}

void MainWindow::removeGame() {
	const Game* g = library_->find(currentId());
	if (!g) return;
	if (QMessageBox::question(this, g->name, T("confirm_remove").arg(g->name)) == QMessageBox::Yes) library_->remove(g->id);
}

void MainWindow::showLog() {
	const Game* g = library_->find(currentId());
	if (!g) return;
	QFile f(library_->logPath(*g));
	QDialog d(this);
	d.setWindowTitle(g->name);
	d.resize(720, 460);
	auto* v = new QVBoxLayout(&d);
	auto* text = new QPlainTextEdit;
	text->setReadOnly(true);
	text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	if (f.open(QIODevice::ReadOnly)) text->setPlainText(QString::fromUtf8(f.readAll()));
	v->addWidget(text);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
	v->addWidget(buttons);
	d.exec();
}

void MainWindow::contextMenu(const QPoint& pos) {
	const QModelIndex i = list_->indexAt(pos);
	if (!i.isValid()) return;
	list_->setCurrentIndex(i);
	QMenu menu(this);
	QAction* p = menu.addAction(T("play"), this, &MainWindow::play);
	p->setEnabled(playAction_->isEnabled());
	menu.addAction(T("detail_verify"), this, &MainWindow::verify);
	menu.addAction(T("detail_reveal_game"), this, [this] { revealGameButton_->click(); });
	menu.addSeparator();
	menu.addAction(T("detail_remove"), this, &MainWindow::removeGame);
	menu.exec(list_->viewport()->mapToGlobal(pos));
}

void MainWindow::closeEvent(QCloseEvent* event) {
	if (player_->running()) {
		if (QMessageBox::question(this, T("app_name"), T("quit_while_playing")) != QMessageBox::Yes) {
			event->ignore();
			return;
		}
		player_->stop();
	}
	event->accept();
}

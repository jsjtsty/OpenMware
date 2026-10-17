#include "AddGameDialog.h"

#include "Library.h"
#include "Strings.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

#include <atomic>

AddGameDialog::AddGameDialog(Library& library, QWidget* parent) : QDialog(parent), library_(library) {
	setWindowTitle(T("add_title"));
	setMinimumWidth(560);
	auto* root = new QVBoxLayout(this);
	root->setSpacing(10);

	auto heading = [&](const QString& text) {
		auto* l = new QLabel(text);
		QFont f = l->font();
		f.setBold(true);
		l->setFont(f);
		root->addSpacing(4);
		root->addWidget(l);
	};
	auto hint = [&](const QString& text) {
		auto* l = new QLabel(text);
		l->setWordWrap(true);
		QPalette p = l->palette();
		QColor c = p.color(QPalette::WindowText);
		c.setAlpha(160);
		p.setColor(QPalette::WindowText, c);
		l->setPalette(p);
		return l;
	};

	heading(T("add_folder"));
	root->addWidget(hint(T("add_folder_hint")));
	auto* row = new QHBoxLayout;
	folder_ = new QLineEdit;
	folder_->setReadOnly(true);
	auto* browse = new QPushButton(T("add_browse"));
	row->addWidget(folder_, 1);
	row->addWidget(browse);
	root->addLayout(row);
	folderInfo_ = new QLabel;
	folderInfo_->setWordWrap(true);
	root->addWidget(folderInfo_);

	heading(T("add_name"));
	name_ = new QLineEdit;
	root->addWidget(name_);

	copy_ = new QCheckBox(T("add_copy"));
	root->addSpacing(4);
	root->addWidget(copy_);
	copyInfo_ = static_cast<QLabel*>(hint(QString()));
	root->addWidget(copyInfo_);

	heading(T("add_keys"));
	auto* keyRow = new QHBoxLayout;
	auto* importKeys = new QPushButton(T("add_keys_import"));
	keyRow->addWidget(importKeys);
	keyRow->addStretch(1);
	root->addLayout(keyRow);
	root->addWidget(hint(T("add_keys_paste")));
	keys_ = new QPlainTextEdit;
	keys_->setFixedHeight(90);
	keys_->setLineWrapMode(QPlainTextEdit::NoWrap);
	QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	keys_->setFont(mono);
	root->addWidget(keys_);
	keysInfo_ = new QLabel;
	root->addWidget(keysInfo_);

	auto* buttons = new QDialogButtonBox;
	addButton_ = buttons->addButton(T("add_import"), QDialogButtonBox::AcceptRole);
	addButton_->setDefault(true);
	buttons->addButton(T("cancel"), QDialogButtonBox::RejectRole);
	root->addWidget(buttons);

	connect(browse, &QPushButton::clicked, this, &AddGameDialog::browse);
	connect(importKeys, &QPushButton::clicked, this, &AddGameDialog::importKeys);
	connect(buttons, &QDialogButtonBox::accepted, this, &AddGameDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(copy_, &QCheckBox::toggled, this, [this] { refresh(); });
	connect(keys_, &QPlainTextEdit::textChanged, this, [this] { refresh(); });
	connect(name_, &QLineEdit::textChanged, this, [this] { refresh(); });
	refresh();
}

void AddGameDialog::browse() {
	const QString dir = QFileDialog::getExistingDirectory(this, T("choose_folder"), folder_->text());
	if (!dir.isEmpty()) setFolder(dir);
}

void AddGameDialog::setFolder(const QString& dir) {
	folder_->setText(QDir::toNativeSeparators(dir));
	archives_ = Library::archives(dir);
	totalBytes_ = 0;
	for (const QString& f : archives_) totalBytes_ += QFileInfo(f).size();
	if (name_->text().isEmpty()) name_->setText(QFileInfo(dir).fileName());
	refresh();
}

void AddGameDialog::importKeys() {
	const QString file = QFileDialog::getOpenFileName(this, T("choose_keys"), QString(), T("text_files"));
	if (file.isEmpty()) return;
	QFile f(file);
	if (f.open(QIODevice::ReadOnly)) keys_->setPlainText(QString::fromUtf8(f.readAll()));
}

void AddGameDialog::refresh() {
	const QString dir = QDir::fromNativeSeparators(folder_->text());
	const bool isGame = !dir.isEmpty() && Library::isGameFolder(dir);
	if (dir.isEmpty()) folderInfo_->clear();
	else if (isGame) folderInfo_->setText(T("add_found").arg(archives_.size()).arg(formatBytes(totalBytes_)));
	else folderInfo_->setText(T("add_not_a_game"));
	folderInfo_->setStyleSheet(dir.isEmpty() || isGame ? QString() : QStringLiteral("color: #c0392b;"));

	copyInfo_->setText(copy_->isChecked() ? T("add_copy_on") : T("add_copy_off"));
	bool spaceOk = true;
	if (copy_->isChecked() && isGame && Library::freeBytes(library_.root()) < totalBytes_) {
		copyInfo_->setText(T("add_no_space").arg(formatBytes(totalBytes_)));
		spaceOk = false;
	}

	const int n = Keys::parse(keys_->toPlainText()).size();
	if (keys_->toPlainText().trimmed().isEmpty()) keysInfo_->clear();
	else if (n > 0) keysInfo_->setText(T("add_keys_ok").arg(n));
	else keysInfo_->setText(T("add_keys_invalid"));
	keysInfo_->setStyleSheet(n > 0 || keys_->toPlainText().trimmed().isEmpty() ? QString() : QStringLiteral("color: #c0392b;"));

	addButton_->setEnabled(isGame && spaceOk && !name_->text().trimmed().isEmpty());
}

void AddGameDialog::accept() {
	const QString dir = QDir::fromNativeSeparators(folder_->text());
	const bool copy = copy_->isChecked();
	Game game = library_.createGame(name_->text().trimmed(), copy, dir);

	if (copy) {
		QProgressDialog progress(T("add_copying"), T("cancel"), 0, 1000, this);
		progress.setWindowModality(Qt::WindowModal);
		progress.setMinimumDuration(0);
		progress.setValue(0);
		std::atomic<qint64> done{0};
		std::atomic<bool> cancel{false};
		std::atomic<bool> ok{true};
		const QStringList files = archives_;
		const QString target = library_.copyTarget(game);
		QThread* worker = QThread::create([&] {
			for (const QString& src : files) {
				QFile in(src), out(target + QLatin1Char('/') + QFileInfo(src).fileName());
				if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate)) { ok = false; return; }
				QByteArray buf(8 << 20, Qt::Uninitialized);
				while (!in.atEnd()) {
					if (cancel) { ok = false; return; }
					const qint64 n = in.read(buf.data(), buf.size());
					if (n < 0 || out.write(buf.constData(), n) != n) { ok = false; return; }
					done += n;
				}
			}
		});
		worker->start();
		connect(&progress, &QProgressDialog::canceled, this, [&] { cancel = true; });
		while (!worker->wait(50)) {
			progress.setLabelText(T("add_copying") + QStringLiteral("\n%1 / %2").arg(formatBytes(done), formatBytes(totalBytes_)));
			progress.setValue(totalBytes_ ? static_cast<int>(done * 1000 / totalBytes_) : 0);
			QCoreApplication::processEvents();
		}
		delete worker;
		progress.close();
		if (!ok) {
			library_.discard(game);
			if (!cancel) QMessageBox::warning(this, T("error_title"), T("add_failed").arg(QStringLiteral("I/O")));
			return;
		}
	}
	if (!library_.commit(game, keys_->toPlainText())) {
		library_.discard(game);
		QMessageBox::warning(this, T("error_title"), T("add_failed").arg(QStringLiteral("keys.txt")));
		return;
	}
	addedId_ = game.id;
	QDialog::accept();
}

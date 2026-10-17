#include "AboutDialog.h"

#include "Strings.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
	setWindowTitle(T("about_title"));
	setMinimumWidth(460);
	auto* root = new QVBoxLayout(this);
	root->setSpacing(8);

	auto* icon = new QLabel;
	icon->setPixmap(QPixmap(QStringLiteral(":/icon.png")).scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	icon->setAlignment(Qt::AlignCenter);
	root->addWidget(icon);

	auto* title = new QLabel(QStringLiteral("OpenMware"));
	QFont tf = title->font();
	tf.setPointSizeF(tf.pointSizeF() * 1.6);
	tf.setBold(true);
	title->setFont(tf);
	title->setAlignment(Qt::AlignCenter);
	root->addWidget(title);

	auto* version = new QLabel(T("about_version").arg(QStringLiteral(OMW_VERSION)));
	version->setAlignment(Qt::AlignCenter);
	root->addWidget(version);

	auto text = [&](const QString& t) {
		auto* l = new QLabel(t);
		l->setWordWrap(true);
		root->addWidget(l);
	};
	root->addSpacing(8);
	text(T("about_body"));
	auto* h = new QLabel(T("about_licenses"));
	QFont hf = h->font();
	hf.setBold(true);
	h->setFont(hf);
	root->addSpacing(6);
	root->addWidget(h);
	text(T("about_licenses_body"));
	text(T("about_ffmpeg"));

	auto* buttons = new QDialogButtonBox;
	auto* licenses = buttons->addButton(T("open_licenses"), QDialogButtonBox::ActionRole);
	buttons->addButton(T("close"), QDialogButtonBox::RejectRole);
	root->addSpacing(8);
	root->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(licenses, &QPushButton::clicked, this, [] {
		QDesktopServices::openUrl(QUrl::fromLocalFile(QCoreApplication::applicationDirPath() + QStringLiteral("/licenses")));
	});
}

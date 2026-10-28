#include "wg_widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <functional>

namespace wg {

static QString S(const char *u) { return QString::fromUtf8(u); }
static const char *kPalette[] = {"#17212b", "#1c3a5c", "#3a1c4a", "#1c4a3a", "#4a2a1c"};

SettingsDialog::SettingsDialog(const Settings &s, const Blacklist &bl, QWidget *parent)
	: QDialog(parent), _s(s), _bl(bl) {
	setWindowTitle("WorkGram");
	setFixedSize(760, 640);
	auto *root = new QHBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);

	auto *left = new QWidget;
	left->setObjectName("nav");
	left->setFixedWidth(200);
	auto *ll = new QVBoxLayout(left);
	ll->setContentsMargins(10, 16, 10, 16);
	auto *brand = new QLabel("WorkGram");
	brand->setObjectName("title");
	ll->addWidget(brand);
	_nav = new QListWidget;
	_nav->setFrameShape(QFrame::NoFrame);
	for (const char *t : {"Основные", "Лента", "Коллекции", "Шаблон", "Чёрный список", "Предложение"}) _nav->addItem(S(t));
	ll->addWidget(_nav, 1);

	_pages = new QStackedWidget;
	_pages->addWidget(buildBasicPage());          // порядок совпадает с enum Page и со списком слева
	_pages->addWidget(buildFeedPage());
	_pages->addWidget(buildCollectionsPage());
	_pages->addWidget(buildTemplatePage());
	_pages->addWidget(buildBlacklistPage());
	_pages->addWidget(buildSuggestionPage());
	connect(_nav, &QListWidget::currentRowChanged, _pages, &QStackedWidget::setCurrentIndex);
	_nav->setCurrentRow(0);
	root->addWidget(left);
	root->addWidget(_pages, 1);
	applyTheme();
}

void SettingsDialog::showPage(int i) { _nav->setCurrentRow(i); }

void SettingsDialog::setBlacklist(const Blacklist &bl) {
	_bl = bl;
	refreshBlacklist();
}

void SettingsDialog::changed() {
	if (!_loading) emit settingsChanged(_s);
}

void SettingsDialog::applyTheme() {
	const QColor menu(QString::fromStdString(_s.menuColor));
	setStyleSheet(QString(
		"QDialog{background:%1;} QWidget#nav{background:%2;}"
		"QLabel{color:#fff;font-size:14px;} QLabel#title{font-size:18px;font-weight:600;}"
		"QLabel#muted{color:#7f91a4;font-size:12px;}"
		"QListWidget{background:transparent;color:#fff;border:0;outline:0;font-size:14px;}"
		"QListWidget::item{padding:8px 12px;border-radius:8px;}"
		"QListWidget::item:selected{background:#2b5278;color:#fff;}"
		"QCheckBox{color:#fff;spacing:10px;font-size:14px;}"
		"QPushButton{background:#2b5278;color:#fff;border:0;border-radius:8px;padding:6px 14px;font-size:14px;}"
		"QSpinBox,QDoubleSpinBox,QLineEdit,QPlainTextEdit,QComboBox{background:#242f3d;color:#fff;border:0;"
		"border-radius:8px;padding:5px 10px;font-size:14px;}"
		"QFrame#card{background:#1d2a38;border-radius:10px;}"
		"QCheckBox::indicator,QListWidget::indicator{width:16px;height:16px;border:1px solid #5b6b7c;border-radius:4px;background:#242f3d;}"
		"QCheckBox::indicator:checked,QListWidget::indicator:checked{background:#5288c1;border:1px solid #5288c1;}"
		"QSlider::groove:horizontal{height:4px;background:#3a4a5c;border-radius:2px;}"
		"QSlider::sub-page:horizontal{background:#5288c1;border-radius:2px;}"
		"QSlider::handle:horizontal{width:14px;height:14px;margin:-5px 0;background:#fff;border-radius:7px;}")
		.arg(menu.name(), menu.darker(135).name()));
}

static QLabel *muted(const QString &t) {
	auto *l = new QLabel(t);
	l->setObjectName("muted");
	l->setWordWrap(true);
	return l;
}

// ---------- Лента ----------
QWidget *SettingsDialog::buildFeedPage() {
	auto *w = new QWidget;
	auto *l = new QVBoxLayout(w);
	l->setContentsMargins(28, 24, 28, 24);
	l->setSpacing(14);
	auto *title = new QLabel(S("Лента"));
	title->setObjectName("title");
	l->addWidget(title);
	l->addWidget(muted(S("Что показывать")));
	auto addCheck = [&](const QString &text, bool value, std::function<void(bool)> set) {
		auto *c = new QCheckBox(text);
		c->setChecked(value);
		connect(c, &QCheckBox::toggled, this, [this, set](bool v) { set(v); changed(); });
		l->addWidget(c);
	};
	addCheck(S("Маркет: новые лоты"), _s.showMarket, [this](bool v) { _s.showMarket = v; });
	addCheck(S("Улучшения: только что улучшенные подарки"), _s.showUpgrades, [this](bool v) { _s.showUpgrades = v; });
	addCheck(S("Анимация моделей в ленте"), _s.animateModels, [this](bool v) { _s.animateModels = v; });
	addCheck(S("Звук при новом подарке"), _s.soundOnNew, [this](bool v) { _s.soundOnNew = v; });

	auto *vr = new QHBoxLayout;
	vr->addWidget(new QLabel(S("Громкость")));
	auto *sl = new QSlider(Qt::Horizontal);
	sl->setRange(0, 100);
	sl->setValue(_s.soundVolume);
	vr->addWidget(sl, 1);
	auto *vl = new QLabel(QString::number(_s.soundVolume) + "%");
	vr->addWidget(vl);
	auto *test = new QPushButton(S("Проверить звук"));
	vr->addWidget(test);
	l->addLayout(vr);
	connect(sl, &QSlider::valueChanged, this, [this, vl](int v) {
		_s.soundVolume = v;
		vl->setText(QString::number(v) + "%");
		changed();
	});
	connect(test, &QPushButton::clicked, this, [this] { emit testSoundRequested(_s.soundVolume); });

	auto addSpin = [&](const QString &label, int lo, int hi, int value, std::function<void(int)> set) {
		auto *row = new QHBoxLayout;
		row->addWidget(new QLabel(label), 1);
		auto *sp = new QSpinBox;
		sp->setRange(lo, hi);
		sp->setValue(value);
		sp->setFixedWidth(110);
		connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, set](int v) { set(v); changed(); });
		row->addWidget(sp);
		l->addLayout(row);
	};
	addSpin(S("Подарков в ленте"), 5, 50, int(_s.feedCapacity), [this](int v) { _s.feedCapacity = size_t(v); });
	addSpin(S("Видимых карточек"), 1, 15, _s.visibleCards, [this](int v) { _s.visibleCards = v; });
	addSpin(S("Сообщений в час (максимум)"), 1, 60, _s.maxMessagesPerHour, [this](int v) { _s.maxMessagesPerHour = v; });
	l->addStretch(1);
	l->addWidget(muted(S("F9 открывает и закрывает это окно")));
	return w;
}

// ---------- Коллекции ----------
QWidget *SettingsDialog::buildCollectionsPage() {
	auto *w = new QWidget;
	auto *l = new QVBoxLayout(w);
	l->setContentsMargins(24, 20, 24, 20);
	l->setSpacing(10);
	auto *title = new QLabel(S("Коллекции"));
	title->setObjectName("title");
	l->addWidget(title);

	auto *card = new QFrame;
	card->setObjectName("card");
	auto *cl = new QHBoxLayout(card);
	_status = new QLabel(S("Не запущено"));
	_status->setWordWrap(true);
	cl->addWidget(_status, 1);
	_toggle = new QPushButton(S("Остановить"));
	connect(_toggle, &QPushButton::clicked, this, [this] {
		_running = !_running;
		_toggle->setText(_running ? S("Остановить") : S("Запустить"));
		emit monitoringToggled(_running);
	});
	cl->addWidget(_toggle);
	l->addWidget(card);

	auto *params = new QHBoxLayout;
	params->addWidget(new QLabel(S("Пауза между запросами")), 1);
	auto *poll = new QDoubleSpinBox;
	poll->setRange(1, 60);
	poll->setSingleStep(0.5);
	poll->setSuffix(S(" сек"));
	poll->setValue(_s.pollSeconds);
	poll->setFixedWidth(110);
	connect(poll, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) { _s.pollSeconds = v; changed(); });
	params->addWidget(poll);
	l->addLayout(params);
	auto *params2 = new QHBoxLayout;
	params2->addWidget(new QLabel(S("Лимит на коллекцию")), 1);
	auto *lim = new QSpinBox;
	lim->setRange(10, 200);
	lim->setValue(_s.limitPerCollection);
	lim->setFixedWidth(110);
	connect(lim, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { _s.limitPerCollection = v; changed(); });
	params2->addWidget(lim);
	l->addLayout(params2);

	auto *search = new QLineEdit;
	search->setPlaceholderText(S("Поиск коллекции"));
	l->addWidget(search);
	auto *bar = new QHBoxLayout;
	auto *all = new QPushButton(S("Выбрать все"));
	auto *none = new QPushButton(S("Снять все"));
	auto *refresh = new QPushButton(S("Обновить список"));
	bar->addWidget(all);
	bar->addWidget(none);
	bar->addWidget(refresh);
	bar->addStretch(1);
	l->addLayout(bar);

	_colList = new QListWidget;
	l->addWidget(_colList, 1);
	auto *autoNew = new QCheckBox(S("Включать новые коллекции автоматически"));
	autoNew->setChecked(_s.autoEnableNewCollections);
	connect(autoNew, &QCheckBox::toggled, this, [this](bool v) { _s.autoEnableNewCollections = v; changed(); });
	l->addWidget(autoNew);
	_colCount = muted("");
	l->addWidget(_colCount);

	auto collect = [this] {
		if (_loading) return;
		_s.enabledCollections.clear();
		for (int i = 0; i < _colList->count(); i++) {
			auto *it = _colList->item(i);
			if (it->checkState() == Qt::Checked) _s.enabledCollections.insert(it->data(Qt::UserRole).toLongLong());
		}
		refreshCollectionCount();
		changed();
	};
	connect(_colList, &QListWidget::itemChanged, this, [collect](QListWidgetItem *) { collect(); });
	connect(search, &QLineEdit::textChanged, this, [this](const QString &t) {
		for (int i = 0; i < _colList->count(); i++)
			_colList->item(i)->setHidden(!_colList->item(i)->text().contains(t, Qt::CaseInsensitive));
	});
	auto setAll = [this, collect](Qt::CheckState st) {
		_loading = true;
		for (int i = 0; i < _colList->count(); i++)
			if (!_colList->item(i)->isHidden()) _colList->item(i)->setCheckState(st);
		_loading = false;
		collect();
	};
	connect(all, &QPushButton::clicked, this, [setAll] { setAll(Qt::Checked); });
	connect(none, &QPushButton::clicked, this, [setAll] { setAll(Qt::Unchecked); });
	connect(refresh, &QPushButton::clicked, this, [this] { emit refreshCollectionsRequested(); });
	return w;
}

void SettingsDialog::setCollections(const std::vector<Collection> &list) {
	_loading = true;
	_colList->clear();
	for (const auto &c : list) {
		QString text = QString::fromStdString(c.name) + S("    в продаже ") + QString::number(c.forSale);
		if (c.newCount > 0) text += S("    ·  новых: ") + QString::number(c.newCount);
		auto *it = new QListWidgetItem(text);
		it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
		it->setCheckState(_s.enabledCollections.count(c.id) ? Qt::Checked : Qt::Unchecked);
		it->setData(Qt::UserRole, qlonglong(c.id));
		_colList->addItem(it);
	}
	_loading = false;
	refreshCollectionCount();
}

void SettingsDialog::refreshCollectionCount() {
	int on = 0;
	for (int i = 0; i < _colList->count(); i++) on += _colList->item(i)->checkState() == Qt::Checked;
	_colCount->setText(S("Выбрано ") + QString::number(on) + S(" из ") + QString::number(_colList->count())
		+ S(". Если Telegram ограничит запросы, приложение само замедлит опрос"));
}

void SettingsDialog::setMonitoringStatus(const QString &text, bool running) {
	_status->setText(text);
	_running = running;
	_toggle->setText(running ? S("Остановить") : S("Запустить"));
}

// ---------- Шаблон ----------
QWidget *SettingsDialog::buildTemplatePage() {
	auto *w = new QWidget;
	auto *l = new QVBoxLayout(w);
	l->setContentsMargins(28, 24, 28, 24);
	l->setSpacing(12);
	auto *title = new QLabel(S("Шаблон"));
	title->setObjectName("title");
	l->addWidget(title);
	l->addWidget(muted(S("Кнопка «Написать шаблоном» кладёт этот текст в поле ввода чата с продавцом. "
		"%gift% заменяется на название и номер подарка, %link% на ссылку.")));
	auto *top = new QHBoxLayout;
	_tplCombo = new QComboBox;
	top->addWidget(_tplCombo, 1);
	auto *add = new QPushButton(S("Новый"));
	auto *del = new QPushButton(S("Удалить"));
	top->addWidget(add);
	top->addWidget(del);
	l->addLayout(top);
	_tplText = new QPlainTextEdit;
	_tplText->setFixedHeight(130);
	l->addWidget(_tplText);
	auto *chips = new QHBoxLayout;
	auto *g = new QPushButton("+ %gift%");
	auto *k = new QPushButton("+ %link%");
	chips->addWidget(g);
	chips->addWidget(k);
	chips->addStretch(1);
	l->addLayout(chips);
	_preview = new QLabel;
	_preview->setWordWrap(true);
	_preview->setObjectName("card");
	_preview->setStyleSheet("QLabel#card{background:#1d2a38;border-radius:10px;padding:12px;}");
	l->addWidget(_preview);
	l->addStretch(1);

	loadTemplates();
	connect(_tplCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
		if (_loading || i < 0) return;
		_s.activeTemplate = i;
		_loading = true;
		_tplText->setPlainText(QString::fromStdString(_s.templates[size_t(i)]));
		_loading = false;
		refreshPreview();
		changed();
	});
	connect(_tplText, &QPlainTextEdit::textChanged, this, [this] {
		if (_loading || _s.templates.empty()) return;
		_s.templates[size_t(_s.activeTemplate)] = _tplText->toPlainText().toStdString();
		refreshPreview();
		changed();
	});
	connect(g, &QPushButton::clicked, this, [this] { _tplText->insertPlainText("%gift%"); });
	connect(k, &QPushButton::clicked, this, [this] { _tplText->insertPlainText("%link%"); });
	connect(add, &QPushButton::clicked, this, [this] {
		_s.templates.push_back("");
		_s.activeTemplate = int(_s.templates.size()) - 1;
		loadTemplates();
		changed();
	});
	connect(del, &QPushButton::clicked, this, [this] {
		if (_s.templates.size() <= 1) return;
		_s.templates.erase(_s.templates.begin() + _s.activeTemplate);
		_s.activeTemplate = std::max(0, _s.activeTemplate - 1);
		loadTemplates();
		changed();
	});
	return w;
}

void SettingsDialog::loadTemplates() {
	_loading = true;
	_tplCombo->clear();
	for (size_t i = 0; i < _s.templates.size(); i++) _tplCombo->addItem(S("Шаблон ") + QString::number(i + 1));
	_tplCombo->setCurrentIndex(_s.activeTemplate);
	_tplText->setPlainText(QString::fromStdString(_s.templates[size_t(_s.activeTemplate)]));
	_loading = false;
	refreshPreview();
}

void SettingsDialog::refreshPreview() {
	FeedItem sample;
	sample.title = "Plush Pepe";
	sample.num = 1204;
	sample.slug = "PlushPepe-1204";
	_preview->setText(S("Предпросмотр:\n") + QString::fromStdString(renderTemplate(_tplText->toPlainText().toStdString(), sample)));
}

// ---------- Чёрный список ----------
QWidget *SettingsDialog::buildBlacklistPage() {
	auto *w = new QWidget;
	auto *l = new QVBoxLayout(w);
	l->setContentsMargins(28, 24, 28, 24);
	l->setSpacing(12);
	auto *title = new QLabel(S("Чёрный список"));
	title->setObjectName("title");
	l->addWidget(title);
	l->addWidget(muted(S("Сюда попадает всё, что ты добавил правой кнопкой по карточке в ленте.")));
	_blList = new QListWidget;
	l->addWidget(_blList, 1);
	auto *rm = new QPushButton(S("Убрать выбранное"));
	rm->setFixedWidth(180);
	l->addWidget(rm);
	connect(rm, &QPushButton::clicked, this, [this] {
		auto *it = _blList->currentItem();
		if (!it) return;
		const int type = it->data(Qt::UserRole).toInt();
		const int64_t id = it->data(Qt::UserRole + 1).toLongLong();
		(type == 0 ? _bl.sellers : type == 1 ? _bl.collections : _bl.items).erase(id);
		_bl.labels.erase(id);
		refreshBlacklist();
		emit blacklistChanged(_bl);
	});
	refreshBlacklist();
	return w;
}

void SettingsDialog::refreshBlacklist() {
	_blList->clear();
	auto fill = [this](const std::set<int64_t> &ids, int type, const QString &prefix) {
		for (int64_t id : ids) {
			auto lab = _bl.labels.find(id);
			auto *it = new QListWidgetItem(prefix + (lab != _bl.labels.end() ? QString::fromStdString(lab->second) : QString::number(id)));
			it->setData(Qt::UserRole, type);
			it->setData(Qt::UserRole + 1, qlonglong(id));
			_blList->addItem(it);
		}
	};
	fill(_bl.sellers, 0, S("Продавец: "));
	fill(_bl.collections, 1, S("Коллекция: "));
	fill(_bl.items, 2, S("Подарок: "));
}

// ---------- Основные ----------
QWidget *SettingsDialog::buildBasicPage() {
	auto *w = new QWidget;
	auto *l = new QVBoxLayout(w);
	l->setContentsMargins(28, 24, 28, 24);
	l->setSpacing(12);
	auto *title = new QLabel(S("Основные"));
	title->setObjectName("title");
	l->addWidget(title);

	auto *card = new QFrame;
	card->setObjectName("card");
	auto *cl = new QVBoxLayout(card);
	auto row = [&](const QString &name, QLabel *&value) {
		auto *r = new QHBoxLayout;
		auto *n = new QLabel(name);
		n->setStyleSheet("color:#aab6c3;");
		r->addWidget(n);
		r->addStretch(1);
		value = new QLabel("—");
		r->addWidget(value);
		cl->addLayout(r);
	};
	row(S("Ключ"), _keyLabel);
	row(S("Осталось"), _daysLabel);
	row(S("Устройство"), _deviceLabel);
	cl->addWidget(muted(S("На сервер уходят: хэш устройства, ОС, версия, ник и время последнего пинга")));
	l->addWidget(card);

	auto swatches = [&](const QString &label, bool feed) {
		l->addWidget(muted(label));
		auto *row = new QHBoxLayout;
		row->setContentsMargins(4, 0, 0, 0);
		auto *buttons = new std::vector<QPushButton *>;
		auto refresh = [this, buttons, feed] {
			const std::string &cur = feed ? _s.feedColor : _s.menuColor;
			for (size_t i = 0; i < buttons->size(); i++) {
				const bool sel = cur == kPalette[i];
				(*buttons)[i]->setStyleSheet(QString("QPushButton{background:%1;border-radius:14px;border:%2;padding:0;}")
					.arg(kPalette[i], sel ? "3px solid #ffffff" : "1px solid #3a4a5c"));
			}
		};
		for (size_t i = 0; i < 5; i++) {
			auto *b = new QPushButton;
			b->setFixedSize(28, 28);
			buttons->push_back(b);
			row->addWidget(b);
			connect(b, &QPushButton::clicked, this, [this, i, feed, refresh] {
				(feed ? _s.feedColor : _s.menuColor) = kPalette[i];
				applyTheme();
				refresh();
				changed();
			});
		}
		row->addStretch(1);
		l->addLayout(row);
		refresh();
		return refresh;
	};
	auto r1 = swatches(S("Цвет ленты"), true);
	auto r2 = swatches(S("Цвет меню F9"), false);
	auto *reset = new QPushButton(S("Сбросить цвета"));
	reset->setFixedWidth(160);
	connect(reset, &QPushButton::clicked, this, [this, r1, r2] {
		_s.feedColor = _s.menuColor = kPalette[0];
		applyTheme();
		r1();
		r2();
		changed();
	});
	l->addWidget(reset);
	l->addStretch(1);
	return w;
}

QWidget *SettingsDialog::buildSuggestionPage() {
	auto *w = new QWidget;
	auto *l = new QVBoxLayout(w);
	l->setContentsMargins(28, 24, 28, 24);
	l->setSpacing(12);
	auto *title = new QLabel(S("Предложение"));
	title->setObjectName("title");
	l->addWidget(title);
	l->addWidget(muted(S("Ваше сообщение будет доставлено разработчику.")));
	_sugText = new QPlainTextEdit;
	_sugText->setPlaceholderText(S("Например: добавить поиск по номеру подарка"));
	_sugText->setFixedHeight(180);
	l->addWidget(_sugText);
	auto *row = new QHBoxLayout;
	auto *counter = muted("0 / 1000");
	row->addWidget(counter, 1);
	_sugSend = new QPushButton(S("Отправить"));
	row->addWidget(_sugSend);
	l->addLayout(row);
	_sugStatus = new QLabel;
	_sugStatus->setWordWrap(true);
	l->addWidget(_sugStatus);
	l->addStretch(1);

	connect(_sugText, &QPlainTextEdit::textChanged, this, [this, counter] {
		if (_sugText->toPlainText().size() > 1000) {
			_sugText->setPlainText(_sugText->toPlainText().left(1000));
			_sugText->moveCursor(QTextCursor::End);
		}
		counter->setText(QString::number(_sugText->toPlainText().size()) + " / 1000");
	});
	connect(_sugSend, &QPushButton::clicked, this, [this] {
		const QString text = _sugText->toPlainText().trimmed();
		if (text.isEmpty()) {
			_sugStatus->setText(S("Сначала напиши, что добавить."));
			return;
		}
		_sugSend->setEnabled(false);
		_sugStatus->setText(S("Отправляю…"));
		emit suggestionSubmitted(text);
	});
	return w;
}

void SettingsDialog::setSuggestionResult(bool ok, const QString &message) {
	_sugSend->setEnabled(true);
	_sugStatus->setText(message);
	if (ok) _sugText->clear();
}

void SettingsDialog::setKeyInfo(const QString &key, int daysLeft, const QString &device) {
	_keyLabel->setText(key);
	_daysLabel->setText(QString::number(daysLeft) + S(" дн."));
	_deviceLabel->setText(device);
}

} // namespace wg

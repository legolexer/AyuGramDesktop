// WorkGram: виджеты ленты и окна F9 на чистом Qt Widgets (без внутренних классов Telegram Desktop).
#pragma once

#include <functional>

#include <QDialog>
#include <QMetaType>
#include <QPixmap>
#include <QWidget>

#include "gift_feed_core.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QScrollArea;
class QStackedWidget;
class QHBoxLayout;
class QPushButton;

Q_DECLARE_METATYPE(wg::FeedItem)

namespace wg {

QString formatNumber(int64_t n);   // 1250 -> "1 250" (с неразрывным пробелом)

// Одна карточка: фон подарка, модель, ник, цена, подпись. Зелёная обводка, пока не открыта.
class GiftCard : public QWidget {
	Q_OBJECT
public:
	explicit GiftCard(const FeedItem &item, QWidget *parent = nullptr);
	const FeedItem &item() const { return _item; }
	void setModelPixmap(const QPixmap &pm) { _model = pm; update(); }
signals:
	void clicked(const wg::FeedItem &item);
	void blacklistRequested(int type, const wg::FeedItem &item);   // type: BlackType
protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	void enterEvent(QEnterEvent *e) override;
#else
	void enterEvent(QEvent *e) override;
#endif
	void leaveEvent(QEvent *e) override;
private:
	FeedItem _item;
	QPixmap _model;
	bool _hover = false;
};

// Полоса карточек над перепиской: колесо мыши листает по горизонтали.
class GiftStrip : public QWidget {
	Q_OBJECT
public:
	explicit GiftStrip(QWidget *parent = nullptr);
	void setItems(const std::deque<FeedItem> &items);
	void setBackgroundColor(const QColor &c);
	// Для тестов: заменяет окно подтверждения. Возвращает true, если пользователь согласен.
	void setConfirmHandler(std::function<bool(const QString &)> h) { _confirm = std::move(h); }
signals:
	void itemClicked(const wg::FeedItem &item);
	void blacklistRequested(int type, const wg::FeedItem &item);
protected:
	void paintEvent(QPaintEvent *e) override;
	bool eventFilter(QObject *o, QEvent *e) override;
private:
	bool confirmBlacklist(int type, const FeedItem &item);
	std::function<bool(const QString &)> _confirm;
	QScrollArea *_scroll = nullptr;
	QWidget *_row = nullptr;
	QHBoxLayout *_lay = nullptr;
	QColor _bg{0x17, 0x21, 0x2b};
};

// Окно F9: Лента, Коллекции, Шаблон, Чёрный список, Основные.
class SettingsDialog : public QDialog {
	Q_OBJECT
public:
	enum Page { PageBasic, PageFeed, PageCollections, PageTemplate, PageBlacklist, PageSuggestion };   // порядок вкладок, Основные первая
	SettingsDialog(const Settings &s, const Blacklist &bl, QWidget *parent = nullptr);
	void setCollections(const std::vector<Collection> &list);
	void setKeyInfo(const QString &key, int daysLeft, const QString &device);
	void setMonitoringStatus(const QString &text, bool running);
	void showPage(int index);
	void setBlacklist(const Blacklist &bl);
	const Settings &settings() const { return _s; }
	const Blacklist &blacklist() const { return _bl; }
signals:
	void settingsChanged(const wg::Settings &s);
	void blacklistChanged(const wg::Blacklist &bl);
	void testSoundRequested(int volume);
	void monitoringToggled(bool run);
	void refreshCollectionsRequested();
	void suggestionSubmitted(const QString &text);
public:
	void setSuggestionResult(bool ok, const QString &message);
private:
	QWidget *buildFeedPage();
	QWidget *buildCollectionsPage();
	QWidget *buildTemplatePage();
	QWidget *buildBlacklistPage();
	QWidget *buildBasicPage();
	QWidget *buildSuggestionPage();
	void changed();
	void applyTheme();
	void refreshBlacklist();
	void refreshCollectionCount();
	void refreshPreview();
	void loadTemplates();

	Settings _s;
	Blacklist _bl;
	QStackedWidget *_pages = nullptr;
	QListWidget *_nav = nullptr, *_colList = nullptr, *_blList = nullptr;
	QLabel *_status = nullptr, *_colCount = nullptr, *_preview = nullptr;
	QLabel *_keyLabel = nullptr, *_daysLabel = nullptr, *_deviceLabel = nullptr;
	QPushButton *_toggle = nullptr, *_sugSend = nullptr;
	QPlainTextEdit *_sugText = nullptr;
	QLabel *_sugStatus = nullptr;
	QComboBox *_tplCombo = nullptr;
	QPlainTextEdit *_tplText = nullptr;
	bool _running = true, _loading = false;
};

} // namespace wg

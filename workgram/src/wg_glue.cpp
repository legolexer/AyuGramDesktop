#include "wg_glue.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QEvent>
#include <QJsonDocument>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QSysInfo>
#include <QTimer>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <set>

#include "wg_bridge.h"
#include "wg_license.h"
#include "wg_log.h"
#include "wg_store.h"
#include "wg_widgets.h"

namespace WorkGram {
namespace {

// Настройки и чёрный список сохраняются между запусками (раздел WorkGram в реестре).
struct PersistedState {
	wg::Settings settings;
	wg::Blacklist blacklist;
	bool loaded = false;
};

PersistedState &Persisted() {
	static PersistedState st;
	if (!st.loaded) {
		st.loaded = true;
		QSettings store("WorkGram", "WorkGram");
		const auto doc = QJsonDocument::fromJson(store.value("settings").toString().toUtf8());
		if (doc.isObject()) wg::SettingsFromJson(doc.object(), &st.settings, &st.blacklist);
	}
	return st;
}

wg::Settings &CurrentSettings() { return Persisted().settings; }
wg::Blacklist &CurrentBlacklist() { return Persisted().blacklist; }

void SaveState() {
	QSettings store("WorkGram", "WorkGram");
	store.setValue("settings", QString::fromUtf8(QJsonDocument(wg::SettingsToJson(CurrentSettings(), CurrentBlacklist()))
		.toJson(QJsonDocument::Compact)));
}

QPointer<wg::SettingsDialog> Dialog;

// Адрес сервера ключей и его публичный ключ (не секрет). Версия приложения сверяется с командой /minver.
constexpr auto kServer = "https://keyhistory.duckdns.org";
constexpr auto kPublicKey = "c7f5aed6eb77bef0211007be16cab3455c0edb525b6f4452538adcbb0d4ebb19";
constexpr auto kVersion = "1.0.0";

std::unique_ptr<wg::LicenseClient> License;

bool Started = false;
bool ExitAsked = false;
QPointer<wg::KeyDialog> KeyWindow;

wg::LicenseClient::Config ServerConfig() {
#ifdef WG_TEST_HOOKS
	// Только для тестов: в рабочей сборке этого кода нет.
	if (qEnvironmentVariableIsSet("WG_TEST_URL")) {
		return {qEnvironmentVariable("WG_TEST_URL"), qEnvironmentVariable("WG_TEST_PUBKEY"), kVersion};
	}
#endif
	return {kServer, kPublicKey, kVersion};
}

void Persist() {
	QSettings store("WorkGram", "WorkGram");
	store.setValue("key", License->key());
	store.setValue("token", License->info().token);
	store.setValue("proxy", License->proxyUrl());
}

void ExitApp() {
	wg::Log("exit requested");
	ExitAsked = true;
	QTimer::singleShot(3000, [] { std::exit(0); });
	QCoreApplication::exit(0);
}

void BeginWork() {
	if (Started) return;
	Started = true;
	wg::Log("license ok, days left: " + QString::number(License->info().daysLeft));
	Persist();
	License->startPinging(90);          // раз в 90 секунд: сервер видит, что человек в сети, и может отозвать ключ
}

void ShowKeyDialog() {
	if (KeyWindow) return;
	wg::Log("show key window");
	KeyWindow = new wg::KeyDialog(License.get());
	KeyWindow->setAttribute(Qt::WA_DeleteOnClose);
	KeyWindow->setWindowModality(Qt::ApplicationModal);
	QObject::connect(KeyWindow.data(), &QDialog::accepted, [] { BeginWork(); });
	QObject::connect(KeyWindow.data(), &QDialog::rejected, [] {
		if (!Started) ExitApp();
	});
	KeyWindow->show();
	KeyWindow->raise();
	KeyWindow->activateWindow();
}

void OnLicenseChanged(const wg::LicenseInfo &info) {
	wg::Log("license status: " + QString::number(int(info.status)));
	if (!Started) {
		if (KeyWindow) return;              // окно ключа само обработает ответ
		if (License->allowed() && info.status == wg::LicenseStatus::Ok) BeginWork();
		else ShowKeyDialog();
		return;
	}
	if (info.status == wg::LicenseStatus::Ok) {
		Persist();
	}
	if (!License->allowed()) {
		auto *box = new QMessageBox(QMessageBox::Warning, "WorkGram",
			wg::licenseStatusText(info.status) + QString::fromUtf8(". Приложение будет закрыто."));
		box->setAttribute(Qt::WA_DeleteOnClose);
		box->show();
		ExitApp();
	}
}

QString MaskedKey() {
	if (!License || License->key().size() < 4) return QString::fromUtf8("нет");
	return "WG-****-****-****-" + License->key().right(4);
}

// ===================== лента и данные Telegram =====================
Bridge &TgBridge() {
	static Bridge bridge;
	return bridge;
}

wg::Feed &LiveFeed() {
	static wg::Feed feed(CurrentSettings().feedCapacity);
	return feed;
}

QPointer<wg::GiftStrip> Strip;
QPointer<QWidget> StripParent;
std::vector<wg::Collection> CollectionsCache;
std::set<int64_t> KnownCollections;
QString MonitorText;
bool MonitorRunning = false;

void ShowNote(const QString &text) {
	auto *box = new QMessageBox(QMessageBox::Information, "WorkGram", text, QMessageBox::Ok, StripParent.data());
	box->setAttribute(Qt::WA_DeleteOnClose);
	box->show();
}

void RepositionStrip() {
	if (!Strip || !StripParent) return;
	const int top = 32;                                         // под заголовком окна, чтобы не закрыть его кнопки
	const int left = int(StripParent->width() * 0.30);          // правая часть окна: над перепиской, не над списком чатов
	Strip->setGeometry(left, top, std::max(200, StripParent->width() - left), Strip->height());
	Strip->raise();
}

void RefreshStrip() {
	if (!Strip) return;
	Strip->setBackgroundColor(QColor(QString::fromStdString(CurrentSettings().feedColor)));
	Strip->setItems(LiveFeed().items());
	Strip->setVisible(MonitorRunning || !LiveFeed().items().empty());
	RepositionStrip();
}

class StripPlacer : public QObject {
public:
	using QObject::QObject;
	bool eventFilter(QObject *o, QEvent *e) override {
		if (e->type() == QEvent::Resize || e->type() == QEvent::Show) RepositionStrip();
		return QObject::eventFilter(o, e);
	}
};

void AddToBlacklist(int type, const wg::FeedItem &item) {
	const auto kind = wg::BlackType(type);
	if (kind == wg::BlackType::Seller && !wg::ownerReachable(item)) {
		ShowNote(QString::fromUtf8("У этого подарка владелец скрыт, добавить продавца в чёрный список нельзя."));
		return;
	}
	auto &bl = CurrentBlacklist();
	if (kind == wg::BlackType::Seller) {
		bl.add(kind, item.sellerId, item.sellerUsername.empty() ? "id " + std::to_string(item.sellerId) : "@" + item.sellerUsername);
	} else if (kind == wg::BlackType::Collection) {
		bl.add(kind, item.collectionId, item.title);
	} else {
		bl.add(kind, item.id, item.title + " #" + std::to_string(item.num));
	}
	SaveState();
	LiveFeed().applyBlacklist(bl);
	if (Dialog) Dialog->setBlacklist(bl);
	RefreshStrip();
}

void WriteByTemplate(const wg::FeedItem &item) {
	const auto &s = CurrentSettings();
	const int index = std::clamp(s.activeTemplate, 0, int(s.templates.size()) - 1);
	const QString text = QString::fromStdString(wg::renderTemplate(s.templates[size_t(index)], item));
	if (!TgBridge().openChatWithDraft) {
		ShowNote(QString::fromUtf8("Нет связи с Telegram: модуль не подключён."));
		return;
	}
	const QString error = TgBridge().openChatWithDraft(uint64_t(item.sellerId), text);
	if (!error.isEmpty()) ShowNote(error);
}

void OnCardClicked(const wg::FeedItem &item) {
	if (TgBridge().openGift && !item.slug.empty()) {
		TgBridge().openGift(QString::fromStdString(item.slug));     // стандартная карточка подарка Telegram
	}
	auto *menu = new QMenu(Strip.data());
	menu->setAttribute(Qt::WA_DeleteOnClose);
	const bool reachable = wg::ownerReachable(item);
	auto *write = menu->addAction(reachable ? QString::fromUtf8("Написать шаблоном")
		: QString::fromUtf8("Написать шаблоном (владелец скрыт)"));
	write->setEnabled(reachable);
	if (!reachable) {
		auto *why = menu->addAction(QString::fromUtf8("Telegram не отдаёт владельца этого подарка, написать ему нельзя"));
		why->setEnabled(false);
	}
	auto *copy = menu->addAction(QString::fromUtf8("Скопировать ссылку на подарок"));
	QObject::connect(write, &QAction::triggered, menu, [item] { WriteByTemplate(item); });
	QObject::connect(copy, &QAction::triggered, menu, [item] {
		QApplication::clipboard()->setText(QString::fromStdString(wg::giftLink(item)));
	});
	menu->popup(QCursor::pos());
}

void EnsureStrip() {
	if (Strip || !StripParent) return;
	Strip = new wg::GiftStrip(StripParent.data());
	QObject::connect(Strip.data(), &wg::GiftStrip::itemClicked, [](const wg::FeedItem &item) {
		LiveFeed().markSeen(item.id, item.kind);
		OnCardClicked(item);
		QTimer::singleShot(0, [] { RefreshStrip(); });
	});
	QObject::connect(Strip.data(), &wg::GiftStrip::blacklistRequested, [](int type, const wg::FeedItem &item) {
		AddToBlacklist(type, item);
	});
	StripParent->installEventFilter(new StripPlacer(Strip.data()));
	RefreshStrip();
}

void LoadCollections() {
	if (!TgBridge().requestCollections) {
		if (Dialog) Dialog->setMonitoringStatus(QString::fromUtf8("Нет связи с Telegram: модуль не подключён"), false);
		return;
	}
	if (Dialog) Dialog->setMonitoringStatus(QString::fromUtf8("Загружаю список коллекций у Telegram…"), MonitorRunning);
	TgBridge().requestCollections([](std::vector<wg::Collection> list, QString error) {
		if (!error.isEmpty()) {
			wg::Log("collections error: " + error);
			if (Dialog) Dialog->setMonitoringStatus(error, false);
			return;
		}
		auto &settings = CurrentSettings();
		if (settings.autoEnableNewCollections && !KnownCollections.empty()) {
			for (const auto &c : list) if (!KnownCollections.count(c.id)) settings.enabledCollections.insert(c.id);
		}
		for (const auto &c : list) KnownCollections.insert(c.id);
		CollectionsCache = list;
		SaveState();
		wg::Log("collections loaded: " + QString::number(list.size()));
		if (Dialog) {
			Dialog->setCollections(list);
			Dialog->setMonitoringStatus(QString::fromUtf8("Список коллекций обновлён: ") + QString::number(list.size()), MonitorRunning);
		}
	});
}

} // namespace

void ToggleSettings(QWidget *parent) {
	if (Dialog) {
		Dialog->close();
		return;
	}
	StripParent = parent;
	Dialog = new wg::SettingsDialog(CurrentSettings(), CurrentBlacklist(), parent);
	Dialog->setAttribute(Qt::WA_DeleteOnClose);
	QObject::connect(Dialog.data(), &wg::SettingsDialog::settingsChanged, [](const wg::Settings &s) {
		CurrentSettings() = s;
		LiveFeed().setCapacity(s.feedCapacity);
		SaveState();
		RefreshStrip();
	});
	QObject::connect(Dialog.data(), &wg::SettingsDialog::blacklistChanged, [](const wg::Blacklist &b) {
		CurrentBlacklist() = b;
		SaveState();
		LiveFeed().applyBlacklist(b);
		RefreshStrip();
	});
	QObject::connect(Dialog.data(), &wg::SettingsDialog::monitoringToggled, [](bool run) {
		if (!run) {
			if (TgBridge().stopMonitoring) TgBridge().stopMonitoring();
			OnMonitorStatus(QString::fromUtf8("Опрос остановлен"), false);
			return;
		}
		if (!TgBridge().startMonitoring) {
			OnMonitorStatus(QString::fromUtf8("Нет связи с Telegram: модуль не подключён"), false);
			return;
		}
		EnsureStrip();
		TgBridge().startMonitoring([] { return CurrentSettings(); });
	});
	QObject::connect(Dialog.data(), &wg::SettingsDialog::refreshCollectionsRequested, [] { LoadCollections(); });
	QObject::connect(Dialog.data(), &wg::SettingsDialog::suggestionSubmitted, [](const QString &text) {
		if (!License) {
			if (Dialog) Dialog->setSuggestionResult(false, QString::fromUtf8("Ключ не подключён"));
			return;
		}
		License->sendSuggestion(text, [](bool ok, QString message) {
			if (Dialog) Dialog->setSuggestionResult(ok, message);
		});
	});
	Dialog->setCollections(CollectionsCache);
	Dialog->setKeyInfo(MaskedKey(), License ? License->info().daysLeft : 0, QSysInfo::prettyProductName());
	Dialog->setMonitoringStatus(MonitorText.isEmpty() ? QString::fromUtf8("Опрос не запущен") : MonitorText, MonitorRunning);
	Dialog->show();
	Dialog->raise();
	Dialog->activateWindow();
	if (CollectionsCache.empty()) LoadCollections();
}

void StartLicense() {
	wg::Log("StartLicense");
	QSettings store("WorkGram", "WorkGram");
	License = std::make_unique<wg::LicenseClient>(ServerConfig());
	License->setKey(store.value("key").toString());
	License->setProxyUrl(store.value("proxy").toString());
	License->setCachedToken(store.value("token").toString());   // без сети можно работать до срока прошлого токена
	QObject::connect(License.get(), &wg::LicenseClient::changed, License.get(), [](const wg::LicenseInfo &info) {
		OnLicenseChanged(info);
	});
	if (License->key().isEmpty()) {
		wg::Log("no saved key: show key window");
		ShowKeyDialog();
		return;
	}
	if (License->allowed()) {
		BeginWork();                       // токен с прошлого раза ещё годен
	}
	License->check();                      // свежая проверка в любом случае
	if (!Started) {
		QTimer::singleShot(10000, [] {     // если сервер не ответил, не оставляем человека без окна
			if (!Started && !KeyWindow) ShowKeyDialog();
		});
	}
}

bool LicenseStarted() {
	return Started;
}

bool ExitRequested() {
	return ExitAsked;
}

void SetBridge(Bridge bridge) {
	TgBridge() = std::move(bridge);
}

void OnMonitorStatus(const QString &text, bool running) {
	MonitorText = text;
	MonitorRunning = running;
	if (Dialog) Dialog->setMonitoringStatus(text, running);
	RefreshStrip();
}

void OnGiftsFound(std::vector<wg::FeedItem> items, bool baseline) {
	const auto &settings = CurrentSettings();
	const auto &blacklist = CurrentBlacklist();
	if (baseline) std::reverse(items.begin(), items.end());      // старые первыми, чтобы самые новые оказались сверху
	int fresh = 0;
	for (auto &item : items) {
		if (!LiveFeed().add(item, settings, blacklist)) continue;
		if (baseline) LiveFeed().markSeen(item.id, item.kind);
		else fresh++;
	}
	if (fresh > 0 && settings.soundOnNew) QApplication::beep();
	RefreshStrip();
}

} // namespace WorkGram

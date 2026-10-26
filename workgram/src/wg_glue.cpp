#include "wg_glue.h"

#include <QCoreApplication>
#include <cstdlib>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QSysInfo>
#include <QTimer>
#include <memory>

#include "wg_license.h"
#include "wg_log.h"
#include "wg_widgets.h"

namespace WorkGram {
namespace {

wg::Settings &CurrentSettings() {
	static wg::Settings settings = [] {
		wg::Settings s;
		s.enabledCollections = {0, 1, 2};
		return s;
	}();
	return settings;
}

wg::Blacklist &CurrentBlacklist() {
	static wg::Blacklist blacklist;
	return blacklist;
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

// Пока тестовые данные: настоящий список коллекций подключим следующим шагом.
std::vector<wg::Collection> DemoCollections() {
	static const char *names[] = {"Chill Flame", "Vice Cream", "Liberty Figure", "Snoop Dogg", "Instant Ramen",
		"Ice Cream", "Lol Pop", "Pool Float", "Mirage Lamp", "Mood Pack", "Happy Brownie", "Whip Cupcake"};
	std::vector<wg::Collection> list;
	for (int i = 0; i < 12; i++) list.push_back({i, names[i], 40 + i * 17, 0});
	return list;
}

} // namespace

void ToggleSettings(QWidget *parent) {
	if (Dialog) {
		Dialog->close();
		return;
	}
	Dialog = new wg::SettingsDialog(CurrentSettings(), CurrentBlacklist(), parent);
	Dialog->setAttribute(Qt::WA_DeleteOnClose);
	QObject::connect(Dialog.data(), &wg::SettingsDialog::settingsChanged, [](const wg::Settings &s) {
		CurrentSettings() = s;
	});
	QObject::connect(Dialog.data(), &wg::SettingsDialog::blacklistChanged, [](const wg::Blacklist &b) {
		CurrentBlacklist() = b;
	});
	Dialog->setCollections(DemoCollections());
	Dialog->setKeyInfo(MaskedKey(), License ? License->info().daysLeft : 0, QSysInfo::prettyProductName());
	Dialog->setMonitoringStatus(QString::fromUtf8("Тестовые данные: подключение к маркету ещё не сделано"), false);
	Dialog->show();
	Dialog->raise();
	Dialog->activateWindow();
}

void StartLicense() {
	wg::Log("StartLicense");
	QSettings store("WorkGram", "WorkGram");
	License = std::make_unique<wg::LicenseClient>(ServerConfig());
	License->setKey(store.value("key").toString());
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

} // namespace WorkGram

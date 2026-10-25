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

bool EnsureLicense() {
	QSettings store("WorkGram", "WorkGram");
	License = std::make_unique<wg::LicenseClient>(wg::LicenseClient::Config{kServer, kPublicKey, kVersion});
	License->setKey(store.value("key").toString());
	License->setCachedToken(store.value("token").toString());   // без сети можно работать до срока прошлого токена
	bool ok = false;
	if (!License->key().isEmpty()) {
		License->checkBlocking(8000);
		ok = License->allowed();
	}
	if (!ok) {
		wg::KeyDialog dialog(License.get());
		ok = dialog.exec() == QDialog::Accepted;
	}
	if (!ok) {
		return false;
	}
	store.setValue("key", License->key());
	store.setValue("token", License->info().token);

	// Дальше раз в 90 секунд: сервер видит, что человек в сети, и может отозвать ключ.
	QObject::connect(License.get(), &wg::LicenseClient::changed, [](const wg::LicenseInfo &info) {
		QSettings store("WorkGram", "WorkGram");
		if (info.status == wg::LicenseStatus::Ok) {
			store.setValue("token", info.token);
		}
		if (!License->allowed()) {
			QMessageBox::warning(nullptr, "WorkGram", wg::licenseStatusText(info.status)
				+ QString::fromUtf8(". Приложение будет закрыто."));
			QTimer::singleShot(3000, [] { std::exit(0); });
			QCoreApplication::quit();
		}
	});
	License->startPinging(90);
	return true;
}

} // namespace WorkGram

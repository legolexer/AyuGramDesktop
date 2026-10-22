// WorkGram: проверка ключа на сервере, подписанный токен, окно ввода ключа.
#pragma once

#include <QDialog>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QTimer>

class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;

namespace wg {

enum class LicenseStatus { Unknown, Ok, InvalidKey, DeviceMismatch, Expired, UpdateRequired, NetworkError, BadSignature };

struct UpdateInfo {
	QString latest, url, sha256, notes;
	bool available = false, required = false;
};

struct LicenseInfo {
	LicenseStatus status = LicenseStatus::Unknown;
	int daysLeft = 0;
	qint64 tokenExp = 0;        // до какого момента (unix) действует токен
	QString token;
	UpdateInfo update;
};

QString licenseStatusText(LicenseStatus s);                    // сообщение для пользователя
QString machineHwid();                                          // хэш идентификатора компьютера
// Проверяет подпись Ed25519, срок и привязку к устройству. Возвращает true, если токен годен.
bool verifyToken(const QString &token, const QString &publicKeyHex, const QString &hwid, qint64 now,
	int *daysLeft = nullptr, qint64 *exp = nullptr);

class LicenseClient : public QObject {
	Q_OBJECT
public:
	struct Config {
		QString baseUrl, publicKeyHex, appVersion;
	};
	explicit LicenseClient(Config config, QObject *parent = nullptr);

	void setKey(const QString &key) { _key = key.trimmed().toUpper(); }
	const QString &key() const { return _key; }
	void setHwidOverride(const QString &hwid) { _hwid = hwid; }   // для тестов
	const QString &hwid() const { return _hwid; }
	void setIdentity(qint64 tgId, const QString &username) { _tgId = tgId; _username = username; }
	void setCachedToken(const QString &token);    // токен с прошлого запуска: даёт работать без сети до его срока

	void check();                                  // один запрос на сервер
	bool checkBlocking(int timeoutMs);             // то же, но ждёт ответа
	void startPinging(int seconds = 90);

	const LicenseInfo &info() const { return _info; }
	bool allowed() const;                          // можно ли сейчас работать

signals:
	void changed(const wg::LicenseInfo &info);

private:
	void finish(LicenseStatus status);

	Config _config;
	QString _key, _hwid, _username;
	qint64 _tgId = 0;
	LicenseInfo _info;
	QNetworkAccessManager *_net = nullptr;
	QTimer _timer;
	bool _busy = false;
};

// Окно «Введите ключ». Закрывается успешно (Accepted), когда сервер подтвердил ключ.
class KeyDialog : public QDialog {
	Q_OBJECT
public:
	explicit KeyDialog(LicenseClient *client, QWidget *parent = nullptr);
private:
	LicenseClient *_client = nullptr;
	QLineEdit *_edit = nullptr;
	QLabel *_status = nullptr;
	QPushButton *_login = nullptr;
};

} // namespace wg

Q_DECLARE_METATYPE(wg::LicenseInfo)

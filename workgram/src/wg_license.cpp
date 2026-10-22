#include "wg_license.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSysInfo>
#include <QVBoxLayout>

#include <openssl/evp.h>

namespace wg {
namespace {

QString S(const char *u) { return QString::fromUtf8(u); }

QByteArray FromBase64Url(const QString &s) {
	return QByteArray::fromBase64(s.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

bool VerifyEd25519(const QByteArray &publicKey, const QByteArray &message, const QByteArray &signature) {
	if (publicKey.size() != 32 || signature.size() != 64) return false;
	EVP_PKEY *key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
		reinterpret_cast<const unsigned char *>(publicKey.constData()), size_t(publicKey.size()));
	if (!key) return false;
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	bool ok = ctx && EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1
		&& EVP_DigestVerify(ctx, reinterpret_cast<const unsigned char *>(signature.constData()), size_t(signature.size()),
			reinterpret_cast<const unsigned char *>(message.constData()), size_t(message.size())) == 1;
	if (ctx) EVP_MD_CTX_free(ctx);
	EVP_PKEY_free(key);
	return ok;
}

LicenseStatus StatusFromServer(const QString &s) {
	if (s == "ok") return LicenseStatus::Ok;
	if (s == "invalid_key") return LicenseStatus::InvalidKey;
	if (s == "device_mismatch") return LicenseStatus::DeviceMismatch;
	if (s == "expired") return LicenseStatus::Expired;
	if (s == "update_required") return LicenseStatus::UpdateRequired;
	return LicenseStatus::NetworkError;
}

} // namespace

QString licenseStatusText(LicenseStatus s) {
	switch (s) {
	case LicenseStatus::Ok: return S("Ключ подтверждён");
	case LicenseStatus::InvalidKey: return S("Ключ не найден или отозван");
	case LicenseStatus::DeviceMismatch: return S("Этот ключ уже привязан к другому компьютеру");
	case LicenseStatus::Expired: return S("Срок действия ключа истёк");
	case LicenseStatus::UpdateRequired: return S("Эта версия больше не поддерживается, нужно обновить приложение");
	case LicenseStatus::NetworkError: return S("Нет связи с сервером ключей. Проверьте интернет и попробуйте снова");
	case LicenseStatus::BadSignature: return S("Ответ сервера не прошёл проверку подписи");
	case LicenseStatus::Unknown: break;
	}
	return QString();
}

QString machineHwid() {
	QByteArray id = QSysInfo::machineUniqueId();
	if (id.isEmpty()) id = (QSysInfo::machineHostName() + QSysInfo::productType()).toUtf8();
	return QString::fromLatin1(QCryptographicHash::hash("workgram:" + id, QCryptographicHash::Sha256).toHex());
}

bool verifyToken(const QString &token, const QString &publicKeyHex, const QString &hwid, qint64 now,
		int *daysLeft, qint64 *exp) {
	const auto parts = token.split('.');
	if (parts.size() != 2) return false;
	const QByteArray signature = FromBase64Url(parts[1]);
	const QByteArray publicKey = QByteArray::fromHex(publicKeyHex.toLatin1());
	if (!VerifyEd25519(publicKey, parts[0].toLatin1(), signature)) return false;   // подписывается строка payload в base64url
	const auto payload = QJsonDocument::fromJson(FromBase64Url(parts[0])).object();
	const QString expected = QString::fromLatin1(QCryptographicHash::hash(hwid.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
	if (payload.value("h").toString() != expected) return false;                    // токен выдан другому компьютеру
	const qint64 tokenExp = qint64(payload.value("exp").toDouble());
	if (tokenExp <= now) return false;
	if (daysLeft) *daysLeft = payload.value("dl").toInt();
	if (exp) *exp = tokenExp;
	return true;
}

// ---------------- LicenseClient ----------------
LicenseClient::LicenseClient(Config config, QObject *parent)
	: QObject(parent), _config(std::move(config)), _hwid(machineHwid()), _net(new QNetworkAccessManager(this)) {
	connect(&_timer, &QTimer::timeout, this, &LicenseClient::check);
}

void LicenseClient::setCachedToken(const QString &token) {
	int days = 0;
	qint64 exp = 0;
	if (token.isEmpty() || !verifyToken(token, _config.publicKeyHex, _hwid, QDateTime::currentSecsSinceEpoch(), &days, &exp)) return;
	_info.token = token;
	_info.tokenExp = exp;
	_info.daysLeft = days;
}

bool LicenseClient::allowed() const {
	const bool statusOk = _info.status == LicenseStatus::Ok || _info.status == LicenseStatus::NetworkError
		|| _info.status == LicenseStatus::Unknown;
	return statusOk && _info.tokenExp > QDateTime::currentSecsSinceEpoch();
}

void LicenseClient::startPinging(int seconds) {
	_timer.start(seconds * 1000);
}

void LicenseClient::finish(LicenseStatus status) {
	_busy = false;
	_info.status = status;
	if (status != LicenseStatus::Ok && status != LicenseStatus::NetworkError) {
		_info.tokenExp = 0;               // сервер прямо отказал: права на работу нет
		_info.token.clear();
	}
	emit changed(_info);
}

void LicenseClient::check() {
	if (_busy) return;
	_busy = true;
	QJsonObject body{{"key", _key}, {"hwid", _hwid}, {"version", _config.appVersion},
		{"device", QSysInfo::prettyProductName().left(80)}};
	if (_tgId) body.insert("tg_id", double(_tgId));
	if (!_username.isEmpty()) body.insert("tg_username", _username);

	QNetworkRequest request{QUrl(_config.baseUrl + "/api/session")};
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setTransferTimeout(10000);
	QNetworkReply *reply = _net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			finish(LicenseStatus::NetworkError);
			return;
		}
		const auto json = QJsonDocument::fromJson(reply->readAll()).object();
		const auto status = StatusFromServer(json.value("status").toString());
		const auto upd = json.value("update").toObject();
		_info.update = {upd.value("latest").toString(), upd.value("url").toString(), upd.value("sha256").toString(),
			upd.value("notes").toString(), upd.value("available").toBool(), upd.value("required").toBool()};
		if (status != LicenseStatus::Ok) {
			finish(status);
			return;
		}
		const QString token = json.value("token").toString();
		int days = 0;
		qint64 exp = 0;
		if (!verifyToken(token, _config.publicKeyHex, _hwid, QDateTime::currentSecsSinceEpoch(), &days, &exp)) {
			finish(LicenseStatus::BadSignature);
			return;
		}
		_info.token = token;
		_info.tokenExp = exp;
		_info.daysLeft = days;
		finish(LicenseStatus::Ok);
	});
}

bool LicenseClient::checkBlocking(int timeoutMs) {
	QEventLoop loop;
	QTimer guard;
	guard.setSingleShot(true);
	connect(this, &LicenseClient::changed, &loop, &QEventLoop::quit);
	connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
	guard.start(timeoutMs);
	check();
	loop.exec();
	return allowed();
}

// ---------------- KeyDialog ----------------
KeyDialog::KeyDialog(LicenseClient *client, QWidget *parent) : QDialog(parent), _client(client) {
	setWindowTitle("WorkGram");
	setModal(true);
	setFixedSize(440, 260);
	setStyleSheet("QDialog{background:#17212b;} QLabel{color:#fff;font-size:14px;}"
		"QLineEdit{background:#242f3d;color:#fff;border:0;border-radius:8px;padding:8px 12px;font-size:15px;}"
		"QPushButton{background:#2b5278;color:#fff;border:0;border-radius:8px;padding:8px 18px;font-size:14px;}"
		"QPushButton:disabled{background:#33414f;color:#8a98a8;}");
	auto *l = new QVBoxLayout(this);
	l->setContentsMargins(28, 24, 28, 24);
	l->setSpacing(12);
	auto *title = new QLabel("WorkGram");
	title->setStyleSheet("font-size:20px;font-weight:600;color:#fff;");
	l->addWidget(title);
	l->addWidget(new QLabel(S("Введите ключ, чтобы пользоваться приложением")));
	_edit = new QLineEdit;
	_edit->setPlaceholderText("WG-XXXX-XXXX-XXXX-XXXX");
	_edit->setText(client->key());
	l->addWidget(_edit);
	_status = new QLabel;
	_status->setWordWrap(true);
	_status->setStyleSheet("color:#f0a0a0;font-size:13px;");
	l->addWidget(_status);
	l->addStretch(1);
	auto *row = new QHBoxLayout;
	auto *quit = new QPushButton(S("Выйти"));
	_login = new QPushButton(S("Войти"));
	_login->setDefault(true);
	row->addWidget(quit);
	row->addStretch(1);
	row->addWidget(_login);
	l->addLayout(row);

	connect(quit, &QPushButton::clicked, this, &QDialog::reject);
	connect(_edit, &QLineEdit::returnPressed, _login, &QPushButton::click);
	connect(_login, &QPushButton::clicked, this, [this] {
		if (_edit->text().trimmed().isEmpty()) {
			_status->setText(S("Впишите ключ."));
			return;
		}
		_login->setEnabled(false);
		_status->setStyleSheet("color:#aab6c3;font-size:13px;");
		_status->setText(S("Проверяю…"));
		_client->setKey(_edit->text());
		_client->check();
	});
	connect(_client, &LicenseClient::changed, this, [this](const LicenseInfo &info) {
		_login->setEnabled(true);
		if (_client->allowed() && info.status == LicenseStatus::Ok) {
			accept();
			return;
		}
		_status->setStyleSheet("color:#f0a0a0;font-size:13px;");
		QString text = licenseStatusText(info.status);
		if (info.status == LicenseStatus::UpdateRequired && !info.update.latest.isEmpty()) {
			text += S(". Новая версия: ") + info.update.latest;
		}
		_status->setText(text);
	});
}

} // namespace wg

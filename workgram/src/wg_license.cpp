#include "wg_license.h"

#include "wg_log.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QSslSocket>
#include <QtNetwork/QNetworkRequest>
#include <QPushButton>
#include <vector>
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


// Разбор строки прокси. Принимает socks5://host:port, http://host:port или просто host:port (тогда socks5).
bool ParseProxy(const QString &text, QNetworkProxy *out) {
	QString t = text.trimmed();
	if (t.isEmpty()) return false;
	if (!t.contains("://")) t = "socks5://" + t;
	const QUrl url(t);
	if (!url.isValid() || url.host().isEmpty() || url.port() <= 0) return false;
	const bool http = url.scheme().toLower().startsWith("http");
	*out = QNetworkProxy(http ? QNetworkProxy::HttpProxy : QNetworkProxy::Socks5Proxy, url.host(),
		quint16(url.port()), url.userName(), url.password());
	return true;
}

// Конкретная причина вместо общей фразы «нет связи».
QString DescribeNetworkError(QNetworkReply *r) {
	const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (!QSslSocket::supportsSsl()) {
		return S("В этой сборке нет поддержки защищённых соединений (TLS), HTTPS недоступен");
	}
	switch (r->error()) {
	case QNetworkReply::HostNotFoundError:
		return S("Не найден адрес сервера (DNS). Проверьте интернет или VPN");
	case QNetworkReply::ConnectionRefusedError:
		return S("Сервер отклонил соединение. Возможно, сервер ключей не запущен");
	case QNetworkReply::RemoteHostClosedError:
		return S("Сервер оборвал соединение. Возможно, неверно настроен HTTPS или прокси на стороне сервера");
	case QNetworkReply::TimeoutError:
	case QNetworkReply::OperationCanceledError:
		return S("Сервер не ответил за 10 секунд. Часто это значит, что адрес заблокирован провайдером или нужен VPN/прокси");
	case QNetworkReply::SslHandshakeFailedError:
		return S("Не удалось установить защищённое соединение (TLS): сертификат сервера или перехват трафика");
	case QNetworkReply::ProxyConnectionRefusedError:
	case QNetworkReply::ProxyConnectionClosedError:
	case QNetworkReply::ProxyNotFoundError:
	case QNetworkReply::ProxyTimeoutError:
	case QNetworkReply::ProxyAuthenticationRequiredError:
	case QNetworkReply::UnknownProxyError:
		return S("Ошибка прокси: ") + r->errorString();
	case QNetworkReply::ContentNotFoundError:
		return S("Сервер ключей не найден по этому адресу (404)");
	default:
		break;
	}
	if (http >= 500) return S("Сервер ключей временно недоступен (код ") + QString::number(http) + ")";
	if (http >= 400) return S("Сервер отклонил запрос (код ") + QString::number(http) + ")";
	return S("Ошибка сети: ") + r->errorString();
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
	static bool logged = false;
	if (!logged) {
		logged = true;
		Log(QString("license: TLS supported: %1, library: %2").arg(QSslSocket::supportsSsl() ? "yes" : "NO", QSslSocket::sslLibraryVersionString()));
	}
	QJsonObject body{{"key", _key}, {"hwid", _hwid}, {"version", _config.appVersion},
		{"device", QSysInfo::prettyProductName().left(80)}};
	if (_tgId) body.insert("tg_id", double(_tgId));
	if (!_username.isEmpty()) body.insert("tg_username", _username);
	// Способы связи по очереди: прокси из окна ключа, настройки по умолчанию, системный прокси, напрямую.
	send(QJsonDocument(body).toJson(QJsonDocument::Compact), 0, 4, _lastGood, QString());
}

void LicenseClient::send(const QByteArray &body, int attempt, int total, int start, const QString &firstReason) {
	const int index = (start + attempt) % total;
	QNetworkProxy proxy(QNetworkProxy::NoProxy);
	bool usable = true;
	if (index == 0) {
		usable = ParseProxy(_proxyUrl, &proxy);
	} else if (index == 1) {
		proxy = QNetworkProxy(QNetworkProxy::DefaultProxy);
	} else if (index == 2) {
		usable = false;
		const auto list = QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(QUrl(_config.baseUrl)));
		for (const auto &p : list) {
			if (p.type() != QNetworkProxy::NoProxy) { proxy = p; usable = true; break; }
		}
	}
	if (!usable) {                                  // этот способ недоступен (прокси не задан): идём дальше
		if (attempt + 1 < total) { send(body, attempt + 1, total, start, firstReason); return; }
		_info.message = firstReason; finish(LicenseStatus::NetworkError); return;
	}
	_net->setProxy(proxy);
	QNetworkRequest request{QUrl(_config.baseUrl + "/api/session")};
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setTransferTimeout(10000);
	QNetworkReply *reply = _net->post(request, body);
	connect(reply, &QNetworkReply::finished, this, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			const QString reason = DescribeNetworkError(reply);
			Log(QString("license: way %1 failed: %2 | %3").arg(index).arg(reason, reply->errorString()));
			const QString first = firstReason.isEmpty() ? reason : firstReason;
			if (attempt + 1 < total) { send(body, attempt + 1, total, start, first); return; }
			_info.message = first;
			_info.detail = reply->errorString();
			finish(LicenseStatus::NetworkError);
			return;
		}
		const auto doc = QJsonDocument::fromJson(reply->readAll());
		if (!doc.isObject() || !doc.object().contains("status")) {
			Log("license: unexpected answer (not a key server)");
			_info.message = S("Сервер вернул не тот ответ (возможно, по этому адресу работает не сервер ключей)");
			finish(LicenseStatus::NetworkError);
			return;
		}
		const auto json = doc.object();
		_lastGood = index;
		const auto status = StatusFromServer(json.value("status").toString());
		const auto upd = json.value("update").toObject();
		_info.update = {upd.value("latest").toString(), upd.value("url").toString(), upd.value("sha256").toString(),
			upd.value("notes").toString(), upd.value("available").toBool(), upd.value("required").toBool()};
		_info.message.clear();
		if (status != LicenseStatus::Ok) {
			finish(status);
			return;
		}
		const QString token = json.value("token").toString();
		int days = 0;
		qint64 exp = 0;
		if (!verifyToken(token, _config.publicKeyHex, _hwid, QDateTime::currentSecsSinceEpoch(), &days, &exp)) {
			Log("license: bad signature");
			finish(LicenseStatus::BadSignature);
			return;
		}
		_info.token = token;
		_info.tokenExp = exp;
		_info.daysLeft = days;
		Log(QString("license: ok via way %1").arg(index));
		finish(LicenseStatus::Ok);
	});
}

void LicenseClient::sendSuggestion(const QString &text, std::function<void(bool, QString)> done) {
	const QJsonObject body{{"key", _key}, {"hwid", _hwid}, {"text", text.left(1000)}};
	postSuggestion(QJsonDocument(body).toJson(QJsonDocument::Compact), 0, std::move(done));
}

void LicenseClient::postSuggestion(const QByteArray &body, int attempt, std::function<void(bool, QString)> done) {
	const int total = 4;
	const int index = (_lastGood + attempt) % total;
	QNetworkProxy proxy(QNetworkProxy::NoProxy);
	bool usable = true;
	if (index == 0) {
		usable = ParseProxy(_proxyUrl, &proxy);
	} else if (index == 1) {
		proxy = QNetworkProxy(QNetworkProxy::DefaultProxy);
	} else if (index == 2) {
		usable = false;
		for (const auto &p : QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(QUrl(_config.baseUrl)))) {
			if (p.type() != QNetworkProxy::NoProxy) { proxy = p; usable = true; break; }
		}
	}
	if (!usable) {
		if (attempt + 1 < total) postSuggestion(body, attempt + 1, std::move(done));
		else done(false, S("Не удалось связаться с сервером"));
		return;
	}
	_net->setProxy(proxy);
	QNetworkRequest request{QUrl(_config.baseUrl + "/api/suggest")};
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setTransferTimeout(10000);
	QNetworkReply *reply = _net->post(request, body);
	connect(reply, &QNetworkReply::finished, this, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			if (attempt + 1 < total) { postSuggestion(body, attempt + 1, done); return; }
			done(false, DescribeNetworkError(reply));
			return;
		}
		const auto status = QJsonDocument::fromJson(reply->readAll()).object().value("status").toString();
		if (status == "ok") done(true, S("Отправлено, спасибо"));
		else if (status == "too_many") done(false, S("Сегодня уже отправлено максимум предложений, попробуйте завтра"));
		else if (status == "invalid_key") done(false, S("Ключ не подошёл, предложение не отправлено"));
		else if (status == "empty") done(false, S("Сначала напишите текст"));
		else done(false, S("Сервер ответил неожиданно, предложение не отправлено"));
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
	setWindowFlag(Qt::WindowStaysOnTopHint, true);
	setFixedSize(440, 330);
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
	_proxy = new QLineEdit;
	_proxy->setPlaceholderText(S("Прокси, если нужен (например socks5://127.0.0.1:1443)"));
	_proxy->setText(client->proxyUrl());
	l->addWidget(_proxy);
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
		_client->setProxyUrl(_proxy->text());
		_client->check();
	});
	connect(_client, &LicenseClient::changed, this, [this](const LicenseInfo &info) {
		_login->setEnabled(true);
		if (_client->allowed() && info.status == LicenseStatus::Ok) {
			accept();
			return;
		}
		_status->setStyleSheet("color:#f0a0a0;font-size:13px;");
		QString text = (info.status == LicenseStatus::NetworkError && !info.message.isEmpty())
			? info.message : licenseStatusText(info.status);
		if (info.status == LicenseStatus::UpdateRequired && !info.update.latest.isEmpty()) {
			text += S(". Новая версия: ") + info.update.latest;
		}
		_status->setText(text);
	});
}

} // namespace wg

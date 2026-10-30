// WorkGram: адаптер к клиенту Telegram Desktop.
// Работает только на чтение маркета и на открытие чата: ничего не покупает, не отправляет и не меняет в аккаунте.
#include "workgram/wg_tg.h"

#include "workgram/wg_bridge.h"
#include "workgram/wg_log.h"

#include "api/api_premium.h"
#include "apiwrap.h"
#include "core/application.h"
#include "data/data_drafts.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_user.h"
#include "history/history.h"
#include "main/main_session.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QDateTime>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>

#include <algorithm>
#include <limits>
#include <set>

namespace WorkGram::Tg {
namespace {

constexpr auto kRequestTimeoutMs = 15000;   // запрос завис: пропускаем и идём к следующей коллекции
constexpr int kScrollEnd = std::numeric_limits<int>::max() / 256;   // то же, что QFIXED_MAX в Qt: прокрутка поля ввода в конец

QString T(const char *utf8) {
	return QString::fromUtf8(utf8);
}

struct Monitor {
	QTimer *timer = nullptr;
	rpl::lifetime loading;
	bool busy = false;
	QElapsedTimer busySince;
	size_t next = 0;
	std::function<wg::Settings()> settings;
	std::set<int64_t> baselineDone;
	bool running = false;
};

Monitor &Mon() {
	static Monitor monitor;
	return monitor;
}

// ---------- список коллекций ----------
void RequestCollections(std::function<void(std::vector<wg::Collection>, QString)> done) {
	const auto session = Core::App().maybePrimarySession();
	if (!session) {
		done({}, T("Нет открытого аккаунта Telegram. Войдите в аккаунт и обновите список"));
		return;
	}
	session->api().request(MTPpayments_GetStarGifts(
		MTP_int(0)
	)).done([=](const MTPpayments_StarGifts &result) {
		result.match([&](const MTPDpayments_starGifts &data) {
			session->data().processUsers(data.vusers());
			session->data().processChats(data.vchats());
			std::vector<wg::Collection> list;
			for (const auto &gift : data.vgifts().v) {
				const auto parsed = Api::FromTL(session, gift);
				if (!parsed || parsed->unique) {
					continue;
				}
				wg::Collection collection;
				collection.id = int64_t(parsed->id);
				collection.name = parsed->resellTitle.isEmpty()
					? std::to_string(parsed->id)
					: parsed->resellTitle.toStdString();
				collection.forSale = parsed->resellCount;
				list.push_back(std::move(collection));
			}
			std::stable_sort(list.begin(), list.end(), [](const wg::Collection &a, const wg::Collection &b) {
				return a.forSale > b.forSale;
			});
			wg::Log(QString("tg: catalog received, collections: %1").arg(list.size()));
			done(std::move(list), QString());
		}, [&](const MTPDpayments_starGiftsNotModified &) {
			done({}, T("Telegram ответил «без изменений», но список не прислал"));
		});
	}).fail([=](const MTP::Error &error) {
		wg::Log("tg: catalog error: " + error.type());
		done({}, T("Telegram вернул ошибку при загрузке списка: ") + error.type());
	}).send();
}

// ---------- опрос маркета ----------
void PollNext() {
	auto &monitor = Mon();
	if (!monitor.running) {
		return;
	}
	const auto session = Core::App().maybePrimarySession();
	if (!session) {
		OnMonitorStatus(T("Нет открытого аккаунта Telegram: войдите в аккаунт, опрос продолжится сам"), true);
		return;
	}
	if (monitor.busy) {
		if (monitor.busySince.elapsed() < kRequestTimeoutMs) {
			return;
		}
		wg::Log("tg: request timed out, skipping");
		monitor.loading.destroy();
		monitor.busy = false;
	}
	const auto settings = monitor.settings();
	if (monitor.timer) {
		monitor.timer->setInterval(int(std::max(1.0, settings.pollSeconds) * 1000));
	}
	const auto ids = std::vector<int64_t>(
		settings.enabledCollections.begin(),
		settings.enabledCollections.end());
	if (ids.empty()) {
		OnMonitorStatus(T("Не выбрано ни одной коллекции: отметьте их на вкладке «Коллекции»"), true);
		return;
	}
	const auto giftId = ids[monitor.next++ % ids.size()];
	monitor.busy = true;
	monitor.busySince.start();

	auto filter = Data::ResaleGiftsFilter();
	filter.sort = Data::ResaleGiftsSort::Date;      // новые лоты первыми
	monitor.loading = Data::ResaleGiftsSlice(
		session,
		uint64(giftId),
		std::move(filter)
	) | rpl::on_next([=](Data::ResaleGiftsDescriptor &&slice) {
		auto &mon = Mon();
		mon.busy = false;
		std::vector<wg::FeedItem> items;
		int withId = 0, nameOnly = 0, addressOnly = 0, nothing = 0;
		for (const auto &gift : slice.list) {
			const auto &unique = gift.unique;
			if (!unique) {
				continue;
			}
			wg::FeedItem item;
			item.id = int64_t(unique->id);
			item.slug = unique->slug.toStdString();
			item.collectionId = giftId;
			item.title = unique->title.toStdString();
			item.num = unique->number;
			item.sellerId = int64_t(unique->ownerId.value);
			item.ownerName = unique->ownerName.toStdString();
			if (unique->ownerId.value) {
				++withId;
				if (const auto peer = session->data().peerLoaded(unique->ownerId)) {
					item.sellerUsername = peer->username().toStdString();
				}
			} else if (!unique->ownerName.isEmpty()) {
				++nameOnly;
			} else if (!unique->ownerAddress.isEmpty()) {
				++addressOnly;
			} else {
				++nothing;
			}
			if (unique->starsForResale > 0) {
				item.priceStars = unique->starsForResale;
			}
			if (unique->nanoTonForResale > 0) {
				item.priceNanoTon = unique->nanoTonForResale;
			}
			item.kind = wg::Kind::Market;
			item.backdropCenter = unique->backdrop.centerColor.rgba();
			item.backdropEdge = unique->backdrop.edgeColor.rgba();
			items.push_back(std::move(item));
		}
		// Что Telegram реально отдаёт про владельцев (только счётчики, без имён и номеров).
		wg::Log(QString("tg: collection %1: lots %2, owner by id %3, name only %4, address only %5, no owner %6")
			.arg(giftId).arg(items.size()).arg(withId).arg(nameOnly).arg(addressOnly).arg(nothing));
		const bool baseline = !mon.baselineDone.count(giftId);
		mon.baselineDone.insert(giftId);
		OnGiftsFound(std::move(items), baseline);
		OnMonitorStatus(T("Работает. Последний ответ: ")
			+ QDateTime::currentDateTime().toString("HH:mm:ss"), true);
	});
}

void StartMonitoring(std::function<wg::Settings()> getSettings) {
	auto &monitor = Mon();
	monitor.settings = std::move(getSettings);
	monitor.running = true;
	monitor.next = 0;
	monitor.baselineDone.clear();
	if (!monitor.timer) {
		monitor.timer = new QTimer();
		QObject::connect(monitor.timer, &QTimer::timeout, [] { PollNext(); });
	}
	monitor.timer->start(int(std::max(1.0, monitor.settings().pollSeconds) * 1000));
	OnMonitorStatus(T("Опрос запущен"), true);
	PollNext();
}

void StopMonitoring() {
	auto &monitor = Mon();
	monitor.running = false;
	if (monitor.timer) {
		monitor.timer->stop();
	}
	monitor.loading.destroy();
	monitor.busy = false;
}

// ---------- чат с владельцем и карточка подарка ----------
QString OpenChatWithDraft(uint64_t peerId, const QString &text) {
	if (!peerId) {
		return T("Владелец скрыт: Telegram не отдаёт его номер, написать нельзя");
	}
	const auto window = Core::App().activeWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	if (!controller) {
		return T("Нет открытого окна Telegram");
	}
	const auto id = PeerId(peerId);
	const auto history = controller->session().data().history(id);
	auto draft = std::make_unique<Data::Draft>();
	draft->textWithTags.text = text;
	const auto end = int(text.size());
	draft->cursor = MessageCursor(end, end, kScrollEnd);
	history->setLocalDraft(std::move(draft));         // текст лежит в поле ввода, отправляет человек сам
	controller->showPeerHistory(id);
	return QString();
}

void OpenGift(const QString &slug) {
	Core::App().openLocalUrl(T("https://t.me/nft/") + slug, QVariant());
}

} // namespace

void Install() {
	Bridge bridge;
	bridge.requestCollections = RequestCollections;
	bridge.startMonitoring = StartMonitoring;
	bridge.stopMonitoring = StopMonitoring;
	bridge.openChatWithDraft = OpenChatWithDraft;
	bridge.openGift = OpenGift;
	SetBridge(std::move(bridge));
	wg::Log("tg: bridge installed");
}

} // namespace WorkGram::Tg

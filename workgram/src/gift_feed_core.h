// WorkGram: ядро ленты подарков. Только стандартный C++17, без Qt и без Telegram-классов,
// поэтому его можно проверить тестами отдельно, а потом подключить к клиенту.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <unordered_set>

namespace wg {

enum class Kind { Market, Upgrade };

struct FeedItem {
	int64_t id = 0;                       // уникальный id лота или подарка
	std::string slug;                     // PlushPepe-1204 (для ссылки t.me/nft/...)
	int64_t collectionId = 0;
	std::string title;                    // Plush Pepe
	int64_t num = 0;
	int64_t sellerId = 0;
	std::string sellerUsername;           // без @, может быть пустым
	std::optional<int64_t> priceStars;    // у апгрейдов цены нет
	Kind kind = Kind::Market;
	bool seen = false;                    // true после открытия карточки
	uint32_t backdropCenter = 0xFF2F7D4F; // цвета фона подарка (ARGB)
	uint32_t backdropEdge = 0xFF1C4A3A;
	int64_t addedAt = 0;
};

enum class BlackType { Seller = 0, Collection = 1, Item = 2 };

struct Collection {
	int64_t id = 0;
	std::string name;
	int forSale = 0;       // сколько лотов в продаже
	int newCount = 0;      // новых за последнее время
};

struct Blacklist {
	std::set<int64_t> sellers, collections, items;
	std::map<int64_t, std::string> labels;   // подписи для списка в настройках (ключ: id)
	void add(BlackType t, int64_t id, const std::string &label) {
		(t == BlackType::Seller ? sellers : t == BlackType::Collection ? collections : items).insert(id);
		labels[id] = label;
	}
	bool contains(const FeedItem &i) const {
		return sellers.count(i.sellerId) || collections.count(i.collectionId) || items.count(i.id);
	}
};

struct Settings {
	bool showMarket = true;
	bool showUpgrades = true;
	bool animateModels = true;
	bool soundOnNew = false;
	int soundVolume = 50;
	size_t feedCapacity = 25;             // всего в ленте
	int visibleCards = 4;                 // сколько видно без прокрутки
	double pollSeconds = 5.0;
	int limitPerCollection = 50;
	int maxMessagesPerHour = 20;
	std::set<int64_t> enabledCollections; // пусто = ничего не отслеживаем
	bool autoEnableNewCollections = false;
	std::vector<std::string> templates{"Привет! Интересует %gift% %link%"};
	int activeTemplate = 0;
	std::string feedColor = "#17212b";    // цвет ленты
	std::string menuColor = "#17212b";    // цвет меню F9
};

// Подпись под ценой: «Маркет», «Апгрейд», плюс «прочитано» у открытых.
inline std::string caption(const FeedItem &i) {
	std::string s = (i.kind == Kind::Market) ? "Маркет" : "Апгрейд";
	return i.seen ? s + " · прочитано" : s;
}

class Feed {
public:
	explicit Feed(size_t capacity = 25) : _capacity(capacity) {}

	// Возвращает true, если карточка добавлена (новая, разрешена настройками и не в чёрном списке).
	bool add(FeedItem item, const Settings &s, const Blacklist &bl) {
		if (!s.enabledCollections.count(item.collectionId)) return false;
		if (item.kind == Kind::Market && !s.showMarket) return false;
		if (item.kind == Kind::Upgrade && !s.showUpgrades) return false;
		if (bl.contains(item)) return false;
		if (!_known.insert(key(item)).second) return false;   // уже видели
		if (_known.size() > 5000) _known.clear();
		item.seen = false;
		_items.push_front(std::move(item));
		while (_items.size() > _capacity) _items.pop_back();  // самый старый пропадает
		return true;
	}
	// Карточка определяется парой «id + тип»: у маркета и апгрейда id может совпасть.
	void markSeen(int64_t id, Kind kind) {
		for (auto &i : _items) if (i.id == id && i.kind == kind) i.seen = true;
	}
	// После добавления в чёрный список карточки пропадают из ленты сразу.
	void applyBlacklist(const Blacklist &bl) {
		_items.erase(std::remove_if(_items.begin(), _items.end(),
			[&](const FeedItem &i) { return bl.contains(i); }), _items.end());
	}
	void setCapacity(size_t c) {
		_capacity = c;
		while (_items.size() > _capacity) _items.pop_back();
	}
	const std::deque<FeedItem> &items() const { return _items; }
	size_t unseenCount() const {
		return std::count_if(_items.begin(), _items.end(), [](const FeedItem &i) { return !i.seen; });
	}

private:
	static std::string key(const FeedItem &i) {
		return std::to_string(i.id) + (i.kind == Kind::Market ? "m" : "u");
	}
	size_t _capacity;
	std::deque<FeedItem> _items;
	std::unordered_set<std::string> _known;
};

// ---------- шаблон сообщения ----------
inline void replaceAll(std::string &s, const std::string &from, const std::string &to) {
	for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size()) s.replace(p, from.size(), to);
}
inline std::string giftLabel(const FeedItem &i) { return i.title + " #" + std::to_string(i.num); }
inline std::string giftLink(const FeedItem &i) { return "https://t.me/nft/" + i.slug; }
inline std::string renderTemplate(std::string t, const FeedItem &i) {
	replaceAll(t, "%gift%", giftLabel(i));
	replaceAll(t, "%link%", giftLink(i));
	return t;
}

// ---------- ответ @SpamBot ----------
struct SpamStatus {
	enum class State { Unknown, Free, Restricted } state = State::Unknown;
	std::optional<int64_t> until;         // unix-время (UTC), когда ограничение снимут
};

inline int64_t daysFromCivil(int y, int m, int d) {
	y -= m <= 2;
	const int era = (y >= 0 ? y : y - 399) / 400;
	const int yoe = y - era * 400;
	const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return static_cast<int64_t>(era) * 146097 + doe - 719468;
}

inline SpamStatus parseSpamBotReply(const std::string &text) {
	SpamStatus r;
	// «свободен от каких-либо ограничений» содержит слово «ограничен», поэтому «свободен» проверяем первым.
	if (text.find("свободен от каких-либо ограничений") != std::string::npos
		|| text.find("no limits are currently applied") != std::string::npos) {
		r.state = SpamStatus::State::Free;
		return r;
	}
	if (text.find("ограничен") == std::string::npos && text.find("limited") == std::string::npos) return r;
	r.state = SpamStatus::State::Restricted;
	static const std::regex re(R"((\d{1,2}) (Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)[a-z]* (\d{4}), (\d{2}):(\d{2}) UTC)");
	std::smatch m;
	if (std::regex_search(text, m, re)) {
		static const std::string months = "JanFebMarAprMayJunJulAugSepOctNovDec";
		const int mon = static_cast<int>(months.find(m[2].str()) / 3) + 1;
		r.until = (daysFromCivil(std::stoi(m[3]), mon, std::stoi(m[1])) * 24 + std::stoi(m[4])) * 3600
			+ std::stoi(m[5]) * 60;
	}
	return r;
}

// Продавца можно открыть в чате по его id, даже если имя и профиль скрыты. Если Telegram не отдаёт
// владельца совсем (sellerId == 0), написать нельзя: кнопка неактивна, остаётся скопировать ссылку на подарок.
inline bool ownerReachable(const FeedItem &i) { return i.sellerId != 0; }

// ---------- когда и как можно писать ----------
enum class SendReason { Ok, PaidMessages, Restricted, RateLimit };

class SendGuard {
public:
	explicit SendGuard(int maxPerHour = 20) : _max(maxPerHour) {}

	// recipientPaidStars: сколько Stars получатель берёт за сообщение (0 или пусто = бесплатно).
	SendReason check(int64_t now, std::optional<int64_t> recipientPaidStars) {
		if (recipientPaidStars && *recipientPaidStars > 0) return SendReason::PaidMessages;  // никогда не платим
		if (_restrictedUntil && *_restrictedUntil > now) return SendReason::Restricted;
		while (!_sent.empty() && _sent.front() <= now - 3600) _sent.pop_front();
		if (static_cast<int>(_sent.size()) >= _max) return SendReason::RateLimit;
		return SendReason::Ok;
	}
	void recordSent(int64_t now) { _sent.push_back(now); }
	void setMax(int maxPerHour) { _max = maxPerHour; }
	void applySpamStatus(const SpamStatus &s) {
		_restrictedUntil = (s.state == SpamStatus::State::Restricted) ? s.until : std::nullopt;
	}
	std::optional<int64_t> restrictedUntil() const { return _restrictedUntil; }

	// Любая ошибка отправки запускает проверку через @SpamBot, но не чаще раза в 10 минут.
	bool shouldCheckSpamBot(int64_t now) {
		if (_lastCheck && now - *_lastCheck < 600) return false;
		_lastCheck = now;
		return true;
	}

private:
	int _max;
	std::deque<int64_t> _sent;
	std::optional<int64_t> _restrictedUntil, _lastCheck;
};

} // namespace wg

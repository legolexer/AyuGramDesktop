// WorkGram: мост между нашим интерфейсом (отдельная библиотека на Qt) и клиентом Telegram.
// Интерфейс ничего не знает про внутренности Telegram Desktop, адаптер (wg_tg.cpp) ничего не знает про окна.
#pragma once

#include <QString>
#include <cstdint>
#include <functional>
#include <vector>

#include "gift_feed_core.h"

namespace WorkGram {

struct Bridge {
	// Запросить у Telegram список коллекций. done(список, ошибка): при успехе ошибка пустая.
	std::function<void(std::function<void(std::vector<wg::Collection>, QString)>)> requestCollections;
	// Запустить опрос маркета. Настройки читаются заново на каждом шаге.
	std::function<void(std::function<wg::Settings()>)> startMonitoring;
	std::function<void()> stopMonitoring;
	// Открыть чат с владельцем и положить текст в поле ввода. Возвращает текст ошибки или пустую строку.
	std::function<QString(uint64_t peerId, const QString &text)> openChatWithDraft;
	// Открыть стандартную карточку подарка Telegram по его адресу в ссылке t.me/nft/.
	std::function<void(const QString &slug)> openGift;
};

void SetBridge(Bridge bridge);

// Адаптер сообщает интерфейсу:
void OnGiftsFound(std::vector<wg::FeedItem> items, bool baseline);   // baseline: первая выборка, это не «новые»
void OnMonitorStatus(const QString &text, bool running);

} // namespace WorkGram

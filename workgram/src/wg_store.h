// WorkGram: сохранение настроек и чёрного списка между запусками (JSON в разделе WorkGram реестра).
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "gift_feed_core.h"

namespace wg {

inline QJsonObject SettingsToJson(const Settings &s, const Blacklist &bl) {
	QJsonArray cols, tpls, sellers, collections, items;
	for (auto id : s.enabledCollections) cols.append(double(id));
	for (const auto &t : s.templates) tpls.append(QString::fromStdString(t));
	for (auto id : bl.sellers) sellers.append(double(id));
	for (auto id : bl.collections) collections.append(double(id));
	for (auto id : bl.items) items.append(double(id));
	QJsonObject labels;
	for (const auto &kv : bl.labels) labels.insert(QString::number(kv.first), QString::fromStdString(kv.second));
	return {{"showMarket", s.showMarket}, {"showUpgrades", s.showUpgrades}, {"animateModels", s.animateModels},
		{"soundOnNew", s.soundOnNew}, {"soundVolume", s.soundVolume}, {"feedCapacity", int(s.feedCapacity)},
		{"visibleCards", s.visibleCards}, {"pollSeconds", s.pollSeconds}, {"limitPerCollection", s.limitPerCollection},
		{"maxMessagesPerHour", s.maxMessagesPerHour}, {"enabledCollections", cols},
		{"autoEnableNewCollections", s.autoEnableNewCollections}, {"templates", tpls}, {"activeTemplate", s.activeTemplate},
		{"feedColor", QString::fromStdString(s.feedColor)}, {"menuColor", QString::fromStdString(s.menuColor)},
		{"blSellers", sellers}, {"blCollections", collections}, {"blItems", items}, {"blLabels", labels}};
}

inline void SettingsFromJson(const QJsonObject &o, Settings *s, Blacklist *bl) {
	auto b = [&](const char *k, bool d) { return o.contains(k) ? o.value(k).toBool(d) : d; };
	auto i = [&](const char *k, int d) { return o.contains(k) ? o.value(k).toInt(d) : d; };
	s->showMarket = b("showMarket", s->showMarket);
	s->showUpgrades = b("showUpgrades", s->showUpgrades);
	s->animateModels = b("animateModels", s->animateModels);
	s->soundOnNew = b("soundOnNew", s->soundOnNew);
	s->soundVolume = qBound(0, i("soundVolume", s->soundVolume), 100);
	s->feedCapacity = size_t(qBound(5, i("feedCapacity", int(s->feedCapacity)), 50));
	s->visibleCards = qBound(1, i("visibleCards", s->visibleCards), 15);
	s->pollSeconds = o.contains("pollSeconds") ? qBound(1.0, o.value("pollSeconds").toDouble(), 60.0) : s->pollSeconds;
	s->limitPerCollection = qBound(10, i("limitPerCollection", s->limitPerCollection), 200);
	s->maxMessagesPerHour = qBound(1, i("maxMessagesPerHour", s->maxMessagesPerHour), 60);
	s->autoEnableNewCollections = b("autoEnableNewCollections", s->autoEnableNewCollections);
	s->activeTemplate = i("activeTemplate", s->activeTemplate);
	if (o.contains("feedColor")) s->feedColor = o.value("feedColor").toString().toStdString();
	if (o.contains("menuColor")) s->menuColor = o.value("menuColor").toString().toStdString();
	if (o.contains("enabledCollections")) {
		s->enabledCollections.clear();
		for (const auto &v : o.value("enabledCollections").toArray()) s->enabledCollections.insert(int64_t(v.toDouble()));
	}
	if (o.contains("templates")) {
		s->templates.clear();
		for (const auto &v : o.value("templates").toArray()) s->templates.push_back(v.toString().toStdString());
	}
	if (s->templates.empty()) s->templates.push_back("Привет! Интересует %gift% %link%");
	if (s->activeTemplate < 0 || s->activeTemplate >= int(s->templates.size())) s->activeTemplate = 0;
	bl->sellers.clear(); bl->collections.clear(); bl->items.clear(); bl->labels.clear();
	for (const auto &v : o.value("blSellers").toArray()) bl->sellers.insert(int64_t(v.toDouble()));
	for (const auto &v : o.value("blCollections").toArray()) bl->collections.insert(int64_t(v.toDouble()));
	for (const auto &v : o.value("blItems").toArray()) bl->items.insert(int64_t(v.toDouble()));
	const auto labels = o.value("blLabels").toObject();
	for (auto it = labels.begin(); it != labels.end(); ++it) bl->labels[it.key().toLongLong()] = it.value().toString().toStdString();
}

} // namespace wg

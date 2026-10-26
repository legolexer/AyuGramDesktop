// WorkGram: простой журнал workgram.log рядом с приложением. Ключи и токены сюда не пишутся.
#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>

namespace wg {

inline void Log(const QString &line) {
	QFile f(QCoreApplication::applicationDirPath() + "/workgram.log");
	if (f.size() > 512 * 1024) f.remove();
	if (!f.open(QIODevice::Append)) return;
	f.write((QDateTime::currentDateTime().toString(Qt::ISODate) + " " + line + "\n").toUtf8());
}

} // namespace wg

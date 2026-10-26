// WorkGram: единственная точка входа из клиента Telegram Desktop в наш интерфейс.
#pragma once

class QWidget;

namespace WorkGram {

// F9: показать или скрыть окно настроек WorkGram.
void ToggleSettings(QWidget *parent);

// Запуск: проверяет ключ и, если нужно, показывает окно ввода. Ничего не блокирует; без ключа приложение закроется.
void StartLicense();
bool LicenseStarted();
bool ExitRequested();      // приложение попросило закрыться (ключ перестал действовать)

} // namespace WorkGram

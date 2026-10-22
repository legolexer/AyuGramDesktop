// WorkGram: единственная точка входа из клиента Telegram Desktop в наш интерфейс.
#pragma once

class QWidget;

namespace WorkGram {

// F9: показать или скрыть окно настроек WorkGram.
void ToggleSettings(QWidget *parent);

// Запуск: спрашивает ключ, если он не подтверждён. false = закрыть приложение.
bool EnsureLicense();

} // namespace WorkGram

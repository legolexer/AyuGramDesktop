"""Применяет WorkGram к исходникам AyuGramDesktop: копирует наши файлы и вносит две небольшие правки.
Запуск: python apply.py ПУТЬ_К_РЕПОЗИТОРИЮ AyuGramDesktop. Можно запускать повторно, ничего не задвоится."""
import pathlib
import shutil
import sys

# На Windows консоль по умолчанию в cp1252 и не умеет печатать русский текст: переключаем вывод на UTF-8.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

if len(sys.argv) != 2:
    sys.exit("Использование: python apply.py ПУТЬ_К_РЕПОЗИТОРИЮ")
repo = pathlib.Path(sys.argv[1]).resolve()
here = pathlib.Path(__file__).resolve().parent

dst = repo / "Telegram" / "SourceFiles" / "workgram"
dst.mkdir(parents=True, exist_ok=True)
for folder in ("src", "tg"):                      # src: наша библиотека, tg: адаптер к клиенту Telegram
    for f in sorted((here / folder).iterdir()):
        shutil.copy2(f, dst / f.name)
        print("скопирован:", f.name)


def patch(path, anchor, addition, marker, after=True):
    """Вставляет addition после (или перед) anchor. Учитывает окончания строк файла (LF или CRLF)."""
    with open(path, encoding="utf-8", newline="") as f:
        text = f.read()
    if marker in text:
        print("уже применено:", path.name)
        return
    nl = "\r\n" if "\r\n" in text else "\n"
    anchor_n, addition_n = anchor.replace("\n", nl), addition.replace("\n", nl)
    if text.count(anchor_n) != 1:
        sys.exit(f"Не найдено единственное место вставки в {path.name}: {anchor[:60]!r} "
                 f"(найдено {text.count(anchor_n)}). Исходники отличаются от ожидаемой версии.")
    new = anchor_n + addition_n if after else addition_n + anchor_n
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text.replace(anchor_n, new, 1))
    print("изменён:", path.name)


patch(repo / "Telegram" / "CMakeLists.txt",
      "set_target_properties(Telegram PROPERTIES AUTOMOC ON)\n",
      """
# WorkGram
add_library(workgram_ui STATIC
    ${src_loc}/workgram/wg_widgets.cpp
    ${src_loc}/workgram/wg_settings.cpp
    ${src_loc}/workgram/wg_license.cpp
    ${src_loc}/workgram/wg_glue.cpp
)
set_target_properties(workgram_ui PROPERTIES AUTOMOC ON)
target_compile_features(workgram_ui PUBLIC cxx_std_17)
if (MSVC)
    target_compile_options(workgram_ui PRIVATE /utf-8)
endif()
target_include_directories(workgram_ui PUBLIC ${src_loc})
target_link_libraries(workgram_ui PUBLIC desktop-app::external_qt desktop-app::external_openssl)
target_link_libraries(Telegram PRIVATE workgram_ui)
nice_target_sources(Telegram ${src_loc}
PRIVATE
    workgram/wg_tg.cpp
    workgram/wg_tg.h
)
""", "workgram_ui")

mw = repo / "Telegram" / "SourceFiles" / "mainwindow.cpp"
patch(mw, '#include "mainwindow.h"\n', '#include "workgram/wg_glue.h"\n', "workgram/wg_glue.h")
patch(mw, "\t} break;\n\n\tcase QEvent::MouseMove: {\n",
      """\t\t// WorkGram: F9
\t\tif (object == windowHandle()) {
\t\t\tconst auto event = static_cast<QKeyEvent*>(e);
\t\t\tif (event->key() == Qt::Key_F9
\t\t\t\t&& event->modifiers() == Qt::NoModifier
\t\t\t\t&& !event->isAutoRepeat()) {
\t\t\t\tWorkGram::ToggleSettings(this);
\t\t\t\treturn true;
\t\t\t}
\t\t}
""", "WorkGram: F9", after=False)

app = repo / "Telegram" / "SourceFiles" / "core" / "application.cpp"
patch(app, '#include "core/application.h"\n', '#include "workgram/wg_glue.h"\n#include "workgram/wg_tg.h"\n', "workgram/wg_glue.h")
patch(app, "void Application::run() {\n",
      """\t// WorkGram: проверка ключа. Ничего не блокирует: окно ключа появится после запуска, без ключа приложение закроется.
\tWorkGram::StartLicense();
\tWorkGram::Tg::Install();

""", "WorkGram::StartLicense")

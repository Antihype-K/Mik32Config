# Windows-упаковка (win64 portable zip + install.exe)

Кросс-компиляция и упаковка делаются на Linux, без реальной Windows-машины —
воспроизводимость проверена на этой сессии: MinGW-w64 GCC 13 (пакеты
`g++-mingw-w64-x86-64-posix` и зависимости) + Qt 6.7.0 для Windows (через
`aqtinstall`) + `wine` (нужен только на этапе сборки, как раннер для
`moc.exe`/`uic.exe`/`rcc.exe` — самому установщику и конечному пользователю
Wine не нужен).

## Зачем нужен `wine` при сборке

CMake AUTOMOC вызывает `moc` как обычную программу сборки. Qt6-пакет для
Windows поставляет `moc.exe` — PE-бинарник, который на Linux-хосте напрямую
не запустить. `CMakeLists.txt` (см. блок `if(CMAKE_CROSSCOMPILING AND WIN32)`)
детектирует это и подменяет `IMPORTED_LOCATION` инструментов `Qt6::moc/uic/rcc`
на обёртки, запускающие тот же `moc.exe`/`uic.exe`/`rcc.exe` через `wine` —
не другой инструмент, тот же самый PE-бинарник, только способ его исполнения.

**Важное ограничение, найденное на практике:** `moc.exe` под wine не может
записать выходной файл, если путь содержит кириллицу (`/home/user/Документы/...`)
— чтение исходников с кириллическим путём работает, а запись вывода — нет.
Поэтому каталог сборки (`-B` у `cmake`) должен быть в пути только из ASCII;
сам исходный каталог репозитория кириллицу содержать может.

## Инструменты (устанавливаются один раз, без sudo/root)

```bash
python3 -m venv ~/.local/mik32-winbuild-tools/venv
~/.local/mik32-winbuild-tools/venv/bin/pip install aqtinstall
source ~/.local/mik32-winbuild-tools/venv/bin/activate
aqt install-qt windows desktop 6.7.0 win64_mingw -O ~/.local/mik32-winbuild-tools/Qt

# MinGW: пакеты скачаны через apt-get download (без установки в систему) и
# распакованы вручную через dpkg-deb -x — apt install потребовал бы sudo.
mkdir -p ~/.local/mik32-winbuild-tools/mingw-local/{debs,root,bin}
cd ~/.local/mik32-winbuild-tools/mingw-local
apt-get download g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix \
    gcc-mingw-w64-x86-64-posix-runtime gcc-mingw-w64-base \
    binutils-mingw-w64-x86-64 binutils-common mingw-w64-x86-64-dev mingw-w64-common
mv *.deb debs/
for f in debs/*.deb; do dpkg-deb -x "$f" root; done

# NSIS — тоже через apt-get download + dpkg-deb -x.
mkdir -p ~/.local/mik32-winbuild-tools/nsis-local/{debs,root}
cd ~/.local/mik32-winbuild-tools/nsis-local
apt-get download nsis nsis-common
mv *.deb debs/
for f in debs/*.deb; do dpkg-deb -x "$f" root; done
```

`gcc-mingw-w64-*` пакеты из Ubuntu ставят `x86_64-w64-mingw32-g++`/`gcc` через
`update-alternatives`, который без `apt install` (только `dpkg-deb -x`) не
срабатывает — бинарники называются `x86_64-w64-mingw32-g++-posix`/`-gcc-posix`
и не находят свой префикс поиска (`cc1plus`, `crtbegin.o` и т.п.) без явного
`-B`. Поэтому `~/.local/mik32-winbuild-tools/mingw-local/bin/` содержит две
тонкие обёртки (`x86_64-w64-mingw32-gcc`/`g++`), которые вызывают `-posix`
бинарник с нужными `-B`-путями, плюс симлинки на `ar`/`windres`/`ranlib`/etc.
под привычными именами без суффикса `-posix` — `CMAKE_C_COMPILER`/
`CMAKE_CXX_COMPILER` в `mingw-toolchain.cmake` указывают именно на них.

## Сборка

```bash
cmake -S . -B /home/<ascii-only-path>/mik32-build-win64 \
    -DCMAKE_TOOLCHAIN_FILE=packaging/windows/mingw-toolchain.cmake \
    -DBUILD_TESTING=OFF -DBUILD_GUI=ON
cmake --build /home/<ascii-only-path>/mik32-build-win64 -j$(nproc)
```

Результат — `MIK32-Configurator.exe` (консоль) и `MIK32-Configurator-GUI.exe`
(GUI-подсистема, без консольного окна — см. `CMakeLists.txt`, `WIN32` у
`add_executable`).

## Упаковка (portable zip + install.exe)

```bash
packaging/windows/build_win64_package.sh /home/<ascii-only-path>/mik32-build-win64
```

Скрипт (1) собирает portable-каталог со всеми Qt/MinGW рантайм-DLL,
`knowledge_base/`, `schemas/`, `templates/`, `platforms/qwindows.dll`,
(2) архивирует его в zip, (3) прогоняет `makensis` с
`packaging/windows/installer.nsi`, передавая версию из `git describe`.
Итог — `MIK32-Configurator-win64.zip` (portable) и
`MIK32-Configurator-install.exe` (инсталлятор) в каталоге, переданном вторым
аргументом (по умолчанию — рядом со скриптом).

## Проверено на этой сессии

- `MIK32-Configurator.exe` под `wine`: реальная генерация проекта из
  `tests/fixtures/gpio_codegen_output.json` — файлы дерева PlatformIO-проекта
  создались корректно.
- `MIK32-Configurator-GUI.exe` (собран с `WIN32` — PE-заголовок подтверждён
  `file`: `PE32+ executable (GUI)`, не `(console)`) под `wine`: процесс
  стартует и держится (не падает сразу же с ошибкой недостающей DLL) при
  наличии `libgcc_s_seh-1.dll`/`libstdc++-6.dll`/`libwinpthread-1.dll`/
  `Qt6Core.dll`/`Qt6Gui.dll`/`Qt6Widgets.dll`/`platforms/qwindows.dll` рядом.
- `MIK32-Configurator-install.exe` под `wine` (`/S`, тихая установка) в чистом
  `WINEPREFIX`: все файлы пакета корректно оказались в
  `%LOCALAPPDATA%\MIK32-Configurator`, ярлык в Пуск и пункт «Удалить» создались
  (`AppData\Roaming\...\Start Menu\Programs\MIK32-Configurator\*.lnk`),
  деинсталлятор (`Uninstall.exe /S`) полностью убрал и файлы, и ярлыки Пуска.
- **Известное ограничение именно wine-прогона:** ярлык на Desktop не появился
  в тестовом `WINEPREFIX` — при этом реестровый путь `Desktop` (`HKCU\...\Shell
  Folders`) резолвится корректно, INSTFILES-секция отрабатывает без ошибок
  (`err:menubuilder` в логе — только неудачное извлечение иконки у
  Start-Menu-ярлыков, не блокирующее их создание), и NSIS `CreateShortCut` —
  штатный, отдельно ничем не отличающийся вызов что для Desktop, что для
  Start Menu. Похоже на изолированный quirk чистого `WINEPREFIX` в
  headless-окружении сборки (RPCSS/OLE-ошибки в том же логе), а не на баг
  `installer.nsi`; не подтверждено ни в одну, ни в другую сторону без
  прогона на реальной Windows.
- **Не проверено:** поведение на реальной Windows целиком (в т.ч. исходный
  баг с кириллическим путём — `wine` его не воспроизводит, см.
  `include/win_file_io.h`) и сам факт появления ярлыка на Desktop — оба
  пункта требуют прогона на настоящей Windows-машине.

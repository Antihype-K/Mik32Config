# Linux-упаковка (.deb)

В отличие от Windows (`packaging/windows/`) здесь не нужен отдельный
тулчейн или скрипт — `.deb` собирается обычным нативным CMake-билдом через
CPack/DEB, настроенным прямо в корневом `CMakeLists.txt` (раздел
«8. ПАКЕТ .deb (Linux) — CPack/DEB»).

## Сборка

```bash
cmake -S . -B build -DBUILD_TESTING=OFF -DBUILD_GUI=ON
cmake --build build --target package
```

Результат — `mik32-configurator_1.0.0_amd64.deb` в `build/`. Для согласованности
с `packaging/windows/out/` копию пакета для дистрибуции кладём в
`packaging/linux/out/` (gitignored, как и `packaging/windows/out/`):

```bash
mkdir -p packaging/linux/out
cp build/mik32-configurator_1.0.0_amd64.deb packaging/linux/out/
```

## Установка (для пользователя)

```bash
sudo apt install ./mik32-configurator_1.0.0_amd64.deb
```

Пакет ставит зависимости (`libqt6core6`, `libqt6gui6`, `libqt6widgets6`)
автоматически через apt. Подробности состава пакета — в
`docs/tutorial.docx`, раздел 3.2.

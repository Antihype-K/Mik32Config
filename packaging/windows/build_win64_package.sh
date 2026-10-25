#!/usr/bin/env bash
# Собирает portable win64-zip и install.exe из уже готовой кросс-сборки
# (см. packaging/windows/README.md — cmake/build шаги идут отдельно, этот
# скрипт их не повторяет: пересобирать exe при каждой упаковке — лишняя
# трата времени на инкрементальных прогонах во время отладки установщика).
#
# Использование: packaging/windows/build_win64_package.sh <build-dir> [out-dir]
set -euo pipefail

BUILD_DIR="${1:?Использование: $0 <build-dir> [out-dir]}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${2:-$REPO_ROOT/packaging/windows/out}"
WINBUILD_ROOT="${MIK32_WINBUILD_ROOT:-$HOME/.local/mik32-winbuild-tools}"

PKG_NAME="MIK32-Configurator-win64"
PKG_ROOT="$OUT_DIR/$PKG_NAME"

for exe in MIK32-Configurator.exe MIK32-Configurator-GUI.exe; do
    if [ ! -f "$BUILD_DIR/$exe" ]; then
        echo "Не найден $BUILD_DIR/$exe — сначала выполните cmake --build (см. README.md)" >&2
        exit 1
    fi
done

rm -rf "$OUT_DIR"
mkdir -p "$PKG_ROOT/platforms"

MINGW_ROOT="$WINBUILD_ROOT/mingw-local/root"
QT_ROOT="$WINBUILD_ROOT/Qt/6.7.0/mingw_64"
STRIP="$MINGW_ROOT/usr/bin/x86_64-w64-mingw32-strip"

cp "$BUILD_DIR/MIK32-Configurator.exe" "$PKG_ROOT/"
cp "$BUILD_DIR/MIK32-Configurator-GUI.exe" "$PKG_ROOT/"
cp "$MINGW_ROOT/usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll" "$PKG_ROOT/"
cp "$MINGW_ROOT/usr/lib/gcc/x86_64-w64-mingw32/13-posix/libstdc++-6.dll" "$PKG_ROOT/"
cp "$MINGW_ROOT/usr/lib/gcc/x86_64-w64-mingw32/13-posix/libgcc_s_seh-1.dll" "$PKG_ROOT/"
cp "$QT_ROOT/bin/Qt6Core.dll" "$PKG_ROOT/"
cp "$QT_ROOT/bin/Qt6Gui.dll" "$PKG_ROOT/"
cp "$QT_ROOT/bin/Qt6Widgets.dll" "$PKG_ROOT/"
cp "$QT_ROOT/plugins/platforms/qwindows.dll" "$PKG_ROOT/platforms/"

# Отладочные символы не нужны конечному пользователю и раздувают DLL в разы
# (libstdc++-6.dll — 26МБ неполосанный против 2МБ после strip).
"$STRIP" "$PKG_ROOT"/*.exe "$PKG_ROOT"/*.dll

# knowledge_base/ вперемешку содержит *.json (данные, нужны рантайму) и
# *_knowledge.{cpp,h} (исходники загрузчика, уже вкомпилированы в exe) —
# копируем только *.json, иначе в пакет уходят C++-исходники репозитория.
mkdir -p "$PKG_ROOT/knowledge_base"
cp "$REPO_ROOT"/knowledge_base/*.json "$PKG_ROOT/knowledge_base/"
cp -r "$REPO_ROOT/schemas" "$PKG_ROOT/"
cp -r "$REPO_ROOT/templates" "$PKG_ROOT/"
mkdir -p "$PKG_ROOT/examples"
cp "$REPO_ROOT/tests/fixtures/gpio_codegen_output.json" "$PKG_ROOT/examples/" 2>/dev/null || true
cp "$REPO_ROOT/tests/fixtures/uart_codegen_output.json" "$PKG_ROOT/examples/" 2>/dev/null || true

cp "$REPO_ROOT/packaging/windows/tutorial.docx" "$PKG_ROOT/"

echo "Portable-каталог: $PKG_ROOT"

# --- zip ---------------------------------------------------------------
ZIP_PATH="$OUT_DIR/$PKG_NAME.zip"
( cd "$OUT_DIR" && zip -rq "$ZIP_PATH" "$PKG_NAME" )
echo "Portable zip: $ZIP_PATH"

# --- install.exe (NSIS) -------------------------------------------------
VERSION="$(cd "$REPO_ROOT" && git describe --tags --always --dirty 2>/dev/null || echo 0.0.0-dev)"
MAKENSIS="$WINBUILD_ROOT/nsis-local/root/usr/bin/makensis"
NSISDIR="$WINBUILD_ROOT/nsis-local/root/usr/share/nsis"
INSTALLER_OUT="$OUT_DIR/MIK32-Configurator-install.exe"

if [ -x "$MAKENSIS" ]; then
    NSISDIR="$NSISDIR" "$MAKENSIS" \
        "-DPRODUCT_VERSION=$VERSION" \
        "-DPACKAGE_DIR=$PKG_ROOT" \
        "-DOUTPUT_FILE=$INSTALLER_OUT" \
        "$REPO_ROOT/packaging/windows/installer.nsi"
    echo "Установщик: $INSTALLER_OUT"
else
    echo "makensis не найден ($MAKENSIS) — install.exe не собран, см. README.md" >&2
    exit 1
fi

# CMake-тулчейн для кросс-сборки MIK32-Configurator под Windows (x86_64) на
# Linux-хосте. Инструменты не входят в репозиторий (см. packaging/windows/
# README.md, раздел «Инструменты») — путь ниже указывает на локальную,
# невоспроизводимую в CI без предварительной установки, но воспроизводимую
# вручную по README.md кэш-директорию каждого разработчика.
#
# Почему x86_64-w64-mingw32-gcc/g++ здесь — не системные (apt install
# g++-mingw-w64-x86-64), а собственные обёртки: пакеты Ubuntu ставят
# компилятор через update-alternatives (создаёт симлинк без суффикса
# -posix/-win32), который сработал только при полноценном apt install (с
# sudo). Здесь пакеты распакованы без sudo через dpkg-deb -x — симлинка нет,
# только *-posix бинарники, которым без явного -B неоткуда узнать, где лежат
# cc1plus/crtbegin.o. См. packaging/windows/README.md.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MIK32_WINBUILD_ROOT "$ENV{HOME}/.local/mik32-winbuild-tools" CACHE PATH
    "Каталог с распакованными MinGW/Qt6/NSIS инструментами (см. packaging/windows/README.md)")

set(CMAKE_C_COMPILER   "${MIK32_WINBUILD_ROOT}/mingw-local/bin/x86_64-w64-mingw32-gcc")
set(CMAKE_CXX_COMPILER "${MIK32_WINBUILD_ROOT}/mingw-local/bin/x86_64-w64-mingw32-g++")
set(CMAKE_RC_COMPILER  "${MIK32_WINBUILD_ROOT}/mingw-local/bin/x86_64-w64-mingw32-windres")
set(CMAKE_AR           "${MIK32_WINBUILD_ROOT}/mingw-local/bin/x86_64-w64-mingw32-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB       "${MIK32_WINBUILD_ROOT}/mingw-local/bin/x86_64-w64-mingw32-ranlib" CACHE FILEPATH "")

set(CMAKE_FIND_ROOT_PATH
    "${MIK32_WINBUILD_ROOT}/mingw-local/root/usr/x86_64-w64-mingw32"
    "${MIK32_WINBUILD_ROOT}/Qt/6.7.0/mingw_64"
)
set(CMAKE_PREFIX_PATH "${MIK32_WINBUILD_ROOT}/Qt/6.7.0/mingw_64")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

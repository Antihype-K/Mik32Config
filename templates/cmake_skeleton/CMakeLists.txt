cmake_minimum_required(VERSION 3.22)

#
# Этот файл генерируется только один раз
# и не перезаписывается при повторных запусках генератора.
#
# Пользователь может свободно изменять его как угодно —
# при повторной генерации проекта этот файл трогать не будут,
# если он уже существует в целевом каталоге.
#

# Имя проекта — предсказуемое имя переменной для генератора.
# Если она не была переопределена раньше (например, отдельным
# include, подключённым до этой строки, или через -D в командной
# строке cmake), используется значение по умолчанию.
if(NOT DEFINED MIK32_PROJECT_NAME)
    set(MIK32_PROJECT_NAME "mik32-firmware")
endif()

# Toolchain-файл обязан быть подключён ДО вызова project() —
# CMake фиксирует CMAKE_C_COMPILER/CMAKE_ASM_COMPILER в момент
# первого определения языков (project()/enable_language()), поэтому
# переменные компилятора должны быть выставлены заранее.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/mik32-riscv-toolchain.cmake)

project(${MIK32_PROJECT_NAME} C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Для clangd и прочих инструментов, работающих через compile_commands.json.
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

enable_language(C ASM)

add_executable(${MIK32_PROJECT_NAME})

# mik32-sdk/shared/ldscripts/sections.lds (вендоренный, тот же файл, что
# использует PlatformIO/SCons) матчит стартап-код по буквальному имени
# объектного файла: "*crt0.o(.text .text.*)". Обычный target_sources()
# называет объект CMake-ом "crt0.S.o"/"crt0.S.obj" — паттерн не совпадает,
# crt0 не попадает в .startup по ORIGIN(REGION_TEXT), линкер ставит entry
# point в середину .text вместо 0x80000000 — МК не стартует (найдено реальной
# прошивкой на плате: UART молчит при чистой линковке без ошибок). Поэтому
# crt0.S компилируется отдельной командой с жёстко заданным именем объекта.
# MIK32_CONFIGURATOR: CRT0_RULE

# Флаги компиляции/линковки перенесены как есть из
# templates/pio_skeleton/platformio.ini (секция [env] +
# [env:development-board-bootloader]) — уже проверены на плате 1T Rex Board,
# менять их значения здесь не нужно.
target_compile_options(${MIK32_PROJECT_NAME} PRIVATE
    -march=rv32im_zicsr_zifencei
    -mabi=ilp32
)

target_link_options(${MIK32_PROJECT_NAME} PRIVATE
    -march=rv32im_zicsr_zifencei
    -mabi=ilp32
    -lm
    # PlatformIO-параметр "board_build.ldscript = spifi" (platformio.ini) не
    # является буквальным флагом компилятора — PlatformIO разворачивает его
    # в -nostartfiles + -T<путь к spifi.ld> внутри собственных Python-скриптов
    # платформы mik32, невидимо для пользователя. Здесь то же самое явно:
    # -nostartfiles обязателен, иначе линкер берёт свой стандартный crt0
    # вместо mik32-sdk/shared/runtime/crt0.S (ошибка "multiple definition of
    # `_start'"), а -T на spifi.ld даёт символы __C_STACK_TOP__/__DATA_IMAGE_*/
    # __RAM_TEXT_*/__SBSS_START__/__TRAP_TEXT_START__, которые crt0.S ожидает
    # получить из линкер-скрипта (mik32-sdk/shared/ldscripts/sections.lds,
    # подключённого через spifi.ld директивой INCLUDE). Найдено попыткой
    # реальной линковки сгенерированного проекта настоящим тулчейном —
    # ни один синтетический юнит-тест этот шаг не проверяет.
    -nostartfiles
    -T${CMAKE_CURRENT_SOURCE_DIR}/mik32-sdk/shared/ldscripts/spifi.ld
    # spifi.ld делает "INCLUDE sections.lds" (относительный путь без
    # каталога) — ld ищет такие INCLUDE только по -L, а не рядом с самим
    # -T файлом, поэтому без этой строки линковка падает с "cannot open
    # linker script file sections.lds".
    -L${CMAKE_CURRENT_SOURCE_DIR}/mik32-sdk/shared/ldscripts
)

target_compile_definitions(${MIK32_PROJECT_NAME} PRIVATE
    MIK32
)

# Исходные файлы (HAL, сгенерированный mik32_init.c/.h, main.c и т.д.)
# добавляются генератором ниже маркера — простой текстовый поиск
# без полноценного CMake-парсера на стороне C++.
target_sources(${MIK32_PROJECT_NAME} PRIVATE
    # MIK32_CONFIGURATOR: SOURCES
)

# Каталоги заголовков (HAL, shared/libs и т.д.) добавляются генератором
# ниже маркера тем же способом.
target_include_directories(${MIK32_PROJECT_NAME} PRIVATE
    # MIK32_CONFIGURATOR: INCLUDE_DIRS
)

# HAL подключается исходными файлами (см. MIK32_CONFIGURATOR: SOURCES
# выше), а не отдельной библиотекой — поэтому здесь обычно ничего не
# требуется, кроме -lm, уже добавленного в target_link_options.
target_link_libraries(${MIK32_PROJECT_NAME} PRIVATE
)

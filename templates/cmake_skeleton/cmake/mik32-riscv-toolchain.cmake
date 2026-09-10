#
# Toolchain-файл кросс-компиляции для RISC-V (МИК32).
#
# Подключается через include() в CMakeLists.txt ДО вызова project() —
# CMake фиксирует CMAKE_C_COMPILER/CMAKE_ASM_COMPILER в момент первого
# определения языков, поэтому все переменные ниже должны быть готовы
# до этого момента.
#

# МИК32 — bare-metal RISC-V, операционной системы нет.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)

# Для bare-metal toolchain-файлов CMake не может выполнить обычную
# проверку компилятора линковкой исполняемого файла — на bare-metal
# нет ни рантайма ОС, ни ld-скрипта по умолчанию, способного собрать
# рабочий exe. Стандартная практика (ARM/RISC-V bare-metal toolchain-
# файлы) — проверять компилятор сборкой статической библиотеки.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Имя бинарника — ИМЕННО riscv-none-elf-gcc.
# Проверено: `ls ~/.platformio/packages/toolchain-riscv/bin/`
# (пакет toolchain-riscv, версия 1.130200.2). Бинарника
# riscv32-unknown-elf-gcc в этом пакете НЕТ — использовать его как
# основное имя было бы ошибкой.
set(_MIK32_GCC_NAME "riscv-none-elf-gcc")

if(DEFINED MIK32_COMPILER_PATH)
    # 1) Явно указанный путь — из GUI-конфигуратора или из командной
    #    строки (-DMIK32_COMPILER_PATH=...). Наивысший приоритет,
    #    используется напрямую, без дальнейшего поиска.
    set(_MIK32_GCC_PATH "${MIK32_COMPILER_PATH}")
else()
    # 2) Путь по умолчанию для PlatformIO-инсталляции пакета
    #    toolchain-riscv. CMAKE_HOST_WIN32 (а не WIN32!) — потому что
    #    CMAKE_SYSTEM_NAME здесь Generic, и WIN32/UNIX/APPLE отражают
    #    целевую, а не хостовую платформу.
    if(CMAKE_HOST_WIN32)
        set(_MIK32_GCC_CANDIDATE "$ENV{USERPROFILE}/.platformio/packages/toolchain-riscv/bin/${_MIK32_GCC_NAME}.exe")
    else()
        set(_MIK32_GCC_CANDIDATE "$ENV{HOME}/.platformio/packages/toolchain-riscv/bin/${_MIK32_GCC_NAME}")
    endif()

    if(EXISTS "${_MIK32_GCC_CANDIDATE}")
        set(_MIK32_GCC_PATH "${_MIK32_GCC_CANDIDATE}")
    else()
        # 3) Поиск в PATH — на случай нестандартной установки PlatformIO
        #    или отдельно поставленного системного toolchain'а.
        find_program(MIK32_COMPILER_AUTODETECTED
            NAMES riscv-none-elf-gcc riscv64-unknown-elf-gcc riscv32-unknown-elf-gcc
        )

        if(MIK32_COMPILER_AUTODETECTED)
            set(_MIK32_GCC_PATH "${MIK32_COMPILER_AUTODETECTED}")
        else()
            # 4) Ничего не найдено ни одним из трёх способов выше.
            message(FATAL_ERROR
                "МИК32: компилятор riscv-none-elf-gcc не найден — ни по "
                "MIK32_COMPILER_PATH, ни в стандартной директории PlatformIO "
                "(~/.platformio/packages/toolchain-riscv/bin), ни в PATH. "
                "Укажите путь к компилятору вручную: "
                "-DMIK32_COMPILER_PATH=<путь-к-riscv-none-elf-gcc>"
            )
        endif()
    endif()
endif()

set(CMAKE_C_COMPILER "${_MIK32_GCC_PATH}")
# Тот же бинарник ассемблирует .S-файлы — gcc умеет вызывать ассемблер
# как драйвер, отдельный as не нужен.
set(CMAKE_ASM_COMPILER "${_MIK32_GCC_PATH}")

unset(_MIK32_GCC_NAME)
unset(_MIK32_GCC_CANDIDATE)
unset(_MIK32_GCC_PATH)

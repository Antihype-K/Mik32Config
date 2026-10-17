# 1. Задаем минимальную версию CMake (стабильная modern-версия)
cmake_minimum_required(VERSION 3.20)

# 2. Имя проекта и указываем C++
project(MIK32-configurator CXX)

# 3. Фиксируем стандарт C++17 (необходим для работы с modern C++ и json.hpp)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# 4. ЯДРО КОНФИГУРАТОРА — статическая библиотека
# ==============================================================================
# Раньше все исходники перечислялись напрямую в каждом add_executable, и список
# приходилось держать синхронным в двух местах. С появлением GUI потребителей
# стало три (консольное приложение, тесты, GUI), поэтому ядро вынесено в
# библиотеку: логика парсинга, базы знаний, валидации и кодогенерации не знает
# ничего ни про Qt, ни про argv, и линкуется во всех троих одинаково.
#
# Библиотека НЕ содержит src/main.cpp и ничего из gui/ — только то, что не
# зависит ни от способа ввода, ни от способа отображения.
set(CORE_SOURCES
        # Парсеры: JSON-конфигурация -> типизированные структуры + валидация схемы
        parser/gpio/gpio_parser.cpp
        parser/i2c/i2c_parser.cpp
        parser/spi/spi_parser.cpp
        parser/timing/timing_parser.cpp
        parser/USART/usart_parser.cpp
        parser/timer16/timer16_parser.cpp
        parser/timer32/timer32_parser.cpp
        parser/freertos/freertos_parser.cpp
        parser/rtc/rtc_parser.cpp
        parser/crc/crc_parser.cpp
        parser/eeprom/eeprom_parser.cpp
        parser/wdt/wdt_parser.cpp
        parser/dac/dac_parser.cpp
        parser/adc/adc_parser.cpp
        parser/crypto/crypto_parser.cpp
        parser/otp/otp_parser.cpp
        parser/tsens/tsens_parser.cpp
        parser/pvd/pvd_parser.cpp
        parser/bus_watchdog/bus_watchdog_parser.cpp
        parser/dma/dma_parser.cpp

        # База знаний МИК32: аппаратные факты из даташита и errata
        knowledge_base/gpio_knowledge.cpp
        knowledge_base/uart_knowledge.cpp
        knowledge_base/timer16_knowledge.cpp
        knowledge_base/i2c_knowledge.cpp
        knowledge_base/package_knowledge.cpp
        knowledge_base/timer32_knowledge.cpp
        knowledge_base/spi_knowledge.cpp
        knowledge_base/rtc_knowledge.cpp
        knowledge_base/dac_knowledge.cpp

        # Кросс-модульная валидация (ТЗ §3.1.6)
        validation/cross_module_validator.cpp
        # Валидация "поддерживается ли фича в Rust-ветке вообще" — отдельно
        # от конфликтов пинов выше (docs/implementations/rust_codegen.md)
        validation/rust_feature_validator.cpp

        # Межмодульная кодогенерация: вклады периферии + единственный сборщик
        codegen/project/project_config.cpp
        codegen/project/contribution_utils.cpp
        codegen/project/gpio_contribution.cpp
        codegen/project/uart_contribution.cpp
        codegen/project/timer16_contribution.cpp
        codegen/project/i2c_contribution.cpp
        codegen/project/timer32_contribution.cpp
        codegen/project/spi_contribution.cpp
        codegen/project/rtc_contribution.cpp
        codegen/project/crc_contribution.cpp
        codegen/project/eeprom_contribution.cpp
        codegen/project/wdt_contribution.cpp
        codegen/project/dac_contribution.cpp
        codegen/project/adc_contribution.cpp
        codegen/project/crypto_contribution.cpp
        codegen/project/otp_contribution.cpp
        codegen/project/tsens_contribution.cpp
        codegen/project/pvd_contribution.cpp
        codegen/project/bus_watchdog_contribution.cpp
        codegen/project/dma_contribution.cpp
        codegen/project/freertos_codegen.cpp
        codegen/project/project_codegen.cpp
        codegen/project/cmake_project_codegen.cpp
        codegen/user_code_markers.cpp

        # Rust-ветка кодогенерации — второй целевой язык
        # (docs/implementations/rust_codegen.md)
        codegen/project/rust_project_codegen.cpp
        codegen/user_code_markers_rust.cpp

        # Reverse-import: вычленение периферии из произвольной, НЕ сгенерированной
        # этим конфигуратором прошивки (docs/implementations/reverse_import.md) —
        # сканер + четыре типовых маппера + точечный патчер исходников + два
        # оркестрирующих слоя (файлы -> JSON и JSON-правки -> файлы).
        parser/reverse_import/reverse_import_scanner.cpp
        parser/reverse_import/c_literal_parser.cpp
        parser/reverse_import/uart_reverse_mapper.cpp
        parser/reverse_import/i2c_reverse_mapper.cpp
        parser/reverse_import/timer32_reverse_mapper.cpp
        parser/reverse_import/timer32_channel_reverse_mapper.cpp
        parser/reverse_import/timer16_reverse_mapper.cpp
        parser/reverse_import/gpio_reverse_mapper.cpp
        parser/reverse_import/spi_reverse_mapper.cpp
        parser/reverse_import/adc_reverse_mapper.cpp
        parser/reverse_import/dac_reverse_mapper.cpp
        parser/reverse_import/crypto_reverse_mapper.cpp
        parser/reverse_import/dma_reverse_mapper.cpp
        parser/reverse_import/eeprom_reverse_mapper.cpp
        parser/reverse_import/reverse_patch_writer.cpp
        parser/reverse_import/reverse_import.cpp
        parser/reverse_import/reverse_import_apply.cpp
)

add_library(mik32-core STATIC ${CORE_SOURCES})

# PUBLIC — каталоги нужны и самой библиотеке, и всем, кто её линкует
# (консольное приложение, тесты, GUI подключают эти же заголовки).
target_include_directories(mik32-core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/parser
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/gpio
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/USART
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/timer16
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/i2c
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/timer32
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/spi
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/freertos
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/rtc
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/crc
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/eeprom
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/wdt
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/dac
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/adc
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/crypto
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/otp
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/tsens
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/pvd
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/bus_watchdog
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/dma
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/reverse_import
        ${CMAKE_CURRENT_SOURCE_DIR}/knowledge_base
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/project
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen
        ${CMAKE_CURRENT_SOURCE_DIR}/validation
)

# ==============================================================================
# 5. КОНСОЛЬНОЕ ПРИЛОЖЕНИЕ
# ==============================================================================
add_executable(MIK32-Configurator src/main.cpp)
target_link_libraries(MIK32-Configurator PRIVATE mik32-core)

# Регрессионный прогон reverse-import по каталогу вендорных примеров
# (docs/examples/mik32-examples). Не GoogleTest-тест, т.к. сами примеры лежат
# в gitignored docs/ — см. комментарий в заголовке файла.
add_executable(reverse-import-smoke tools/reverse_import_smoke_test.cpp)
target_link_libraries(reverse-import-smoke PRIVATE mik32-core)

# ==============================================================================
# 6. ГРАФИЧЕСКИЙ ИНТЕРФЕЙС (ТЗ §3.1.9, §3.4 п.5 — Qt 6.x)
# ==============================================================================
# Собирается, только если Qt6 найден: ядро и тесты не должны переставать
# собираться на машине без Qt (в т.ч. в CI).
option(BUILD_GUI "Build the Qt6 GUI" ON)

if(BUILD_GUI)
    find_package(Qt6 6.2 COMPONENTS Core Gui Widgets QUIET)

    if(Qt6_FOUND)
        # AUTOMOC обязателен: классы GUI используют Q_OBJECT (сигналы/слоты),
        # для них генерируется код moc'ом на этапе сборки.
        set(CMAKE_AUTOMOC ON)

        # Кросс-компиляция под Windows (MinGW-тулчейн + Qt6 для Windows):
        # moc/uic/rcc, которые ставит сам Qt6-пакет, — это moc.exe и т.п.,
        # PE-бинарники, которые на Linux-хосте напрямую не запустить. CMake
        # тестово вызывает их при конфигурации и падает с "permission denied".
        # QT_HOST_PATH (штатный механизм Qt6 для этого случая) здесь не
        # сработал на этой связке версий Qt, поэтому тот же исполняемый PE
        # запускается через wine — не подменяется другой версией инструмента,
        # только способ его запуска.
        if(CMAKE_CROSSCOMPILING AND WIN32)
            find_program(WINE_EXECUTABLE wine)
            if(WINE_EXECUTABLE)
                foreach(qt_tool moc uic rcc)
                    if(TARGET Qt6::${qt_tool})
                        get_target_property(_qt_tool_exe Qt6::${qt_tool} IMPORTED_LOCATION)
                        if(_qt_tool_exe MATCHES "\\.exe$")
                            set(_wrapper "${CMAKE_BINARY_DIR}/qt-wine-wrappers/${qt_tool}")
                            file(WRITE "${_wrapper}" "#!/bin/sh\nexec \"${WINE_EXECUTABLE}\" \"${_qt_tool_exe}\" \"$@\"\n")
                            file(CHMOD "${_wrapper}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
                            set_target_properties(Qt6::${qt_tool} PROPERTIES IMPORTED_LOCATION "${_wrapper}")
                        endif()
                    endif()
                endforeach()
            else()
                message(WARNING "Кросс-сборка под Windows: wine не найден — moc/uic/rcc (PE-бинарники Qt6) не запустятся на конфигурации.")
            endif()
        endif()

        # WIN32 здесь — не заглушка "для вида": без него в MinGW-сборке
        # исполняемый файл линкуется как консольная подсистема, и двойной
        # клик по .exe на Windows открывает GUI за окном чёрной консоли
        # (воспроизведено пользователем на реальной машине). CMake сам
        # игнорирует WIN32 на остальных платформах (Linux/macOS) — здесь он
        # не нужен и не мешает.
        add_executable(MIK32-Configurator-GUI WIN32
                gui/gui_main.cpp
                gui/main_window.cpp
                gui/main_window.h
                gui/project_document.cpp
                gui/project_document.h
                gui/peripheral_tree.cpp
                gui/peripheral_tree.h
                gui/package_view.cpp
                gui/package_view.h
                gui/properties_panel.cpp
                gui/properties_panel.h
                gui/diagnostics_panel.cpp
                gui/diagnostics_panel.h
                gui/screen_navigator.cpp
                gui/screen_navigator.h
                gui/home_screen.cpp
                gui/home_screen.h
                gui/new_project_screen.cpp
                gui/new_project_screen.h
                gui/project_manager_screen.cpp
                gui/project_manager_screen.h
                gui/clock_config_screen.cpp
                gui/clock_config_screen.h
                gui/freertos_settings_widget.cpp
                gui/freertos_settings_widget.h
                gui/code_preview_screen.cpp
                gui/code_preview_screen.h
        )

        target_include_directories(MIK32-Configurator-GUI
                PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui)

        target_link_libraries(MIK32-Configurator-GUI
                PRIVATE mik32-core Qt6::Core Qt6::Gui Qt6::Widgets)
    else()
        message(STATUS "Qt6 не найден — цель MIK32-Configurator-GUI пропущена. "
                       "Установить: sudo apt install qt6-base-dev")
    endif()
endif()

# ==============================================================================
# 7. ПОДКЛЮЧЕНИЕ GOOGLETEST ДЛЯ ЮНИТ-ТЕСТОВ
# ==============================================================================
option(BUILD_TESTING "Build unit tests" OFF)

if(BUILD_TESTING)
    include(FetchContent)

    FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG release-1.12.1
    )

    # Для Windows
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(MIK32-Parser-Tests
            tests/test_parsers.cpp
            tests/test_cross_module_severity_standalone.cpp
            tests/test_reverse_import_uart_i2c.cpp
            tests/test_reverse_import_timer32_gpio.cpp
            tests/test_reverse_patch_writer.cpp
            tests/test_reverse_import_integration.cpp
            tests/test_reverse_import_timer16.cpp
            tests/test_reverse_import_spi.cpp
            tests/test_reverse_import_timer32_channel.cpp
            tests/test_reverse_import_adc.cpp
            tests/test_reverse_import_dac.cpp
            tests/test_reverse_import_crypto.cpp
            tests/test_reverse_import_dma.cpp
            tests/test_reverse_import_eeprom.cpp
            tests/test_freertos.cpp
            tests/test_rust_project_codegen.cpp
    )

    target_link_libraries(MIK32-Parser-Tests
            PRIVATE
            mik32-core
            gtest_main
    )

    include(GoogleTest)
    # WORKING_DIRECTORY = tests/, чтобы относительные пути фикстур в тестах
    # ("fixtures/gpio_valid.json" и т.п., см. tests/test_parsers.cpp) резолвились
    # от каталога с самими фикстурами (tests/fixtures/), а не от каталога сборки.
    gtest_discover_tests(MIK32-Parser-Tests
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
endif()

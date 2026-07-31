# 1. Задаем минимальную версию CMake (стабильная modern-версия)
cmake_minimum_required(VERSION 3.20)

# 2. Имя проекта и указываем C++
project(MIK32-configurator CXX)

# 3. Фиксируем стандарт C++17 (необходим для работы с modern C++ и json.hpp)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# 4. СБОРКА ОСНОВНОГО ИСПОЛНЯЕМОГО ФАЙЛА
# ==============================================================================

# Создаем исполняемый файл и явно собираем все исходники логики парсеров
add_executable(MIK32-Configurator
        src/main.cpp

        # Модули парсера периферии
        parser/gpio/gpio_parser.cpp      # Добавь реальные имя_файла.cpp, если они есть
        parser/i2c/i2c_parser.cpp
        parser/spi/spi_parser.cpp
        parser/timing/timing_parser.cpp
        parser/USART/usart_parser.cpp
        parser/gpio/gpio_parser.h
        parser/gpio/gpio_parser.h
)

# Подключаем папки с заголовочными файлами
target_include_directories(MIK32-Configurator
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include        # Для json.hpp и публичных заголовков
        ${CMAKE_CURRENT_SOURCE_DIR}/parser         # Чтобы удобнее включать headers парсеров
)

# ==============================================================================
# 5. (ОПЦИОНАЛЬНО) ПОДКТЮЧЕНИЕ GOOGLETEST ДЛЯ ЮНИТ-ТЕСТОВ
# ==============================================================================
option(BUILD_TESTING "Build unit tests" OFF)

if(BUILD_TESTING)
    include(FetchContent)

    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )

    # Фикс совместимости MSVC для Windows
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)

    enable_testing()

    # Создаем отдельные тесты под парсеры
    add_executable(MIK32-Parser-Tests
            tests/test_parsers.cpp
            # Файлы парсеров без src/main.cpp
            parser/gpio/gpio_parser.cpp
            parser/i2c/i2c_parser.cpp
            parser/spi/spi_parser.cpp
            parser/timing/timing_parser.cpp
            parser/USART/usart_parser.cpp
            parser/gpio/gpio_parser.h
            parser/gpio/gpio_parser.h
    )

    target_include_directories(MIK32-Parser-Tests
            PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/parser
    )

    target_link_libraries(MIK32-Parser-Tests
            PRIVATE
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(MIK32-Parser-Tests)
endif()


#Инструкция по сборке
#Обычная сборка приложения:

#Bash
#cmake -S . -B build
#cmake --build build
#Результат: Бинарник MIK32-Configurator скомпилирован в папке build/.


#Сборка вместе с юнит-тестами:

#Bash
#cmake -S . -B build -DBUILD_TESTING=ON
#cmake --build build
#cd build && ctest --output-on-failure
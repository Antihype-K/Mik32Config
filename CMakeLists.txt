# 1. Задаем минимальную версию CMake (стабильная modern-версия)
cmake_minimum_required(VERSION 3.20)

# 2. Имя проекта и указываем C++
project(MIK32-configurator CXX)

# 3. Фиксируем стандарт C++17 (необходим для работы с modern C++ и json.hpp)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# 4. ОБЩИЕ ИСХОДНЫЕ ФАЙЛЫ (чтобы не дублировать)
# ==============================================================================
set(PARSER_SOURCES
        parser/gpio/gpio_parser.cpp
        parser/i2c/i2c_parser.cpp
        parser/spi/spi_parser.cpp
        parser/timing/timing_parser.cpp
        parser/USART/usart_parser.cpp
)

set(PARSER_HEADERS
        include/parse_result.h
)

set(GPIO_CODEGEN_SOURCES
        knowledge_base/gpio_knowledge.cpp
        codegen/gpio/gpio_codegen.cpp
)

set(GPIO_CODEGEN_HEADERS
        knowledge_base/gpio_knowledge.h
        codegen/gpio/gpio_codegen.h
)

set(UART_CODEGEN_SOURCES
        knowledge_base/uart_knowledge.cpp
        codegen/uart/uart_codegen.cpp
)

set(UART_CODEGEN_HEADERS
        knowledge_base/uart_knowledge.h
        codegen/uart/uart_codegen.h
        include/codegen_result.h
)

set(USER_CODE_MARKERS_SOURCES
        codegen/user_code_markers.cpp
)

set(USER_CODE_MARKERS_HEADERS
        codegen/user_code_markers.h
)

# ==============================================================================
# 5. СБОРКА ОСНОВНОГО ИСПОЛНЯЕМОГО ФАЙЛА
# ==============================================================================
add_executable(MIK32-Configurator
        src/main.cpp
        ${PARSER_SOURCES}
        ${PARSER_HEADERS}
        ${GPIO_CODEGEN_SOURCES}
        ${GPIO_CODEGEN_HEADERS}
        ${UART_CODEGEN_SOURCES}
        ${UART_CODEGEN_HEADERS}
        ${USER_CODE_MARKERS_SOURCES}
        ${USER_CODE_MARKERS_HEADERS}
)

# Подключаем папки с заголовочными файлами
target_include_directories(MIK32-Configurator
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/parser
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/gpio
        ${CMAKE_CURRENT_SOURCE_DIR}/parser/USART
        ${CMAKE_CURRENT_SOURCE_DIR}/knowledge_base
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/gpio
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/uart
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen
)

# ==============================================================================
# 6. ПОДКЛЮЧЕНИЕ GOOGLETEST ДЛЯ ЮНИТ-ТЕСТОВ
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

    # Создаем отдельные тесты под парсеры
    add_executable(MIK32-Parser-Tests
            tests/test_parsers.cpp
            ${PARSER_SOURCES}
            ${PARSER_HEADERS}
            ${GPIO_CODEGEN_SOURCES}
            ${GPIO_CODEGEN_HEADERS}
            ${UART_CODEGEN_SOURCES}
            ${UART_CODEGEN_HEADERS}
            ${USER_CODE_MARKERS_SOURCES}
            ${USER_CODE_MARKERS_HEADERS}
    )

    target_include_directories(MIK32-Parser-Tests
            PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/parser
            ${CMAKE_CURRENT_SOURCE_DIR}/parser/gpio
            ${CMAKE_CURRENT_SOURCE_DIR}/parser/USART
            ${CMAKE_CURRENT_SOURCE_DIR}/knowledge_base
            ${CMAKE_CURRENT_SOURCE_DIR}/codegen/gpio
            ${CMAKE_CURRENT_SOURCE_DIR}/codegen/uart
            ${CMAKE_CURRENT_SOURCE_DIR}/codegen
    )

    target_link_libraries(MIK32-Parser-Tests
            PRIVATE
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
//
// Тесты classifyPinConflicts/classifyPinConflictsInDocument (ТЗ §3.1.6.3) —
// новый, отдельный тестовый файл: цель, которая его подключит к сборке
// (см. комментарий в конце файла), сейчас занята параллельным агентом
// вместе с CMakeLists.txt/tests/test_parsers.cpp, поэтому эти кейсы не
// вписаны прямо в test_parsers.cpp, а живут отдельно до координаторской
// интеграции.
//
// Стиль — как у TEST(CrossModuleValidator, ...) в tests/test_parsers.cpp:
// та же загрузка knowledge_base через loadXKnowledge(...), те же фикстуры
// из tests/fixtures/.
//

#include <gtest/gtest.h>
#include <cross_module_validator.h>
#include <uart_knowledge.h>
#include <timer16_knowledge.h>
#include <i2c_knowledge.h>
#include <timer32_knowledge.h>
#include <spi_knowledge.h>

namespace
{
    // Общая обвязка загрузки knowledge_base — идентична паттерну из
    // TEST(CrossModuleValidator, ...) в test_parsers.cpp, вынесена сюда,
    // чтобы не повторять пять ASSERT_TRUE в каждом TEST-кейсе.
    struct LoadedKnowledge
    {
        UartKnowledge uart;
        Timer16Knowledge timer16;
        I2cKnowledge i2c;
        Timer32Knowledge timer32;
        SpiKnowledge spi;
    };

    LoadedKnowledge loadAllKnowledgeOrDie()
    {
        ParseResult<UartKnowledge> uart_r = loadUartKnowledge("../knowledge_base/uart.json");
        EXPECT_TRUE(uart_r.success) << uart_r.error;
        ParseResult<Timer16Knowledge> t16_r = loadTimer16Knowledge("../knowledge_base/timer16.json");
        EXPECT_TRUE(t16_r.success) << t16_r.error;
        ParseResult<I2cKnowledge> i2c_r = loadI2cKnowledge("../knowledge_base/i2c.json");
        EXPECT_TRUE(i2c_r.success) << i2c_r.error;
        ParseResult<Timer32Knowledge> t32_r = loadTimer32Knowledge("../knowledge_base/timer32.json");
        EXPECT_TRUE(t32_r.success) << t32_r.error;
        ParseResult<SpiKnowledge> spi_r = loadSpiKnowledge("../knowledge_base/spi.json");
        EXPECT_TRUE(spi_r.success) << spi_r.error;

        return LoadedKnowledge{
            *uart_r.config, *t16_r.config, *i2c_r.config, *t32_r.config, *spi_r.config};
    }
}

TEST(CrossModuleValidatorSeverity, ClassifiesGpioUartPinConflictAsError)
{
    // Given: та же фикстура, что и CrossModuleValidator.DetectsGpioUartPinConflict
    // (test_parsers.cpp) — GPIO instance 0 pin 6 сталкивается с UART instance 0 TX/RX.
    LoadedKnowledge k = loadAllKnowledgeOrDie();

    // When
    ClassifiedCrossModuleValidationResult result = classifyPinConflicts(
        "fixtures/cross_gpio_uart_conflict.json", k.uart, k.timer16, k.i2c, k.timer32, k.spi);

    // Then: обычный конфликт GPIO<->UART — блокирующая Ошибка, без errata-контекста.
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.conflicts.size(), 1u);
    EXPECT_EQ(result.conflicts[0].gpio_instance, 0);
    EXPECT_EQ(result.conflicts[0].pin, 6);
    EXPECT_EQ(result.conflicts[0].conflicting_owners.size(), 2u);
    EXPECT_EQ(result.conflicts[0].severity, ConflictSeverity::Error);
    EXPECT_TRUE(result.conflicts[0].errata_note.empty());
}

TEST(CrossModuleValidatorSeverity, NoConflictWhenPinsDontOverlap)
{
    // Given
    LoadedKnowledge k = loadAllKnowledgeOrDie();

    // When
    ClassifiedCrossModuleValidationResult result = classifyPinConflicts(
        "fixtures/cross_gpio_uart_no_conflict.json", k.uart, k.timer16, k.i2c, k.timer32, k.spi);

    // Then
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.conflicts.empty());
}

TEST(CrossModuleValidatorSeverity, ClassifiesSpiGpioPinConflictAsError)
{
    // Given: SPI instance 0 (mosi = GPIO_0/pin1) сталкивается с обычным GPIO-блоком
    // на том же пине — ординарный конфликт, никак не связанный с SPI Errata п.7.1
    // (тот требует instance=0 + master + manual_cs=false + chip_select=cs3, здесь
    // chip_select=cs0 и конфликт вообще не по CS-пину, а по SCK/MOSI/MISO/SS_IN).
    LoadedKnowledge k = loadAllKnowledgeOrDie();

    // When
    ClassifiedCrossModuleValidationResult result = classifyPinConflicts(
        "fixtures/cross_spi_gpio_conflict.json", k.uart, k.timer16, k.i2c, k.timer32, k.spi);

    // Then
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.conflicts.size(), 1u);
    EXPECT_EQ(result.conflicts[0].severity, ConflictSeverity::Error);
    EXPECT_TRUE(result.conflicts[0].errata_note.empty());
}

TEST(CrossModuleValidatorSeverity, MvpClassifiesEveryConflictAsErrorRegardlessOfErrataContext)
{
    // Given: документирует явно MVP-ограничение (см.
    // docs/implementations/cross_module_validation.md, раздел "MVP-ограничение
    // классификации") — на сегодняшний день Warning-ветка недостижима, потому
    // что knowledge_base не несёт структурного признака errata-совместимости,
    // а единственный известный кандидат (SPI Errata п.7.1) не порождает
    // коллизию (gpio_instance, pin) в принципе (cs2/cs3 инстанса 0 — разные
    // физические пины в knowledge_base/spi.json). Тест фиксирует нынешнее
    // поведение как явный контракт, чтобы будущее расширение (когда
    // knowledge_base заведёт errata_compatible_conflict) сознательно его
    // сломало, а не тихо разошлось с документацией.
    LoadedKnowledge k = loadAllKnowledgeOrDie();

    // When
    ClassifiedCrossModuleValidationResult gpio_uart = classifyPinConflicts(
        "fixtures/cross_gpio_uart_conflict.json", k.uart, k.timer16, k.i2c, k.timer32, k.spi);
    ClassifiedCrossModuleValidationResult spi_gpio = classifyPinConflicts(
        "fixtures/cross_spi_gpio_conflict.json", k.uart, k.timer16, k.i2c, k.timer32, k.spi);

    // Then
    ASSERT_TRUE(gpio_uart.success) << gpio_uart.error;
    ASSERT_TRUE(spi_gpio.success) << spi_gpio.error;
    for (const ClassifiedPinConflict& conflict : gpio_uart.conflicts)
    {
        EXPECT_EQ(conflict.severity, ConflictSeverity::Error);
    }
    for (const ClassifiedPinConflict& conflict : spi_gpio.conflicts)
    {
        EXPECT_EQ(conflict.severity, ConflictSeverity::Error);
    }
}

// ----------------------------------------------------------------------------
// Интеграция в сборку (для координатора):
// В CMakeLists.txt добавить tests/test_cross_module_severity_standalone.cpp
// в список источников цели MIK32-Parser-Tests (тот же список, куда уже
// входит tests/test_parsers.cpp) — отдельная цель не нужна, файл использует
// тот же gtest_main и те же фикстуры/knowledge_base по относительным путям
// "../knowledge_base/..." и "fixtures/...", что и test_parsers.cpp, то есть
// предполагает тот же working directory ctest'а (build/tests или аналог,
// где уже настроен запуск существующих тестов).
// ----------------------------------------------------------------------------

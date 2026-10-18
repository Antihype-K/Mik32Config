#include "project_document.h"
#include "utf8_path.h"
#include "win_file_io.h"
#include "project_config.h"
#include "cross_module_validator.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
    // Значения по умолчанию для нового блока. Взяты не с потолка: каждое —
    // либо единственно допустимое по схеме, либо то, что уже проверено на
    // плате (см. tests/fixtures/*.json и docs/implementations/).
    nlohmann::json defaultBlock(const std::string& type, int instance)
    {
        nlohmann::json block;
        block["type"] = type;
        block["instance"] = instance;

        if (type == "gpio")
        {
            // Один пин на выход — минимальная валидная конфигурация:
            // schemas/gpio.json требует непустой "pins".
            block["mode"] = "output";
            block["pull"] = "none";
            block["drive_strength"] = "2mA";
            block["pins"] = nlohmann::json::array({0});
        }
        else if (type == "uart")
        {
            // 38400 8N1 duplex — конфигурация, проверенная на 1T Rex Board
            // (docs/implementations/uart.md).
            block["baud_rate"] = 38400;
            block["data_bits"] = 8;
            block["stop_bits"] = 1;
            block["parity"] = "none";
            block["flow_control"] = false;
            block["direction"] = "duplex";
        }
        else if (type == "timer16")
        {
            // period + prescaler 16 + ARR 999/CMP 499 — 2 кГц, скважность 50%
            // при HSI 32 МГц (ТЗ §3.1.8).
            block["mode"] = "period";
            block["prescaler"] = 16;
            block["arr"] = 999;
            block["cmp"] = 499;
        }
        else if (type == "i2c")
        {
            // 100 кГц — единственная скорость, для которой в проекте есть
            // подтверждённые железом коэффициенты TIMINGR
            // (см. calcI2cTimingr в codegen/project/i2c_contribution.cpp).
            block["mode"] = "master";
            block["clock_speed"] = 100000;
        }
        else if (type == "spi")
        {
            // master/CPHA=on/CPOL=high/DIV64/CS0 — ровно значения обоих
            // вендорных примеров SPI0_Init (docs/examples/mik32-examples/
            // HAL_SPI_Master/src/main.c), кроме manual_cs: там ManualCS=OFF,
            // здесь тоже OFF (auto), чтобы не задеть Errata п.7.1 (та
            // актуальна только для chip_select=cs3, дефолт — cs0).
            block["mode"] = "master";
            block["clk_phase"] = "on";
            block["clk_polarity"] = "high";
            block["threshold_tx"] = 4;
            block["baud_rate_div"] = 64;
            block["manual_cs"] = false;
            block["decoder"] = false;
            block["chip_select"] = "cs0";
        }
        else if (type == "timer32")
        {
            // forward/prescaler 31/top 999 — та же HW-подтверждённая
            // конфигурация, что properties_panel.cpp подставляет через
            // setField при первой отрисовке формы (см. buildTimer32Form);
            // дублируется здесь, чтобы addBlock() сразу давал валидный
            // блок и для потребителей вне GUI-формы (например клика по
            // пину в MainWindow::onPinClicked).
            block["count_mode"] = "forward";
            block["prescaler"] = 31;
            block["top"] = 999;
            block["channels"] = nlohmann::json::array();
        }
        else if (type == "rtc")
        {
            // Полночь 1 января 2000 года — валидная, нейтральная дата без
            // будильника (schemas/rtc.json: time/date обязательны, alarm нет).
            block["time"] = {{"dow", 6}, {"hours", 0}, {"minutes", 0}, {"seconds", 0}};
            block["date"] = {{"century", 20}, {"day", 1}, {"month", 1}, {"year", 0}};
        }
        else if (type == "crc")
        {
            // CRC-32 (preset) — самый распространённый вариант, известные
            // контрольные значения подтверждены на плате (docs/implementations/crc.md).
            block["preset"] = "crc32";
        }
        else if (type == "eeprom")
        {
            // two_stage — то же значение, что использует вендорный пример по
            // умолчанию (schemas/eeprom.json).
            block["mode"] = "two_stage";
        }
        else if (type == "wdt")
        {
            // osc32k/1000мс — безопасный дефолт в пределах предела для 32 кГц
            // источника (schemas/wdt.json: 511875 мс максимум для 32к).
            // HAL_WDT_Start/Refresh кодоген НЕ генерирует (см. схему).
            block["clock_source"] = "osc32k";
            block["reload_ms"] = 1000;
        }
        else if (type == "dac")
        {
            // div=0 (без деления), встроенный ИОН — минимальная валидная
            // конфигурация (schemas/dac.json).
            block["div"] = 0;
            block["ext_ref"] = false;
            block["ext_clb"] = false;
        }
        else if (type == "adc")
        {
            // channel=0, встроенный ИОН — минимальная валидная конфигурация
            // (schemas/adc.json).
            block["channel"] = 0;
            block["ext_ref"] = false;
            block["ext_clb"] = false;
        }
        else if (type == "crypto")
        {
            // AES/CBC — распространённая комбинация, ключ/IV НЕ хранятся в
            // конфиге (schemas/crypto.json, _comment_no_key_no_iv).
            block["algorithm"] = "aes";
            block["cipher_mode"] = "cbc";
        }
        else if (type == "otp")
        {
            // two_stages — единственный безопасный дефолт (HAL_OTP_Write*
            // кодоген никогда не генерирует, см. schemas/otp.json).
            block["read_mode"] = "two_stages";
        }
        else if (type == "tsens")
        {
            // sys_clk/40000 Гц — TSENS_OPTIMAL_FREQUENCY из mik32_hal_tsens.h
            // (schemas/tsens.json).
            block["clock_source"] = "sys_clk";
            block["frequency_hz"] = 40000;
        }
        else if (type == "pvd")
        {
            // Середина диапазона порогов, фильтр выключен — нейтральный
            // стартовый дефолт (schemas/pvd.json). NRESETU/NRESETO кодоген
            // не настраивает вообще (полярность не подтверждена).
            block["under_threshold"] = 0;
            block["over_threshold"] = 15;
            block["digital_filter_value"] = 0;
        }
        else if (type == "bus_watchdog")
        {
            // Все три линии выключены по умолчанию — тот же принцип, что у
            // системного WDT (schemas/bus_watchdog.json).
        }
        else if (type == "dma")
        {
            // peripheral_to_memory/crypto/byte/16 байт — минимальная валидная
            // конфигурация (schemas/dma.json, docs/implementations/dma.md).
            block["direction"] = "peripheral_to_memory";
            block["request"] = "crypto";
            block["element_size"] = "byte";
            block["buffer_size_bytes"] = 16;
        }
        return block;
    }
}

ProjectDocument::ProjectDocument()
{
    // Формат верхнего уровня — «Архитектура.md», раздел «Общий пайплайн».
    document_["mcu"] = "MIK32";
    document_["version"] = "2.0";
    document_["peripherals"] = nlohmann::json::array();
}

bool ProjectDocument::load(const std::string& path, std::string& error_out)
{
    const FileReadOutcome file = readUtf8PathFile(path);
    if (!file.success)
    {
        error_out = "Не удалось открыть файл: " + path + file.diagnostic;
        return false;
    }

    nlohmann::json parsed;
    try
    {
        parsed = nlohmann::json::parse(file.content);
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        error_out = std::string("Ошибка разбора JSON: ") + parse_error.what();
        return false;
    }

    if (!parsed.contains("peripherals") || !parsed["peripherals"].is_array())
    {
        error_out = "Файл не содержит массив \"peripherals\"";
        return false;
    }

    document_ = parsed;
    file_path_ = path;
    modified_ = false;
    return true;
}

void ProjectDocument::loadFromDocument(const nlohmann::json& document)
{
    document_ = document;
    file_path_.clear();
    modified_ = false;
}

bool ProjectDocument::save(const std::string& path, std::string& error_out)
{
    // ТЗ §3.2 п.1. Временный файл создаётся В ТОМ ЖЕ каталоге, что и целевой,
    // намеренно: переименование атомарно только в пределах одной файловой
    // системы, а /tmp может оказаться отдельной ФС, и тогда rename выродится
    // в копирование — ровно тот незащищённый случай, который мы и обходим.
    // path приходит из QString::toStdString(), то есть в UTF-8 — на Windows
    // его нельзя отдавать в fs::path напрямую (см. include/utf8_path.h).
    const fs::path target = utf8Path(path);
    // Суффикс дописывается прямо к пути, а не через filename().string():
    // narrow-конверсия имени на Windows шла бы через ANSI-кодировку и теряла
    // символы, которых в ней нет. Результат тот же — <целевой файл>.tmp.
    fs::path temporary = target;
    temporary += ".tmp";

    {
        const FileWriteOutcome write_result = writeUtf8PathFile(temporary.u8string(), document_.dump(2) + "\n");
        if (!write_result.success)
        {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            error_out = "Не удалось записать временный файл: " + temporary.u8string() + write_result.diagnostic;
            return false;
        }
    }

    std::error_code rename_error;
    fs::rename(temporary, target, rename_error);
    if (rename_error)
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        error_out = "Не удалось заменить файл проекта: " + rename_error.message();
        return false;
    }

    file_path_ = path;
    modified_ = false;
    return true;
}

std::vector<int> ProjectDocument::blockIndices(const std::string& type, int instance) const
{
    std::vector<int> indices;
    const nlohmann::json& peripherals = document_["peripherals"];
    for (size_t i = 0; i < peripherals.size(); ++i)
    {
        const nlohmann::json& block = peripherals[i];
        if (block.value("type", std::string()) == type && block.value("instance", -1) == instance)
        {
            indices.push_back(static_cast<int>(i));
        }
    }
    return indices;
}

bool ProjectDocument::isEnabled(const std::string& type, int instance) const
{
    return !blockIndices(type, instance).empty();
}

int ProjectDocument::addBlock(const std::string& type, int instance)
{
    document_["peripherals"].push_back(defaultBlock(type, instance));
    modified_ = true;
    return static_cast<int>(document_["peripherals"].size()) - 1;
}

void ProjectDocument::removeBlocks(const std::string& type, int instance)
{
    nlohmann::json remaining = nlohmann::json::array();
    for (const nlohmann::json& block : document_["peripherals"])
    {
        if (block.value("type", std::string()) == type && block.value("instance", -1) == instance)
        {
            continue;
        }
        remaining.push_back(block);
    }
    document_["peripherals"] = remaining;
    modified_ = true;
}

void ProjectDocument::removeBlockAt(int block_index)
{
    if (block_index < 0 || block_index >= static_cast<int>(document_["peripherals"].size()))
    {
        return;
    }
    document_["peripherals"].erase(document_["peripherals"].begin() + block_index);
    modified_ = true;
}

void ProjectDocument::setField(int block_index, const std::string& field_name, const nlohmann::json& value)
{
    if (block_index < 0 || block_index >= static_cast<int>(document_["peripherals"].size()))
    {
        return;
    }
    document_["peripherals"][block_index][field_name] = value;
    modified_ = true;

    // Пользователь сам указал значение явно — поле больше не unresolved,
    // независимо от того, подтверждён ли блок целиком "плюсиком".
    const auto it = unresolved_fields_.find(block_index);
    if (it != unresolved_fields_.end())
    {
        auto& fields = it->second;
        fields.erase(std::remove_if(fields.begin(), fields.end(),
            [&field_name](const UnresolvedField& field) { return field.field_name == field_name; }),
            fields.end());
        if (fields.empty())
        {
            unresolved_fields_.erase(it);
        }
    }
}

void ProjectDocument::eraseField(int block_index, const std::string& field_name)
{
    if (block_index < 0 || block_index >= static_cast<int>(document_["peripherals"].size()))
    {
        return;
    }
    document_["peripherals"][block_index].erase(field_name);
    modified_ = true;
}

nlohmann::json ProjectDocument::field(int block_index, const std::string& field_name) const
{
    if (block_index < 0 || block_index >= static_cast<int>(document_["peripherals"].size()))
    {
        return nlohmann::json();
    }
    const nlohmann::json& block = document_["peripherals"][block_index];
    return block.contains(field_name) ? block[field_name] : nlohmann::json();
}

nlohmann::json ProjectDocument::projectSetting(const std::string& key) const
{
    // document_ мог быть загружен из файла, созданного до появления этих
    // полей (CMake-тулчейн) — contains() возвращает false, а не кидает,
    // ровно то же соглашение, что и у field() выше для отсутствующего поля.
    return document_.contains(key) ? document_[key] : nlohmann::json();
}

void ProjectDocument::setProjectSetting(const std::string& key, const nlohmann::json& value)
{
    document_[key] = value;
    modified_ = true;
}

std::vector<Diagnostic> ProjectDocument::validate(const ProjectKnowledge& knowledge) const
{
    std::vector<Diagnostic> diagnostics;

    if (document_["peripherals"].empty())
    {
        diagnostics.push_back(Diagnostic{DiagnosticSeverity::Info,
            "Не включён ни один блок периферии — генерировать нечего.", -1});
        return diagnostics;
    }

    // 0. Unresolved-поля reverse-import (feedback_reverse_import_unresolved_
    // fields_policy): пока хоть одно поле не разобрано однозначно,
    // кодогенерация всего проекта заблокирована — не только затронутого
    // блока.
    for (const auto& [block_index, fields] : unresolved_fields_)
    {
        if (isBlockConfirmed(block_index))
        {
            continue;   // пользователь подтвердил блок вручную — не блокировать
        }
        for (const UnresolvedField& field : fields)
        {
            diagnostics.push_back(Diagnostic{DiagnosticSeverity::Error,
                "Поле \"" + field.field_name + "\" не удалось определить при импорте — "
                "укажите значение явно.", block_index});
        }
    }

    // 1. Per-module schema-валидация. parseProjectDocument останавливается на
    // первой ошибке (контракт ParseResult — одно сообщение), поэтому здесь
    // будет ровно один Error, а не список: показывать пользователю мнимую
    // «полноту» списка ошибок, которой ядро не даёт, было бы враньём.
    ParseResult<ProjectConfig> parsed = parseProjectDocument(document_);
    if (!parsed.success)
    {
        // Номер блока восстанавливается из текста ошибки: parseProjectDocument
        // предваряет сообщение блока префиксом "Блок #N".
        int block_index = -1;
        const std::string marker = "Блок #";
        const size_t at = parsed.error.find(marker);
        if (at != std::string::npos)
        {
            const int human_number = std::atoi(parsed.error.c_str() + at + marker.size());
            if (human_number > 0)
            {
                block_index = human_number - 1;  // в сообщении нумерация с 1
            }
        }
        diagnostics.push_back(Diagnostic{DiagnosticSeverity::Error, parsed.error, block_index});
        return diagnostics;  // кросс-модульная проверка на невалидных блоках бессмысленна
    }

    // 2. Кросс-модульные конфликты выводов (ТЗ §3.1.6.2/3.1.6.3 — Ошибка).
    CrossModuleValidationResult cross = validatePinConflictsInDocument(
        document_, knowledge.uart, knowledge.timer16, knowledge.i2c, knowledge.timer32, knowledge.spi);
    if (!cross.success)
    {
        diagnostics.push_back(Diagnostic{DiagnosticSeverity::Error, cross.error, -1});
        return diagnostics;
    }
    for (const PinConflict& conflict : cross.conflicts)
    {
        std::ostringstream message;
        message << "Конфликт вывода GPIO_" << conflict.gpio_instance << " PIN_" << conflict.pin << ": ";
        for (size_t i = 0; i < conflict.conflicting_owners.size(); ++i)
        {
            if (i > 0)
            {
                message << " и ";
            }
            message << conflict.conflicting_owners[i];
        }
        diagnostics.push_back(Diagnostic{DiagnosticSeverity::Error, message.str(), -1});
    }

    // 3. Информационные сообщения. Погрешность Baud Rate ТЗ §3.1.3.2 требует
    // считать; в консольном пайплайне она попадала только в комментарий
    // сгенерированного файла, то есть пользователь видел её ПОСЛЕ генерации.
    // В GUI она показывается до.
    for (const UartConfig& uart : parsed.config->uart_blocks)
    {
        const long divider = knowledge.uart.hsi_hz / uart.baud_rate;
        if (divider == 0)
        {
            continue;
        }
        const double actual = static_cast<double>(knowledge.uart.hsi_hz) / static_cast<double>(divider);
        const double error_percent = (actual - uart.baud_rate) / uart.baud_rate * 100.0;

        std::ostringstream message;
        message.setf(std::ios::fixed);
        message.precision(3);
        message << "UART_" << uart.instance << ": запрошено " << uart.baud_rate
                << " бод, фактически " << actual << " бод, погрешность " << error_percent << "%.";
        // Порог 2% — общепринятая граница устойчивого приёма асинхронного
        // кадра при рассинхроне на 10 битах; ТЗ числа не задаёт, поэтому это
        // предупреждение, а не ошибка, и генерацию оно не блокирует.
        const DiagnosticSeverity severity = (error_percent > 2.0 || error_percent < -2.0)
            ? DiagnosticSeverity::Warning
            : DiagnosticSeverity::Info;
        diagnostics.push_back(Diagnostic{severity, message.str(), -1});
    }

    // 4. Errata MIK32V2 v1.0.1 §7.1 (SPI0, авто-выбор ведомых) — ситуации из
    // Errata по конвенции проекта являются Предупреждением, не Ошибкой (не
    // блокируют генерацию, см. codegen/project/spi_contribution.cpp,
    // errata_7_1_applies), но в отличие от кодогена, где это только
    // комментарий, GUI может показать её ДО генерации — тот же приём, что и
    // с погрешностью Baud Rate выше.
    for (const SpiConfig& spi : parsed.config->spi_blocks)
    {
        if (spi.instance == 0 && spi.mode == "master" && !spi.manual_cs && spi.chip_select == "cs3")
        {
            const std::vector<int> indices = blockIndices("spi", spi.instance);
            const int block_index = indices.empty() ? -1 : indices.front();
            diagnostics.push_back(Diagnostic{DiagnosticSeverity::Warning,
                "SPI_0: Errata п. 7.1 — в автоматическом режиме выбора ведомых вывод "
                "SPI0_n_ss_out_3 не работает и дублирует SPI0_n_ss_out_2. Используйте "
                "manual_cs=true с ручным управлением через GPIO для 4-го ведомого.",
                block_index});
        }
    }

    // 5. OVERNIGHT_FIELD_REFERENCE.md — тот же приём "запрошено vs
    // фактически", что и у Baud Rate выше, для Timer16/Timer32
    // frequency_hz/duty_percent (округление делителей сдвигает фактический
    // результат относительно запрошенного).
    for (const Timer16Config& timer : parsed.config->timer16_blocks)
    {
        if (!timer.has_frequency_duty)
        {
            continue;
        }
        const double actual_freq = 32000000.0 / (static_cast<double>(timer.prescaler) * (static_cast<double>(timer.arr) + 1.0));
        const double actual_duty = (timer.arr > 0 || timer.cmp > 0)
            ? (static_cast<double>(timer.cmp) * 100.0 / (static_cast<double>(timer.arr) + 1.0)) : 0.0;
        std::ostringstream message;
        message.setf(std::ios::fixed);
        message.precision(3);
        message << "Timer16_" << timer.instance << ": запрошено " << timer.requested_frequency_hz
                << " Гц/" << timer.requested_duty_percent << "%, фактически "
                << actual_freq << " Гц/" << actual_duty << "%.";
        diagnostics.push_back(Diagnostic{DiagnosticSeverity::Info, message.str(), -1});
    }
    for (const Timer32Config& timer : parsed.config->timer32_blocks)
    {
        if (!timer.has_frequency)
        {
            continue;
        }
        const double count_freq = 32000000.0 / (static_cast<double>(timer.prescaler) + 1.0);
        const double actual_freq = count_freq / (static_cast<double>(timer.top) + 1.0);
        std::ostringstream message;
        message.setf(std::ios::fixed);
        message.precision(3);
        message << "Timer32_" << timer.instance << ": запрошено " << timer.requested_frequency_hz
                << " Гц, фактически " << actual_freq << " Гц.";
        diagnostics.push_back(Diagnostic{DiagnosticSeverity::Info, message.str(), -1});

        for (const Timer32ChannelConfig& channel : timer.channels)
        {
            if (!channel.has_duty_percent)
            {
                continue;
            }
            const double actual_duty = static_cast<double>(channel.ocr) * 100.0 / (static_cast<double>(timer.top) + 1.0);
            std::ostringstream channel_message;
            channel_message.setf(std::ios::fixed);
            channel_message.precision(3);
            channel_message << "Timer32_" << timer.instance << " канал " << channel.index
                             << ": запрошена скважность " << channel.requested_duty_percent
                             << "%, фактически " << actual_duty << "%.";
            diagnostics.push_back(Diagnostic{DiagnosticSeverity::Info, channel_message.str(), -1});
        }
    }

    return diagnostics;
}

std::vector<std::pair<int, int>> ProjectDocument::claimedPins(
    int block_index, const ProjectKnowledge& knowledge) const
{
    std::vector<std::pair<int, int>> pins;
    if (block_index < 0 || block_index >= static_cast<int>(document_["peripherals"].size()))
    {
        return pins;
    }

    const nlohmann::json& block = document_["peripherals"][block_index];
    const std::string type = block.value("type", std::string());
    const int instance = block.value("instance", -1);

    if (type == "gpio")
    {
        // GPIO — единственный тип, объявляющий выводы явно в конфигурации.
        if (block.contains("pins") && block["pins"].is_array())
        {
            for (const auto& pin : block["pins"])
            {
                if (pin.is_number_integer())
                {
                    pins.emplace_back(instance, pin.get<int>());
                }
            }
        }
    }
    else if (type == "uart")
    {
        auto it = knowledge.uart.instance_fixed_pins.find(instance);
        if (it != knowledge.uart.instance_fixed_pins.end())
        {
            pins.emplace_back(it->second.tx.gpio_instance, it->second.tx.pin);
            pins.emplace_back(it->second.rx.gpio_instance, it->second.rx.pin);
            if (block.value("flow_control", false))
            {
                pins.emplace_back(it->second.rts.gpio_instance, it->second.rts.pin);
                pins.emplace_back(it->second.cts.gpio_instance, it->second.cts.pin);
            }
        }
    }
    else if (type == "timer16")
    {
        // Вывод ШИМ занимается только в режимах, реально порождающих волновую
        // форму — та же логика, что в cross_module_validator.cpp.
        const std::string mode = block.value("mode", std::string());
        auto waveform = knowledge.timer16.mode_waveform_enable.find(mode);
        if (waveform != knowledge.timer16.mode_waveform_enable.end() && waveform->second)
        {
            auto it = knowledge.timer16.instance_fixed_pwm_pin.find(instance);
            if (it != knowledge.timer16.instance_fixed_pwm_pin.end())
            {
                pins.emplace_back(it->second.gpio_instance, it->second.pin);
            }
        }
    }
    else if (type == "i2c")
    {
        auto it = knowledge.i2c.instance_fixed_pins.find(instance);
        if (it != knowledge.i2c.instance_fixed_pins.end())
        {
            for (const I2cKnowledge::FixedPin& pin : it->second)
            {
                pins.emplace_back(pin.gpio_instance, pin.pin);
            }
        }
    }
    else if (type == "spi")
    {
        // SCK/MOSI/MISO/SS_IN — заняты всегда (см. cross_module_validator.cpp).
        auto it = knowledge.spi.instance_fixed_pins.find(instance);
        if (it != knowledge.spi.instance_fixed_pins.end())
        {
            for (const SpiKnowledge::FixedPin& pin : it->second)
            {
                pins.emplace_back(pin.gpio_instance, pin.pin);
            }
        }
        // CS-пин — только для master, тот же порог, что в cross_module_validator.cpp.
        if (block.value("mode", std::string()) == "master" && block.contains("chip_select")
            && block["chip_select"].is_string())
        {
            auto cs_it = knowledge.spi.instance_chip_select_pin.find(instance);
            if (cs_it != knowledge.spi.instance_chip_select_pin.end())
            {
                auto pin_it = cs_it->second.find(block["chip_select"].get<std::string>());
                if (pin_it != cs_it->second.end())
                {
                    pins.emplace_back(pin_it->second.gpio_instance, pin_it->second.pin);
                }
            }
        }
    }
    else if (type == "timer32")
    {
        // Один жёстко закреплённый вывод на канал (не на весь instance) —
        // та же карта instance_channel_fixed_pin, что уже использует
        // cross_module_validator.cpp для поиска конфликтов; поле массива
        // называется "index", не "channel_index" (см. validatePinConflictsInDocument).
        auto instance_map_it = knowledge.timer32.instance_channel_fixed_pin.find(instance);
        if (instance_map_it != knowledge.timer32.instance_channel_fixed_pin.end()
            && block.contains("channels") && block["channels"].is_array())
        {
            for (const auto& channel : block["channels"])
            {
                if (!channel.contains("index") || !channel["index"].is_number_integer())
                {
                    continue;
                }
                auto pin_it = instance_map_it->second.find(channel["index"].get<int>());
                if (pin_it != instance_map_it->second.end())
                {
                    pins.emplace_back(pin_it->second.gpio_instance, pin_it->second.pin);
                }
            }
        }
    }

    return pins;
}

void ProjectDocument::setUnresolvedFields(int block_index, std::vector<UnresolvedField> fields)
{
    confirmed_blocks_.erase(block_index);   // новый импорт блока -> подтверждение не актуально
    if (fields.empty())
    {
        unresolved_fields_.erase(block_index);
        return;
    }
    unresolved_fields_[block_index] = std::move(fields);
}

void ProjectDocument::clearUnresolvedFields()
{
    unresolved_fields_.clear();
    confirmed_blocks_.clear();
}

std::vector<std::string> ProjectDocument::unresolvedFieldNames(int block_index) const
{
    const auto it = unresolved_fields_.find(block_index);
    if (it == unresolved_fields_.end())
    {
        return {};
    }
    std::vector<std::string> names;
    names.reserve(it->second.size());
    for (const UnresolvedField& field : it->second)
    {
        names.push_back(field.field_name);
    }
    return names;
}

std::string ProjectDocument::unresolvedFieldDetail(int block_index, const std::string& field_name) const
{
    const auto it = unresolved_fields_.find(block_index);
    if (it == unresolved_fields_.end())
    {
        return "";
    }
    for (const UnresolvedField& field : it->second)
    {
        if (field.field_name == field_name)
        {
            return field.detail;
        }
    }
    return "";
}

bool ProjectDocument::blockHasUnresolvedFields(int block_index) const
{
    return unresolved_fields_.count(block_index) != 0;
}

bool ProjectDocument::hasAnyUnresolvedFields() const
{
    return !unresolved_fields_.empty();
}

void ProjectDocument::setBlockConfirmed(int block_index, bool confirmed)
{
    if (confirmed)
    {
        confirmed_blocks_.insert(block_index);
    }
    else
    {
        confirmed_blocks_.erase(block_index);
    }
    modified_ = true;
}

bool ProjectDocument::isBlockConfirmed(int block_index) const
{
    return confirmed_blocks_.count(block_index) != 0;
}

bool ProjectDocument::blockBlocksGeneration(int block_index) const
{
    return blockHasUnresolvedFields(block_index) && !isBlockConfirmed(block_index);
}

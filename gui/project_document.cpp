#include "project_document.h"
#include "project_config.h"
#include "cross_module_validator.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
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
            block["demo"] = "scanner";
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
    std::ifstream input(path);
    if (!input.is_open())
    {
        error_out = "Не удалось открыть файл: " + path;
        return false;
    }

    nlohmann::json parsed;
    try
    {
        input >> parsed;
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

bool ProjectDocument::save(const std::string& path, std::string& error_out)
{
    // ТЗ §3.2 п.1. Временный файл создаётся В ТОМ ЖЕ каталоге, что и целевой,
    // намеренно: переименование атомарно только в пределах одной файловой
    // системы, а /tmp может оказаться отдельной ФС, и тогда rename выродится
    // в копирование — ровно тот незащищённый случай, который мы и обходим.
    const fs::path target(path);
    const fs::path temporary = target.parent_path() / (target.filename().string() + ".tmp");

    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output.is_open())
        {
            error_out = "Не удалось создать временный файл: " + temporary.string();
            return false;
        }
        output << document_.dump(2) << "\n";
        if (!output.good())
        {
            output.close();
            std::error_code ignored;
            fs::remove(temporary, ignored);
            error_out = "Ошибка записи во временный файл: " + temporary.string();
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

std::vector<Diagnostic> ProjectDocument::validate(const ProjectKnowledge& knowledge) const
{
    std::vector<Diagnostic> diagnostics;

    if (document_["peripherals"].empty())
    {
        diagnostics.push_back(Diagnostic{DiagnosticSeverity::Info,
            "Не включён ни один блок периферии — генерировать нечего.", -1});
        return diagnostics;
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
        document_, knowledge.uart, knowledge.timer16, knowledge.i2c);
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

    return pins;
}

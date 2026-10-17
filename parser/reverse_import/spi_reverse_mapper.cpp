#include "spi_reverse_mapper.h"

#include "c_literal_parser.h"

#include <regex>

namespace reverse_import
{

namespace
{

std::string trim(const std::string& text)
{
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

// Поле не найдено в блоке вовсе — feedback_reverse_import_unresolved_
// fields_policy: вместо тихого дефолта помечаем unresolved, GUI подсветит
// красным и блокирует кодоген всего проекта.
FieldProvenance synthesizedProvenance(const std::string& json_field)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.editable = false;
    provenance.unresolved = true;
    return provenance;
}

// SPI_0/SPI_1 — базовый указатель периферии, не поле структуры (тот же
// приём, что TIMER32_<N>/TIMER16_<N> у соседних мапперов).
const std::regex kInstanceValue(R"(^SPI_(\d)$)");

std::optional<std::string> mapModeEnum(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v == "HAL_SPI_MODE_MASTER") return "master";
    if (v == "HAL_SPI_MODE_SLAVE") return "slave";
    return std::nullopt;
}

std::optional<std::string> mapClkPhaseEnum(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v == "SPI_PHASE_OFF") return "off";
    if (v == "SPI_PHASE_ON") return "on";
    return std::nullopt;
}

std::optional<std::string> mapClkPolarityEnum(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v == "SPI_POLARITY_LOW") return "low";
    if (v == "SPI_POLARITY_HIGH") return "high";
    return std::nullopt;
}

std::optional<int> mapBaudRateDivEnum(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v == "SPI_BAUDRATE_DIV4") return 4;
    if (v == "SPI_BAUDRATE_DIV8") return 8;
    if (v == "SPI_BAUDRATE_DIV16") return 16;
    if (v == "SPI_BAUDRATE_DIV32") return 32;
    if (v == "SPI_BAUDRATE_DIV64") return 64;
    if (v == "SPI_BAUDRATE_DIV128") return 128;
    if (v == "SPI_BAUDRATE_DIV256") return 256;
    return std::nullopt;
}

std::optional<bool> mapDecoderEnum(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v == "SPI_DECODER_NONE") return false;
    if (v == "SPI_DECODER_USE") return true;
    return std::nullopt;
}

std::optional<bool> mapManualCsEnum(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v == "SPI_MANUALCS_OFF") return false;
    if (v == "SPI_MANUALCS_ON") return true;
    return std::nullopt;
}

const std::regex kChipSelectValue(R"(^SPI_CS_(\d)$)");

std::optional<long long> tryParseIntLiteral(const std::string& raw_value_text)
{
    // См. timer32_channel_reverse_mapper.cpp — тот же общий разборщик вместо
    // "только десятичные".
    return parseCIntegerLiteral(raw_value_text);
}

// Общий шаблон "распознали enum -> кладём строку/bool/int в json_block,
// провенанс editable=true; не распознали -> дефолт + editable=false + note" —
// повторяется для каждого enum-поля SPI, вынесен один раз, а не скопирован
// вручную под каждое поле (в отличие от timer32/timer16-мапперов, где полей
// с этим шаблоном всего 1-2 и вынос не оправдан).
template <typename ValueType>
void mapEnumField(
    MappedBlock& mapped,
    const RawFieldAssignment& assignment,
    const std::string& json_field,
    std::optional<ValueType> (*mapper)(const std::string&),
    const ValueType& fallback)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.location = assignment.location;
    provenance.original_raw_value_text = assignment.raw_value_text;
    provenance.original_full_line_text = assignment.full_line_text;

    const std::optional<ValueType> value = mapper(assignment.raw_value_text);
    if (value.has_value())
    {
        mapped.json_block[json_field] = *value;
        provenance.editable = true;
    }
    else
    {
        mapped.json_block[json_field] = fallback;
        provenance.editable = false;
        provenance.unresolved = true;
        mapped.notes.push_back(
            "не удалось распознать " + assignment.field_path + " = \"" + assignment.raw_value_text +
            "\" — конфигуратор не смог определить " + json_field + ", укажите значение явно.");
    }
    mapped.provenance.push_back(provenance);
}

} // namespace

std::optional<MappedBlock> mapSpiBlock(const RawInitBlock& block)
{
    if (block.handle_type != "SPI_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock mapped;
    mapped.json_block["type"] = "spi";

    bool has_instance = false, has_mode = false, has_clk_phase = false,
         has_clk_polarity = false, has_threshold_tx = false;
    bool is_master = false;  // по умолчанию — до того, как встретим Init.SPI_Mode

    for (const RawFieldAssignment& assignment : block.assignments)
    {
        if (assignment.field_path == "Instance")
        {
            has_instance = true;
            std::smatch match;
            const std::string value = trim(assignment.raw_value_text);

            FieldProvenance provenance;
            provenance.json_field = "instance";
            provenance.location = assignment.location;
            provenance.original_raw_value_text = assignment.raw_value_text;
            provenance.original_full_line_text = assignment.full_line_text;

            if (std::regex_match(value, match, kInstanceValue))
            {
                mapped.json_block["instance"] = std::stoi(match[1].str());
                provenance.editable = true;
            }
            else
            {
                mapped.json_block["instance"] = 0;
                provenance.editable = false;
                provenance.unresolved = true;
                mapped.notes.push_back(
                    "не удалось определить instance из Instance = \"" + assignment.raw_value_text +
                    "\" — конфигуратор не смог определить instance, укажите значение явно.");
            }
            mapped.provenance.push_back(provenance);
            continue;
        }

        if (assignment.field_path == "Init.SPI_Mode")
        {
            has_mode = true;
            mapEnumField<std::string>(mapped, assignment, "mode", mapModeEnum, std::string("master"));
            is_master = (mapped.json_block.value("mode", std::string()) == "master");
            continue;
        }

        if (assignment.field_path == "Init.CLKPhase")
        {
            has_clk_phase = true;
            mapEnumField<std::string>(mapped, assignment, "clk_phase", mapClkPhaseEnum, std::string("off"));
            continue;
        }

        if (assignment.field_path == "Init.CLKPolarity")
        {
            has_clk_polarity = true;
            mapEnumField<std::string>(mapped, assignment, "clk_polarity", mapClkPolarityEnum, std::string("low"));
            continue;
        }

        if (assignment.field_path == "Init.ThresholdTX")
        {
            has_threshold_tx = true;
            FieldProvenance provenance;
            provenance.json_field = "threshold_tx";
            provenance.location = assignment.location;
            provenance.original_raw_value_text = assignment.raw_value_text;
            provenance.original_full_line_text = assignment.full_line_text;

            const std::optional<long long> value = tryParseIntLiteral(assignment.raw_value_text);
            if (value.has_value())
            {
                mapped.json_block["threshold_tx"] = static_cast<int>(*value);
                provenance.editable = true;
            }
            else
            {
                mapped.json_block["threshold_tx"] = 4;
                provenance.editable = false;
                provenance.unresolved = true;
                mapped.notes.push_back(
                    "не удалось распознать Init.ThresholdTX = \"" + assignment.raw_value_text +
                    "\" как число — конфигуратор не смог определить threshold_tx, укажите значение явно.");
            }
            mapped.provenance.push_back(provenance);
            continue;
        }

        // Только для mode=master (schemas/spi.json) — распознаём, только если
        // встретились; для slave-блоков реальный код их и не пишет вовсе.
        if (assignment.field_path == "Init.BaudRateDiv")
        {
            mapEnumField<int>(mapped, assignment, "baud_rate_div", mapBaudRateDivEnum, 64);
            continue;
        }
        if (assignment.field_path == "Init.Decoder")
        {
            mapEnumField<bool>(mapped, assignment, "decoder", mapDecoderEnum, false);
            continue;
        }
        if (assignment.field_path == "Init.ManualCS")
        {
            mapEnumField<bool>(mapped, assignment, "manual_cs", mapManualCsEnum, false);
            continue;
        }
        if (assignment.field_path == "Init.ChipSelect")
        {
            std::smatch match;
            const std::string value = trim(assignment.raw_value_text);

            FieldProvenance provenance;
            provenance.json_field = "chip_select";
            provenance.location = assignment.location;
            provenance.original_raw_value_text = assignment.raw_value_text;
            provenance.original_full_line_text = assignment.full_line_text;

            if (std::regex_match(value, match, kChipSelectValue))
            {
                mapped.json_block["chip_select"] = "cs" + match[1].str();
                provenance.editable = true;
            }
            else
            {
                mapped.json_block["chip_select"] = "cs0";
                provenance.editable = false;
                provenance.unresolved = true;
                mapped.notes.push_back(
                    "не удалось распознать Init.ChipSelect = \"" + assignment.raw_value_text +
                    "\" — конфигуратор не смог определить chip_select, укажите значение явно.");
            }
            mapped.provenance.push_back(provenance);
            continue;
        }

        // Init.CRCPolynomial, Init.CRCEnable и прочие поля вне schemas/spi.json —
        // молча игнорируются, как и у остальных мапперов.
    }

    if (!has_instance)
    {
        mapped.json_block["instance"] = 0;
        mapped.provenance.push_back(synthesizedProvenance("instance"));
        mapped.notes.push_back("в блоке не найдено присваивание Instance — подставлена заглушка instance=0");
    }
    if (!has_mode)
    {
        mapped.json_block["mode"] = "master";
        mapped.provenance.push_back(synthesizedProvenance("mode"));
        mapped.notes.push_back("в блоке не найдено присваивание Init.SPI_Mode — подставлен дефолт \"master\"");
        is_master = true;
    }
    if (!has_clk_phase)
    {
        mapped.json_block["clk_phase"] = "off";
        mapped.provenance.push_back(synthesizedProvenance("clk_phase"));
        mapped.notes.push_back("в блоке не найдено присваивание Init.CLKPhase — подставлен дефолт \"off\"");
    }
    if (!has_clk_polarity)
    {
        mapped.json_block["clk_polarity"] = "low";
        mapped.provenance.push_back(synthesizedProvenance("clk_polarity"));
        mapped.notes.push_back("в блоке не найдено присваивание Init.CLKPolarity — подставлен дефолт \"low\"");
    }
    if (!has_threshold_tx)
    {
        mapped.json_block["threshold_tx"] = 4;
        mapped.provenance.push_back(synthesizedProvenance("threshold_tx"));
        mapped.notes.push_back("в блоке не найдено присваивание Init.ThresholdTX — подставлен дефолт 4");
    }

    // master-only поля: если mode оказался master, но само поле в исходнике
    // не встретилось — синтезируем дефолт (required только для master, но
    // если это ветка master — GUI должен показать что-то валидное сразу).
    // Для slave эти поля НЕ синтезируются вовсе (schemas/spi.json их не
    // требует, а реальный HAL_SPI_Init их для slave и не читает).
    if (is_master)
    {
        if (!mapped.json_block.contains("baud_rate_div"))
        {
            mapped.json_block["baud_rate_div"] = 64;
            mapped.provenance.push_back(synthesizedProvenance("baud_rate_div"));
            mapped.notes.push_back("mode=master, но Init.BaudRateDiv не найден — подставлен дефолт 64");
        }
        if (!mapped.json_block.contains("manual_cs"))
        {
            mapped.json_block["manual_cs"] = false;
            mapped.provenance.push_back(synthesizedProvenance("manual_cs"));
            mapped.notes.push_back("mode=master, но Init.ManualCS не найден — подставлен дефолт false");
        }
        if (!mapped.json_block.contains("decoder"))
        {
            mapped.json_block["decoder"] = false;
            mapped.provenance.push_back(synthesizedProvenance("decoder"));
            mapped.notes.push_back("mode=master, но Init.Decoder не найден — подставлен дефолт false");
        }
        if (!mapped.json_block.contains("chip_select"))
        {
            mapped.json_block["chip_select"] = "cs0";
            mapped.provenance.push_back(synthesizedProvenance("chip_select"));
            mapped.notes.push_back("mode=master, но Init.ChipSelect не найден — подставлен дефолт \"cs0\"");
        }
    }

    return mapped;
}

std::optional<std::string> serializeSpiFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& /*original_raw_value_text*/)
{
    if (field_name == "instance")
    {
        if (!new_value.is_number_integer()) return std::nullopt;
        const int instance = new_value.get<int>();
        if (instance < 0 || instance > 1) return std::nullopt;
        return "SPI_" + std::to_string(instance);
    }
    if (field_name == "mode")
    {
        if (!new_value.is_string()) return std::nullopt;
        const std::string v = new_value.get<std::string>();
        if (v == "master") return "HAL_SPI_MODE_MASTER";
        if (v == "slave") return "HAL_SPI_MODE_SLAVE";
        return std::nullopt;
    }
    if (field_name == "clk_phase")
    {
        if (!new_value.is_string()) return std::nullopt;
        const std::string v = new_value.get<std::string>();
        if (v == "off") return "SPI_PHASE_OFF";
        if (v == "on") return "SPI_PHASE_ON";
        return std::nullopt;
    }
    if (field_name == "clk_polarity")
    {
        if (!new_value.is_string()) return std::nullopt;
        const std::string v = new_value.get<std::string>();
        if (v == "low") return "SPI_POLARITY_LOW";
        if (v == "high") return "SPI_POLARITY_HIGH";
        return std::nullopt;
    }
    if (field_name == "threshold_tx")
    {
        if (!new_value.is_number_integer()) return std::nullopt;
        const int value = new_value.get<int>();
        if (value < 0 || value > 8) return std::nullopt;
        return std::to_string(value);
    }
    if (field_name == "baud_rate_div")
    {
        if (!new_value.is_number_integer()) return std::nullopt;
        switch (new_value.get<int>())
        {
            case 4: return "SPI_BAUDRATE_DIV4";
            case 8: return "SPI_BAUDRATE_DIV8";
            case 16: return "SPI_BAUDRATE_DIV16";
            case 32: return "SPI_BAUDRATE_DIV32";
            case 64: return "SPI_BAUDRATE_DIV64";
            case 128: return "SPI_BAUDRATE_DIV128";
            case 256: return "SPI_BAUDRATE_DIV256";
            default: return std::nullopt;
        }
    }
    if (field_name == "decoder")
    {
        if (!new_value.is_boolean()) return std::nullopt;
        return new_value.get<bool>() ? "SPI_DECODER_USE" : "SPI_DECODER_NONE";
    }
    if (field_name == "manual_cs")
    {
        if (!new_value.is_boolean()) return std::nullopt;
        return new_value.get<bool>() ? "SPI_MANUALCS_ON" : "SPI_MANUALCS_OFF";
    }
    if (field_name == "chip_select")
    {
        if (!new_value.is_string()) return std::nullopt;
        const std::string v = new_value.get<std::string>();
        static const std::regex kCs(R"(^cs([0-3])$)");
        std::smatch match;
        if (std::regex_match(v, match, kCs))
        {
            return "SPI_CS_" + match[1].str();
        }
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace reverse_import

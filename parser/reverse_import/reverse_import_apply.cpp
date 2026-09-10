#include "reverse_import_apply.h"

#include <functional>
#include <set>

#include "uart_reverse_mapper.h"
#include "i2c_reverse_mapper.h"
#include "timer32_reverse_mapper.h"
#include "timer32_channel_reverse_mapper.h"
#include "timer16_reverse_mapper.h"
#include "gpio_reverse_mapper.h"
#include "spi_reverse_mapper.h"

namespace reverse_import
{

namespace
{

// Единая точка диспетчеризации serialize<Type>FieldValue по строке "type"
// json-блока — та же строка, что уже лежит в document["peripherals"][i]["type"]
// (см. schemas/*.json::peripheral_type), поэтому никакого отдельного
// перечисления заводить не нужно.
std::optional<std::string> serializeFieldValue(
    const std::string& peripheral_type,
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text)
{
    if (peripheral_type == "uart")
    {
        return serializeUartFieldValue(field_name, new_value, original_raw_value_text);
    }
    if (peripheral_type == "i2c")
    {
        return serializeI2cFieldValue(field_name, new_value, original_raw_value_text);
    }
    if (peripheral_type == "timer32")
    {
        return serializeTimer32FieldValue(field_name, new_value, original_raw_value_text);
    }
    if (peripheral_type == "gpio")
    {
        return serializeGpioFieldValue(field_name, new_value, original_raw_value_text);
    }
    if (peripheral_type == "timer16")
    {
        return serializeTimer16FieldValue(field_name, new_value, original_raw_value_text);
    }
    if (peripheral_type == "spi")
    {
        return serializeSpiFieldValue(field_name, new_value, original_raw_value_text);
    }
    return std::nullopt;
}

std::set<std::string> allKeys(const nlohmann::json& original_block, const nlohmann::json& current_block)
{
    std::set<std::string> keys;
    for (auto it = original_block.begin(); it != original_block.end(); ++it)
    {
        keys.insert(it.key());
    }
    for (auto it = current_block.begin(); it != current_block.end(); ++it)
    {
        keys.insert(it.key());
    }
    keys.erase("type");
    keys.erase("instance");
    return keys;
}

using Serializer = std::function<std::optional<std::string>(const std::string&, const nlohmann::json&, const std::string&)>;

// Общее ядро "поле изменилось -> найти provenance -> построить LineEditRequest
// или объяснить, почему нельзя" — используется и для top-level полей блока
// (uart baud_rate и т.п.), и для полей ВНУТРИ канала Timer32 ("channels[N].mode"
// и т.п.) — разница только в том, какой json_field искать в provenance, какой
// serializer вызывать и какой человекочитаемый label показать в unpatchable.
void processFieldChange(
    int block_index,
    const std::string& provenance_json_field,
    const std::string& unpatchable_label,
    const nlohmann::json& current_value,
    const std::vector<FieldProvenance>* provenance,
    const Serializer& serializer,
    std::vector<LineEditRequest>& requests,
    ApplyReport& report)
{
    if (provenance == nullptr)
    {
        report.unpatchable.push_back(UnpatchableChange{
            block_index, unpatchable_label, "Для этого блока не сохранён провенанс импорта."});
        return;
    }

    bool any_editable_entry = false;
    bool any_entry_found = false;
    for (const FieldProvenance& entry : *provenance)
    {
        if (entry.json_field != provenance_json_field)
        {
            continue;
        }
        any_entry_found = true;

        if (!entry.editable || entry.location.file.empty())
        {
            continue;
        }
        any_editable_entry = true;

        const std::optional<std::string> new_text =
            serializer(provenance_json_field, current_value, entry.original_raw_value_text);
        if (!new_text.has_value())
        {
            report.unpatchable.push_back(UnpatchableChange{
                block_index, unpatchable_label,
                "Не удалось построить новый текст для строки " + entry.location.file + ":" +
                std::to_string(entry.location.line) + " (несовместимое значение)."});
            continue;
        }

        requests.push_back(LineEditRequest{
            entry.location, entry.original_full_line_text, entry.original_raw_value_text, *new_text});
    }

    if (!any_entry_found)
    {
        report.unpatchable.push_back(UnpatchableChange{
            block_index, unpatchable_label,
            "Поле не встретилось в исходнике при импорте — записать правку обратно "
            "неоткуда (потребовалась бы вставка новой строки, не реализовано)."});
    }
    else if (!any_editable_entry)
    {
        report.unpatchable.push_back(UnpatchableChange{
            block_index, unpatchable_label,
            "Поле помечено нередактируемым при импорте (синтезированный дефолт или "
            "вычисляемое выражение в исходнике, не литерал) — правку нельзя записать "
            "обратно автоматически."});
    }
}

// "channels" — не скалярное поле, а массив объектов (schemas/timer32.json,
// только у peripheral_type=="timer32"). Диффуется ПО ЭЛЕМЕНТАМ и подполям
// каждого элемента, а не как одно неделимое значение — иначе правка ОДНОГО
// канала выглядела бы как "весь массив поменялся" и не находила бы
// provenance ни для одного конкретного поля (провенанс заведён на
// "channels[N].<field>", см. reverse_import.cpp).
void processChannelsChange(
    int block_index,
    const nlohmann::json& original_channels,
    const nlohmann::json& current_channels,
    const std::vector<FieldProvenance>* provenance,
    std::vector<LineEditRequest>& requests,
    ApplyReport& report)
{
    if (original_channels.size() != current_channels.size())
    {
        report.unpatchable.push_back(UnpatchableChange{
            block_index, "channels",
            "Количество каналов Timer32 изменилось (канал добавлен или удалён) — точечное "
            "применение не поддерживает структурные изменения массива channels, нужна полная "
            "кодогенерация для этого блока."});
        return;
    }

    const Serializer channel_serializer =
        [](const std::string& compound_field, const nlohmann::json& value, const std::string& original_text)
    {
        // compound_field приходит как "channels[N].<field>" — serializeTimer32ChannelFieldValue
        // не знает про массив вообще, ему нужно только простое имя поля.
        const std::size_t dot = compound_field.rfind('.');
        const std::string plain_field = (dot == std::string::npos) ? compound_field : compound_field.substr(dot + 1);
        return serializeTimer32ChannelFieldValue(plain_field, value, original_text);
    };

    for (std::size_t ci = 0; ci < original_channels.size(); ++ci)
    {
        const nlohmann::json& original_channel = original_channels[ci];
        const nlohmann::json& current_channel = current_channels[ci];

        for (const std::string& channel_field : allKeys(original_channel, current_channel))
        {
            const nlohmann::json original_value = original_channel.contains(channel_field)
                ? original_channel.at(channel_field) : nlohmann::json();
            const nlohmann::json current_value = current_channel.contains(channel_field)
                ? current_channel.at(channel_field) : nlohmann::json();

            if (original_value == current_value)
            {
                continue;
            }

            const std::string compound_field = "channels[" + std::to_string(ci) + "]." + channel_field;
            processFieldChange(block_index, compound_field, compound_field, current_value,
                                provenance, channel_serializer, requests, report);
        }
    }
}

} // namespace

ApplyReport applyImportedChanges(
    const nlohmann::json& original_peripherals,
    const nlohmann::json& current_peripherals,
    const std::vector<std::vector<FieldProvenance>>& block_provenance)
{
    ApplyReport report;
    std::vector<LineEditRequest> requests;

    const std::size_t original_count = original_peripherals.size();
    const std::size_t current_count = current_peripherals.size();
    const std::size_t common_count = std::min(original_count, current_count);

    for (std::size_t i = 0; i < common_count; ++i)
    {
        const int block_index = static_cast<int>(i);
        const nlohmann::json& original_block = original_peripherals[i];
        const nlohmann::json& current_block = current_peripherals[i];

        const std::string peripheral_type = original_block.value("type", std::string());

        // Структурные правки (тип/instance блока поменялись относительно
        // импорта) обесценивают контекст остальных полей этого блока — какой
        // физический хендл/порт они теперь описывают, уже неоднозначно.
        // Точечное применение для такого блока целиком не поддерживается.
        if (current_block.value("type", std::string()) != peripheral_type
            || current_block.value("instance", -1) != original_block.value("instance", -1))
        {
            report.unpatchable.push_back(UnpatchableChange{
                block_index, "type/instance",
                "Тип или instance блока изменён относительно момента импорта — "
                "точечное применение правок не поддерживает структурные изменения, "
                "нужна полная кодогенерация для этого блока."});
            continue;
        }

        const std::vector<FieldProvenance>* provenance =
            (i < block_provenance.size()) ? &block_provenance[i] : nullptr;

        for (const std::string& field_name : allKeys(original_block, current_block))
        {
            const nlohmann::json original_value = original_block.contains(field_name)
                ? original_block.at(field_name) : nlohmann::json();
            const nlohmann::json current_value = current_block.contains(field_name)
                ? current_block.at(field_name) : nlohmann::json();

            if (original_value == current_value)
            {
                continue;
            }

            if (peripheral_type == "timer32" && field_name == "channels")
            {
                processChannelsChange(block_index, original_value, current_value, provenance,
                                       requests, report);
                continue;
            }

            const Serializer serializer =
                [&peripheral_type](const std::string& f, const nlohmann::json& v, const std::string& raw)
            {
                return serializeFieldValue(peripheral_type, f, v, raw);
            };
            processFieldChange(block_index, field_name, field_name, current_value, provenance,
                                serializer, requests, report);
        }
    }

    for (std::size_t i = common_count; i < original_count; ++i)
    {
        report.unpatchable.push_back(UnpatchableChange{
            static_cast<int>(i), "(блок)",
            "Блок присутствовал при импорте, но удалён в GUI — удаление в исходнике не выполнено, "
            "нужна ручная правка или полная кодогенерация."});
    }
    for (std::size_t i = common_count; i < current_count; ++i)
    {
        report.unpatchable.push_back(UnpatchableChange{
            static_cast<int>(i), "(блок)",
            "Блок добавлен в GUI после импорта — точечное применение новых блоков не "
            "поддерживается, нужна полная кодогенерация."});
    }

    report.patch_results = applyLineEdits(requests);
    return report;
}

} // namespace reverse_import

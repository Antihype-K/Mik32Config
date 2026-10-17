#include "reverse_import.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>

#include "reverse_import_scanner.h"
#include "uart_reverse_mapper.h"
#include "i2c_reverse_mapper.h"
#include "timer32_reverse_mapper.h"
#include "timer32_channel_reverse_mapper.h"
#include "timer16_reverse_mapper.h"
#include "gpio_reverse_mapper.h"
#include "spi_reverse_mapper.h"
#include "adc_reverse_mapper.h"

namespace reverse_import
{

namespace
{

// Ключ группировки GPIO: один физический вызов HAL_GPIO_Init() в исходнике
// становится одним блоком (см. reverse_import_scanner.h — блок режется по
// границам Init-вызовов), но реальный код (mik32_fixed_v3) часто вызывает
// его отдельно для каждого пина, даже когда mode/pull/drive_strength у
// соседних пинов идентичны — визуально это выглядит как 11 "групп" вместо
// 2-3 естественных. Здесь такие блоки схлопываются обратно в одну группу,
// объединяя их "pins" — ровно то, что сделал бы пользователь руками через
// GPIO_PIN_A | GPIO_PIN_B в одном вызове.
// Единственная HAL-функция, которая для данного типа хендла РЕАЛЬНО
// настраивает периферию. Любой другой HAL_*_Init, принимающий тот же хендл,
// — прикладной драйвер поверх уже настроенной периферии (см. место вызова).
// nullptr для типов, у которых канонического Init нет (тогда проверка не
// применяется и поведение остаётся прежним).
const std::string* canonicalInitFunctionFor(const std::string& handle_type)
{
    static const std::unordered_map<std::string, std::string> kCanonical{
        {"USART_HandleTypeDef", "HAL_USART_Init"},
        {"I2C_HandleTypeDef", "HAL_I2C_Init"},
        {"TIMER32_HandleTypeDef", "HAL_Timer32_Init"},
        {"TIMER32_CHANNEL_HandleTypeDef", "HAL_Timer32_Channel_Init"},
        {"Timer16_HandleTypeDef", "HAL_Timer16_Init"},
        {"GPIO_InitTypeDef", "HAL_GPIO_Init"},
        {"SPI_HandleTypeDef", "HAL_SPI_Init"},
        {"ADC_HandleTypeDef", "HAL_ADC_Init"}};

    const auto it = kCanonical.find(handle_type);
    return it == kCanonical.end() ? nullptr : &it->second;
}

using GpioGroupKey = std::tuple<int, std::string, std::string, std::string>;

GpioGroupKey gpioGroupKeyOf(const nlohmann::json& block)
{
    return GpioGroupKey{
        block.value("instance", -1),
        block.value("mode", std::string()),
        block.value("pull", std::string()),
        block.value("drive_strength", std::string())};
}

// Объединяет соседние по конфигурации (не обязательно по позиции в файле)
// GPIO-блоки с одинаковыми instance/mode/pull/drive_strength в один. Не
// трогает никакой другой тип периферии — их блоки переносятся как есть, в
// исходном порядке.
void mergeIdenticalGpioGroups(ImportedProject& result)
{
    // Индексы ИСХОДНОГО document["peripherals"], сгруппированные по ключу,
    // в порядке первого появления каждой группы — гарантирует, что итоговый
    // порядок блоков в документе не "перемешивается" относительно того, что
    // было раньше (первый встреченный представитель группы остаётся на
    // своём месте, остальные "вливаются" в него и пропадают из вывода).
    std::vector<std::size_t> gpio_indices;
    for (std::size_t i = 0; i < result.document["peripherals"].size(); ++i)
    {
        if (result.document["peripherals"][i].value("type", std::string()) == "gpio")
        {
            gpio_indices.push_back(i);
        }
    }

    std::map<GpioGroupKey, std::vector<std::size_t>> groups;
    std::vector<GpioGroupKey> group_order;
    for (std::size_t index : gpio_indices)
    {
        const GpioGroupKey key = gpioGroupKeyOf(result.document["peripherals"][index]);
        if (groups.find(key) == groups.end())
        {
            group_order.push_back(key);
        }
        groups[key].push_back(index);
    }

    // Если ни одна группа не объединяется (у каждой ровно один член),
    // ничего перестраивать не нужно — типичный случай для GPIO_0/GPIO_2 с
    // 1-2 блоками, где мерж почти никогда не требуется.
    const bool any_real_merge = std::any_of(
        group_order.begin(), group_order.end(),
        [&groups](const GpioGroupKey& key) { return groups[key].size() > 1; });
    if (!any_real_merge)
    {
        return;
    }

    nlohmann::json rebuilt_peripherals = nlohmann::json::array();
    std::vector<std::vector<FieldProvenance>> rebuilt_provenance;

    std::set<std::size_t> gpio_index_set(gpio_indices.begin(), gpio_indices.end());
    std::map<std::size_t, std::size_t> first_index_of_group;  // исходный индекс первого члена -> позиция группы в group_order
    for (std::size_t g = 0; g < group_order.size(); ++g)
    {
        first_index_of_group[groups[group_order[g]].front()] = g;
    }

    for (std::size_t i = 0; i < result.document["peripherals"].size(); ++i)
    {
        if (gpio_index_set.count(i) == 0)
        {
            // Не GPIO — переносится без изменений, на своём месте.
            rebuilt_peripherals.push_back(result.document["peripherals"][i]);
            rebuilt_provenance.push_back(result.block_provenance[i]);
            continue;
        }

        const auto first_of_group_it = first_index_of_group.find(i);
        if (first_of_group_it == first_index_of_group.end())
        {
            // Этот индекс — НЕ первый представитель своей группы, значит он
            // уже влит в блок первого представителя (обработан ниже) —
            // просто пропускаем, отдельного блока для него в выводе не будет.
            continue;
        }

        const std::vector<std::size_t>& members = groups[group_order[first_of_group_it->second]];

        nlohmann::json merged_block = result.document["peripherals"][members.front()];
        std::vector<FieldProvenance> merged_provenance = result.block_provenance[members.front()];
        const bool actually_merged = members.size() > 1;

        if (actually_merged)
        {
            std::set<int> pins;
            for (std::size_t member_index : members)
            {
                for (const auto& pin : result.document["peripherals"][member_index]["pins"])
                {
                    if (pin.is_number_integer())
                    {
                        pins.insert(pin.get<int>());
                    }
                }
            }
            merged_block["pins"] = nlohmann::json::array();
            for (int pin : pins)
            {
                merged_block["pins"].push_back(pin);
            }

            // "pins" объединённой группы read-only: у КАЖДОГО исходного
            // члена pins лежал в СВОЕЙ строке (свой вызов HAL_GPIO_Init) —
            // если пользователь поменяет объединённый массив, непонятно, в
            // какую из N строк какое изменение записывать (могло бы задеть
            // несколько несвязанных строк или потерять привязку конкретного
            // пина к его исходной строке) — честнее отказать, чем угадать.
            merged_provenance.clear();
            for (std::size_t member_index : members)
            {
                for (const FieldProvenance& provenance : result.block_provenance[member_index])
                {
                    if (provenance.json_field == "pins")
                    {
                        FieldProvenance read_only = provenance;
                        read_only.editable = false;
                        merged_provenance.push_back(read_only);
                    }
                    else
                    {
                        // mode/pull/drive_strength — ОДНО и то же значение у
                        // ВСЕХ членов группы (это и есть критерий объединения),
                        // поэтому применение новой правки сразу ко всем
                        // исходным строкам безопасно и корректно — оставляем
                        // editable как было (см. reverse_import_apply.cpp,
                        // уже поддерживает несколько provenance-записей на
                        // одно json_field, как у UART instance/parity).
                        merged_provenance.push_back(provenance);
                    }
                }
            }

            result.notes.push_back(
                "GPIO instance " + std::to_string(merged_block.value("instance", -1)) +
                ": " + std::to_string(members.size()) + " одинаково настроенных блоков "
                "(mode/pull/drive_strength) из исходника объединены в одну группу пинов " +
                merged_block["pins"].dump() + " — состав пинов у объединённой группы "
                "нередактируем (каждый пин был в своей отдельной строке исходника), "
                "mode/pull/drive_strength редактируются как обычно.");
        }

        rebuilt_peripherals.push_back(merged_block);
        rebuilt_provenance.push_back(merged_provenance);
    }

    result.document["peripherals"] = rebuilt_peripherals;
    result.block_provenance = rebuilt_provenance;
}

} // namespace

ImportedProject importProjectFromDirectory(const std::string& root_dir)
{
    ImportedProject result;
    result.document["mcu"] = "MIK32";
    result.document["version"] = "2.0";
    result.document["peripherals"] = nlohmann::json::array();

    const ScannedProject scanned = scanDirectoryForHalHandles(root_dir);
    result.files_scanned = scanned.files_scanned;
    result.notes = scanned.unsupported_notes;

    // Отложенные до второго прохода блоки TIMER32_CHANNEL_HandleTypeDef —
    // их родитель (см. timer32_channel_reverse_mapper.h) может встретиться
    // в файле позже, чем сам канал (main.cpp объявляет каналы раньше их
    // Init, но порядок Init-вызовов таймера и канала не гарантирован тем же
    // способом ни архитектурой, ни этим сканером) — поэтому все ОБЫЧНЫЕ
    // блоки сперва собираются целиком, и только потом разбираются каналы.
    std::vector<const RawInitBlock*> channel_blocks;

    // handle_name (как в исходнике, например "htimer32_1") -> индекс блока
    // в result.document["peripherals"] — нужен ТОЛЬКО для присоединения
    // каналов ко второму проходу; для всех остальных типов периферии не
    // используется.
    std::unordered_map<std::string, std::size_t> timer32_block_index_by_handle;

    for (const RawInitBlock& block : scanned.blocks)
    {
        // Сканер считает границей блока ЛЮБОЙ вызов вида HAL_<что-то>_Init,
        // в том числе прикладной драйвер, которому хендл просто передают:
        // HAL_SSD1306_Init(&hi2c0, BRIGHTNESS_FULL) в
        // docs/examples/mik32-examples/SSD1306/src/main.c:33 — это
        // инициализация ЭКРАНА по уже настроенной шине, а не второй
        // конфигурации I2C. Без этой проверки такой вызов давал второй
        // i2c-блок с дублирующимся instance, который parseProjectDocument
        // справедливо считает ошибкой конфига.
        const std::string* canonical_init = canonicalInitFunctionFor(block.handle_type);
        if (canonical_init != nullptr && block.init_function_name != *canonical_init)
        {
            result.notes.push_back(
                "Вызов " + block.init_function_name + "(" + block.handle_name + ") в " +
                block.init_call_location.file + ":" +
                std::to_string(block.init_call_location.line) +
                " не является инициализацией периферии (" + *canonical_init +
                ") — хендл лишь передан прикладной функции, блок не создан.");
            continue;
        }

        if (block.handle_type == "TIMER32_CHANNEL_HandleTypeDef")
        {
            channel_blocks.push_back(&block);
            continue;
        }

        std::optional<MappedBlock> mapped;

        if (block.handle_type == "USART_HandleTypeDef")
        {
            mapped = mapUartBlock(block);
        }
        else if (block.handle_type == "I2C_HandleTypeDef")
        {
            mapped = mapI2cBlock(block);
        }
        else if (block.handle_type == "TIMER32_HandleTypeDef")
        {
            mapped = mapTimer32Block(block);
        }
        else if (block.handle_type == "GPIO_InitTypeDef")
        {
            mapped = mapGpioBlock(block);
        }
        else if (block.handle_type == "Timer16_HandleTypeDef")
        {
            mapped = mapTimer16Block(block);
        }
        else if (block.handle_type == "SPI_HandleTypeDef")
        {
            mapped = mapSpiBlock(block);
        }
        else if (block.handle_type == "ADC_HandleTypeDef")
        {
            mapped = mapAdcBlock(block);
        }
        else
        {
            result.notes.push_back(
                "Неизвестный тип хендла \"" + block.handle_type + "\" (\"" + block.handle_name +
                "\") в " + block.init_call_location.file + ":" +
                std::to_string(block.init_call_location.line) +
                " — это не один из поддерживаемых типов периферии MIK32 (UART/I2C/Timer32/"
                "Timer16/GPIO/SPI/ADC), пропущено.");
            continue;
        }

        if (!mapped.has_value())
        {
            // map<Type>Block вернул nullopt только когда block.handle_type не
            // совпал с тем, что он обрабатывает (защитная проверка внутри
            // маппера) — при диспетчеризации выше по РОВНО этому handle_type
            // это не должно происходить, но на случай расхождения контракта
            // сообщение всё равно полезнее молчания.
            result.notes.push_back(
                "Маппер для \"" + block.handle_type + "\" (\"" + block.handle_name +
                "\") отказался разобрать блок в " + block.init_call_location.file + ":" +
                std::to_string(block.init_call_location.line) + " — пропущено.");
            continue;
        }

        const std::size_t block_index = result.document["peripherals"].size();
        result.document["peripherals"].push_back(mapped->json_block);
        result.block_provenance.push_back(mapped->provenance);
        for (const std::string& note : mapped->notes)
        {
            result.notes.push_back(note);
        }

        if (block.handle_type == "TIMER32_HandleTypeDef")
        {
            timer32_block_index_by_handle[block.handle_name] = block_index;
        }
    }

    // Второй проход: приписать каждый распознанный канал к его родителю по
    // имени (см. timer32_channel_reverse_mapper.h — родитель читается из
    // ТЕКСТА присваивания "TimerInstance = <родитель>.Instance;").
    for (const RawInitBlock* channel_block_ptr : channel_blocks)
    {
        const RawInitBlock& channel_block = *channel_block_ptr;
        const std::optional<MappedTimer32Channel> mapped_channel =
            mapTimer32ChannelBlock(channel_block);

        if (!mapped_channel.has_value())
        {
            // Только защитная проверка handle_type — см. .h. При правильной
            // диспетчеризации (channel_block.handle_type уже проверен выше)
            // сюда попасть не должны, но на случай расхождения контракта.
            result.notes.push_back(
                "Маппер каналов Timer32 отказался разобрать блок \"" + channel_block.handle_name +
                "\" в " + channel_block.init_call_location.file + ":" +
                std::to_string(channel_block.init_call_location.line) + " — пропущено.");
            continue;
        }

        for (const std::string& note : mapped_channel->notes)
        {
            result.notes.push_back(note);
        }

        if (mapped_channel->parent_handle_name.empty())
        {
            // note про ПРИЧИНУ (TimerInstance отсутствует/не распознан) уже
            // добавлена выше из mapped_channel->notes — здесь не дублируем.
            continue;
        }

        const auto parent_it = timer32_block_index_by_handle.find(mapped_channel->parent_handle_name);
        if (parent_it == timer32_block_index_by_handle.end())
        {
            result.notes.push_back(
                "Канал \"" + channel_block.handle_name + "\" ссылается на родительский Timer32 \"" +
                mapped_channel->parent_handle_name + "\", который сканер не нашёл/не смог разобрать "
                "в этом же обходе — канал не присоединён к дереву: " +
                channel_block.init_call_location.file + ":" +
                std::to_string(channel_block.init_call_location.line));
            continue;
        }

        nlohmann::json& parent_block = result.document["peripherals"][parent_it->second];
        if (!parent_block.contains("channels") || !parent_block["channels"].is_array())
        {
            parent_block["channels"] = nlohmann::json::array();
        }
        parent_block["channels"].push_back(mapped_channel->channel_json);
        const std::size_t channel_position = parent_block["channels"].size() - 1;

        // Провенанс канала переносится в провенанс родителя с префиксом
        // "channels[N]." — reverse_import_apply распознаёт этот составной
        // json_field отдельно от top-level полей родителя (см. её .cpp,
        // диффует "channels" по элементам массива, а не как одно целое).
        std::vector<FieldProvenance>& parent_provenance =
            result.block_provenance[parent_it->second];
        const std::string prefix = "channels[" + std::to_string(channel_position) + "].";
        for (const FieldProvenance& channel_field_provenance : mapped_channel->provenance)
        {
            FieldProvenance renamed = channel_field_provenance;
            renamed.json_field = prefix + channel_field_provenance.json_field;
            parent_provenance.push_back(renamed);
        }
    }

    mergeIdenticalGpioGroups(result);

    return result;
}

} // namespace reverse_import

#include "gpio_contribution.h"
#include "contribution_utils.h"
#include <sstream>

ParseResult<PeripheralContribution> buildGpioContribution(
    const std::vector<GpioConfig>& blocks,
    const GpioKnowledge& knowledge,
    bool emit_demo_loop)
{
    if (blocks.empty())
    {
        return ParseResult<PeripheralContribution>{
            false, "buildGpioContribution вызван с пустым списком блоков", std::nullopt};
    }

    const int instance = blocks.front().instance;

    // Хендл и макрос тактирования зависят только от instance, поэтому берутся
    // один раз, а не на каждый блок: knowledge_base/gpio.json,
    // instance_gpio_handle / instance_clock_enable_macro (Датащит, Таблица 2,
    // с.11-12 — ровно три блока GPIO в карте памяти).
    const std::string* clock_macro = findOrNull(knowledge.instance_clock_enable_macro, instance);
    const std::string* gpio_handle = findOrNull(knowledge.instance_gpio_handle, instance);
    if (!clock_macro || !gpio_handle)
    {
        return ParseResult<PeripheralContribution>{
            false, "GpioConfig ссылается на instance, отсутствующий в knowledge_base/gpio.json", std::nullopt};
    }

    PeripheralContribution contribution;
    contribution.label = "GPIO_" + std::to_string(instance);
    contribution.init_order = kInitOrderGpio;
    contribution.init_function_name = "GPIO_" + std::to_string(instance) + "_Init";
    // mik32_hal_pcc.h — ради макроса __HAL_PCC_GPIO_x_CLK_ENABLE, который
    // разворачивается в обращение к регистрам PCC, а не к GPIO.
    contribution.init_c_includes = {"mik32_hal_gpio.h", "mik32_hal_pcc.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};
    // GPIO не имеет хендла: HAL_GPIO_Init принимает указатель GPIO_0/1/2
    // напрямую, поэтому global_definitions/extern_declarations остаются пустыми.

    std::ostringstream init_c;
    init_c << "/*\n"
              " * " << contribution.label << ", блоков конфигурации: " << blocks.size() << ".\n";
    for (const GpioConfig& block : blocks)
    {
        init_c << " *   пины " << buildPinListComment(block.pins)
               << ": mode=" << block.mode << ", pull=" << block.pull
               << ", drive_strength=" << block.drive_strength << ".\n";
    }
    init_c << " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    GPIO_InitTypeDef GPIO_InitStruct = {0};\n"
              "\n"
              // Тактирование порта включается ОДИН раз на функцию, а не на
              // блок: макрос выставляет бит разрешения тактирования всего
              // порта в PCC, повторный вызов не даёт ничего нового.
              "    " << *clock_macro << "();\n";

    for (const GpioConfig& block : blocks)
    {
        const std::string* mode_enum = findOrNull(knowledge.mode_hal_enum, block.mode);
        const std::string* pull_enum = findOrNull(knowledge.pull_hal_enum, block.pull);
        const std::string* ds_enum = findOrNull(knowledge.drive_strength_hal_enum, block.drive_strength);
        if (!mode_enum || !pull_enum || !ds_enum)
        {
            return ParseResult<PeripheralContribution>{
                false,
                "GpioConfig ссылается на mode/pull/drive_strength, отсутствующий в knowledge_base/gpio.json",
                std::nullopt};
        }

        // Один вызов HAL_GPIO_Init на блок — knowledge_base/gpio.json,
        // call_shape.single_call_covers_gpio_and_pad_config: одна и та же
        // функция настраивает и направление в регистрах GPIO, и функцию
        // вывода в PAD_CONFIG.
        init_c << "\n"
                  "    GPIO_InitStruct.Pin = " << buildPinMaskExpression(block.pins) << ";\n"
                  "    GPIO_InitStruct.Mode = " << *mode_enum << ";\n"
                  "    GPIO_InitStruct.Pull = " << *pull_enum << ";\n"
                  "    GPIO_InitStruct.DS = " << *ds_enum << ";\n"
                  "    HAL_GPIO_Init(" << *gpio_handle << ", &GPIO_InitStruct);\n";
    }
    init_c << "}\n";
    contribution.init_function_text = init_c.str();

    // Демо-логика: переключение только тех пинов, что настроены на выход —
    // HAL_GPIO_TogglePin для входа физически ничего не даст (регистр
    // OUTPUT у пина в режиме входа не управляет линией), а для serial/
    // timer_serial/analog вывод вообще замультиплексирован на другую
    // периферию, и запись в него конфликтует с её работой.
    if (emit_demo_loop)
    {
        std::ostringstream loop;
        for (const GpioConfig& block : blocks)
        {
            if (block.mode != "output")
            {
                continue;
            }
            loop << "            HAL_GPIO_TogglePin(" << *gpio_handle << ", "
                 << buildPinMaskExpression(block.pins) << ");\n";
        }
        contribution.loop_body = loop.str();
        if (!contribution.loop_body.empty())
        {
            contribution.wants_loop_delay = true;
            // main.c обращается к HAL_GPIO_TogglePin и к константам
            // GPIO_PIN_* — без этого заголовка имена не объявлены.
            contribution.main_c_includes = {"mik32_hal_gpio.h"};
        }
    }

    return ParseResult<PeripheralContribution>{true, "", contribution};
}

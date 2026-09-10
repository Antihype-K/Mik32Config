#include "gpio_contribution.h"
#include "contribution_utils.h"
#include <sstream>

namespace
{
    // Errata MIK32V2 v1.0.1, §3.1 «Чтение регистра LINE_MUX» — текст цитаты
    // общий и для комментария над HAL_GPIO_InitInterruptLine (ТЗ §3.1.2 п.6),
    // и для комментария над шаблоном обработчика (ТЗ п.7).
    constexpr const char* kErrata31Comment =
        " * Errata п. 3.1 (mik32v2errataV1_0_1_1 v1.0.1): в блоке GPIO_IRQ регистр\n"
        " * LINE_MUX при чтении возвращает только младший байт (каналы 0-1), в\n"
        " * остальных разрядах всегда 0 — операция «чтение-изменение-запись» без\n"
        " * теневой переменной повредила бы конфигурацию каналов 2-7. Обход уже\n"
        " * встроен в саму HAL_GPIO_InitInterruptLine (mik32_hal_gpio.c: теневая\n"
        " * переменная current_irq_line_mux) — этот кодоген её не переизобретает,\n"
        " * а полагается на HAL, поэтому отдельного кода здесь нет, только\n"
        " * предупреждение по ТЗ §3.1.2 п.6.\n";

    std::string irqHandlerFunctionName(int instance)
    {
        return "GPIO_" + std::to_string(instance) + "_IRQHandler_Template";
    }
}

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

    // ТЗ §3.1.2 п.5-6: прерывания через 8 каналов GPIO IRQ (LINE_MUX).
    // Собираем список пар (pin, line, trigger) по ВСЕМ блокам этого
    // instance сразу — HAL_GPIO_InitInterruptLine работает по одному
    // каналу за раз, независимо от того, из какого блока конфигурации
    // пришёл вывод.
    struct ResolvedInterrupt { uint8_t pin; int line; std::string trigger_enum; std::string mux_enum; };
    std::vector<ResolvedInterrupt> resolved_interrupts;
    for (const GpioConfig& block : blocks)
    {
        for (const GpioInterruptConfig& irq : block.interrupts)
        {
            const std::string* trigger_enum = findOrNull(knowledge.trigger_hal_enum, irq.trigger);
            const std::string key = std::to_string(instance) + "_" + std::to_string(irq.pin) + "_" + std::to_string(irq.line);
            const std::string* mux_enum = findOrNull(knowledge.irq_mux_enum, key);
            if (!trigger_enum || !mux_enum)
            {
                return ParseResult<PeripheralContribution>{
                    false,
                    "GPIO_" + std::to_string(instance) + "/PIN_" + std::to_string(irq.pin)
                        + " не может быть источником линии " + std::to_string(irq.line)
                        + " — такой комбинации нет в аппаратной таблице LINE_MUX (knowledge_base/gpio.json, irq_mux_enum)",
                    std::nullopt};
            }
            resolved_interrupts.push_back(ResolvedInterrupt{irq.pin, irq.line, *trigger_enum, *mux_enum});
        }
    }

    if (!resolved_interrupts.empty())
    {
        init_c << "\n"
                  "    /*\n"
               << kErrata31Comment
               << "     *\n"
                  "     * HAL_GPIO_InitInterruptLine не включает саму линию в EPIC (см.\n"
                  "     * mik32_hal_gpio.h) — маскирование HAL_EPIC_MaskLevelSet(HAL_EPIC_GPIO_IRQ_MASK)\n"
                  "     * и HAL_IRQ_EnableInterrupts() остаются на стороне пользователя, как и\n"
                  "     * подключение " << irqHandlerFunctionName(instance) << "() к своему raw_trap_handler.\n"
                  "     */\n";
        for (const ResolvedInterrupt& irq : resolved_interrupts)
        {
            init_c << "    HAL_GPIO_InitInterruptLine(" << irq.mux_enum << ", " << irq.trigger_enum << ");\n";
        }
    }

    init_c << "}\n";
    contribution.init_function_text = init_c.str();

    if (!resolved_interrupts.empty())
    {
        // ТЗ §3.1.2 п.7: сброс прерываний через выражение (1 << N), НЕ через
        // макросы кодировки LINE_MUX/GPIO_MUX_* — те содержат сдвиг
        // <<GPIO_IRQ_LINE_S (4 бита) и как маска регистра CLEAR дали бы
        // неверный бит (Errata п.3.1, подтверждено инженером ПАО «Микрон»).
        std::ostringstream irq_template;
        irq_template << "/*\n"
                     << kErrata31Comment
                     << " */\n"
                        "void " << irqHandlerFunctionName(instance) << "(void)\n"
                        "{\n";
        for (const ResolvedInterrupt& irq : resolved_interrupts)
        {
            irq_template << "    if (HAL_GPIO_LineInterruptState(GPIO_LINE_" << irq.line << "))\n"
                            "    {\n"
                            "        /* USER TODO: GPIO_" << instance << "/PIN_" << static_cast<int>(irq.pin)
                         << " (line " << irq.line << ") сработал. */\n"
                            "\n"
                            "        GPIO_IRQ->CLEAR = (1 << " << irq.line << ");\n"
                            "    }\n";
        }
        irq_template << "}\n";
        contribution.helper_functions_text = irq_template.str();
        contribution.irq_handler_function_name = irqHandlerFunctionName(instance);
    }

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

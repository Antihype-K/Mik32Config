#include "gpio_codegen.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
    // Собирает "GPIO_PIN_0 | GPIO_PIN_3 | ..." — HAL_PinsTypeDef (mik32_hal_gpio.h,
    // строки 33-53) определяет ровно такие имена как enum GPIO_PIN_<n> = (1 << n),
    // побитовое ИЛИ этих значений и есть маска Pin, которую ожидает HAL_GPIO_Init.
    std::string buildPinMaskExpression(const std::vector<uint8_t>& pins)
    {
        std::ostringstream out;
        for (size_t i = 0; i < pins.size(); ++i)
        {
            if (i > 0)
            {
                out << " | ";
            }
            out << "GPIO_PIN_" << static_cast<int>(pins[i]);
        }
        return out.str();
    }

    template <typename ValueT>
    const ValueT* findOrNull(const std::map<std::string, ValueT>& map, const std::string& key)
    {
        auto it = map.find(key);
        return it == map.end() ? nullptr : &it->second;
    }

    template <typename ValueT>
    const ValueT* findOrNull(const std::map<int, ValueT>& map, int key)
    {
        auto it = map.find(key);
        return it == map.end() ? nullptr : &it->second;
    }
}

ParseResult<GeneratedGpioFirmware> generateGpioFirmware(
    const GpioConfig& config,
    const GpioKnowledge& knowledge)
{
    const std::string* mode_enum = findOrNull(knowledge.mode_hal_enum, config.mode);
    const std::string* pull_enum = findOrNull(knowledge.pull_hal_enum, config.pull);
    const std::string* ds_enum = findOrNull(knowledge.drive_strength_hal_enum, config.drive_strength);
    const std::string* clock_macro = findOrNull(knowledge.instance_clock_enable_macro, config.instance);
    const std::string* gpio_handle = findOrNull(knowledge.instance_gpio_handle, config.instance);

    if (!mode_enum || !pull_enum || !ds_enum || !clock_macro || !gpio_handle)
    {
        return ParseResult<GeneratedGpioFirmware>{
            false,
            "GpioConfig ссылается на mode/pull/drive_strength/instance, отсутствующий в knowledge_base/gpio.json",
            std::nullopt};
    }

    GeneratedGpioFirmware firmware;

    firmware.mik32_init_h =
        "#ifndef MIK32_INIT_H\n"
        "#define MIK32_INIT_H\n"
        "\n"
        "void GPIO_Init(void);\n"
        "\n"
        "#endif //MIK32_INIT_H\n";

    // Концепция сгенерированного тела: ровно один вызов HAL_GPIO_Init на блок
    // конфигурации — обоснование в knowledge_base/gpio.json,
    // call_shape.single_call_covers_gpio_and_pad_config, со ссылкой на
    // docs/examples/mik32-hal/peripherals/Source/mik32_hal_gpio.c.
    std::ostringstream init_c;
    init_c << "#include \"mik32_init.h\"\n"
              "#include \"mik32_hal_gpio.h\"\n"
              "#include \"mik32_hal_pcc.h\"\n"
              "\n"
              "/*\n"
              " * Сгенерировано MIK32-Configurator.\n"
              " * GPIO_" << config.instance << ": mode=" << config.mode
           << ", pull=" << config.pull << ", drive_strength=" << config.drive_strength << ".\n"
              " */\n"
              "void GPIO_Init(void)\n"
              "{\n"
              "    GPIO_InitTypeDef GPIO_InitStruct = {0};\n"
              "\n"
              "    " << *clock_macro << "();\n"
              "\n"
              "    GPIO_InitStruct.Pin = " << buildPinMaskExpression(config.pins) << ";\n"
              "    GPIO_InitStruct.Mode = " << *mode_enum << ";\n"
              "    GPIO_InitStruct.Pull = " << *pull_enum << ";\n"
              "    GPIO_InitStruct.DS = " << *ds_enum << ";\n"
              "\n"
              "    HAL_GPIO_Init(" << *gpio_handle << ", &GPIO_InitStruct);\n"
              "}\n";
    firmware.mik32_init_c = init_c.str();

    // SystemClock_Config зашита дефолтным блоком из
    // docs/examples/mik32-examples/HAL_Blink/src/main.c (строки 27-43) —
    // конфигуратор пока не умеет настраивать PCC отдельно (это другой модуль),
    // а без вызова HAL_PCC_Config генерируемый проект не соберёт корректное
    // тактирование вообще. Тело main() после инициализации — пустой цикл:
    // схема gpio.json не описывает поведение (toggle/blink), только
    // электрическую настройку пинов.
    // mik32_hal_gpio.h подключён и здесь (не только в mik32_init.c) по
    // образцу docs/examples/mik32-examples/HAL_Blink/src/main.c (строки 1-2):
    // main.c — то место, где пользователь ожидаемо допишет ручную логику
    // работы с пинами (WritePin/TogglePin/ReadPin) поверх сгенерированной
    // инициализации, и без этого include GPIO_TypeDef/GPIO_PIN_*/HAL_GPIO_*
    // там не объявлены — сборка падает на первой же строчке с этими именами.
    firmware.main_c =
        "#include \"mik32_hal_pcc.h\"\n"
        "#include \"mik32_hal_gpio.h\"\n"
        "#include \"mik32_init.h\"\n"
        "\n"
        "void SystemClock_Config(void);\n"
        "\n"
        "int main()\n"
        "{\n"
        "    SystemClock_Config();\n"
        "\n"
        "    GPIO_Init();\n"
        "\n"
        "    while (1)\n"
        "    {\n"
        "    }\n"
        "}\n"
        "\n"
        "void SystemClock_Config(void)\n"
        "{\n"
        "    PCC_InitTypeDef PCC_OscInit = {0};\n"
        "\n"
        "    PCC_OscInit.OscillatorEnable = PCC_OSCILLATORTYPE_ALL;\n"
        "    PCC_OscInit.FreqMon.OscillatorSystem = PCC_OSCILLATORTYPE_OSC32M;\n"
        "    PCC_OscInit.FreqMon.ForceOscSys = PCC_FORCE_OSC_SYS_UNFIXED;\n"
        "    PCC_OscInit.FreqMon.Force32KClk = PCC_FREQ_MONITOR_SOURCE_OSC32K;\n"
        "    PCC_OscInit.AHBDivider = 0;\n"
        "    PCC_OscInit.APBMDivider = 0;\n"
        "    PCC_OscInit.APBPDivider = 0;\n"
        "    PCC_OscInit.HSI32MCalibrationValue = 128;\n"
        "    PCC_OscInit.LSI32KCalibrationValue = 8;\n"
        "    PCC_OscInit.RTCClockSelection = PCC_RTC_CLOCK_SOURCE_AUTO;\n"
        "    PCC_OscInit.RTCClockCPUSelection = PCC_CPU_RTC_CLOCK_SOURCE_OSC32K;\n"
        "    HAL_PCC_Config(&PCC_OscInit);\n"
        "}\n";

    return ParseResult<GeneratedGpioFirmware>{true, "", firmware};
}

CodegenWriteResult writeGeneratedGpioProject(
    const GeneratedGpioFirmware& firmware,
    const std::string& skeleton_dir,
    const std::string& output_project_dir)
{
    std::error_code fs_error;

    // Скелет (platformio.ini/.vscode/lib/test) копируется только один раз —
    // повторная генерация в тот же каталог не должна затирать файлы,
    // которые пользователь мог отредактировать вручную после первого запуска.
    if (!fs::exists(fs::path(output_project_dir) / "platformio.ini"))
    {
        // fs::copy(from, to, ...) для каталогов не создаёт недостающие
        // родительские каталоги у `to` (только сам конечный каталог) —
        // generated_projects/ в свежем репозитории ещё не существует.
        fs::create_directories(output_project_dir, fs_error);
        if (fs_error)
        {
            return CodegenWriteResult{false, "Не удалось создать " + output_project_dir + ": " + fs_error.message()};
        }

        fs::copy(skeleton_dir, output_project_dir,
                  fs::copy_options::recursive | fs::copy_options::skip_existing,
                  fs_error);
        if (fs_error)
        {
            return CodegenWriteResult{false, "Не удалось скопировать templates/pio_skeleton: " + fs_error.message()};
        }
    }

    const fs::path src_dir = fs::path(output_project_dir) / "src";
    fs::create_directories(src_dir, fs_error);
    if (fs_error)
    {
        return CodegenWriteResult{false, "Не удалось создать каталог src/: " + fs_error.message()};
    }

    const std::pair<const char*, const std::string*> files_to_write[] = {
        {"mik32_init.h", &firmware.mik32_init_h},
        {"mik32_init.c", &firmware.mik32_init_c},
        {"main.c", &firmware.main_c},
    };

    for (const auto& [file_name, content] : files_to_write)
    {
        std::ofstream out(src_dir / file_name, std::ios::trunc);
        if (!out.is_open())
        {
            return CodegenWriteResult{false, std::string("Не удалось записать файл src/") + file_name};
        }
        out << *content;
    }

    return CodegenWriteResult{true, ""};
}

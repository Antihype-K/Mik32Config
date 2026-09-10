#include "freertos_parser.h"
#include <regex>
#include <set>
#include <sstream>

namespace
{
    ParseResult<FreeRtosConfig> makeError(const std::string& message)
    {
        return ParseResult<FreeRtosConfig>{false, message, std::nullopt};
    }

    // Имя объекта ОС становится C-идентификатором напрямую (глобальный хендл
    // TaskHandle_t/QueueHandle_t/... или имя функции задачи) — см.
    // codegen/project/freertos_codegen.cpp. Правило то же, что у компилятора
    // Си: буква/подчёркивание, затем буквы/цифры/подчёркивания.
    const std::regex kIdentifierPattern(R"(^[A-Za-z_][A-Za-z0-9_]*$)");

    const std::set<std::string> kValidHeapSchemes = {"heap_1", "heap_2", "heap_3", "heap_4", "heap_5"};
    const std::set<std::string> kValidSemaphoreKinds = {"binary", "counting"};
    const std::set<std::string> kValidMutexKinds = {"normal", "recursive"};
    const std::set<std::string> kValidTimerKinds = {"one_shot", "periodic"};

    bool isPositiveInt(const nlohmann::json& value)
    {
        return value.is_number_integer() && value.get<long long>() > 0;
    }

    // Читает обязательное строковое поле "name", проверяет формат
    // идентификатора и накапливает его в all_names для последующей проверки
    // глобальной уникальности (все объекты ОС делят один C-неймспейс —
    // grill-решение 2026-09-10: хендлы адресуются по имени напрямую, без
    // отдельного поля "какая задача что использует").
    bool readName(const nlohmann::json& node, const std::string& kind_label,
                  size_t index, std::string& out_name, std::string& error)
    {
        if (!node.contains("name") || !node["name"].is_string())
        {
            error = kind_label + " #" + std::to_string(index + 1) + ": отсутствует строковое поле \"name\"";
            return false;
        }
        out_name = node["name"].get<std::string>();
        if (!std::regex_match(out_name, kIdentifierPattern))
        {
            error = kind_label + " #" + std::to_string(index + 1) + " (\"" + out_name
                + "\"): имя должно быть валидным идентификатором Си "
                  "(буква/подчёркивание, затем буквы/цифры/подчёркивания) — "
                  "оно становится именем глобального хендла/функции в сгенерированном коде";
            return false;
        }
        return true;
    }
}

ParseResult<FreeRtosConfig> parseFreeRtosBlock(const nlohmann::json& freertos_node)
{
    if (!freertos_node.is_object())
    {
        return makeError("Поле \"freertos\" должно быть объектом");
    }

    FreeRtosConfig config;

    if (freertos_node.contains("enabled"))
    {
        if (!freertos_node["enabled"].is_boolean())
        {
            return makeError("freertos.enabled должно быть булевым значением");
        }
        config.enabled = freertos_node["enabled"].get<bool>();
    }

    if (!config.enabled)
    {
        // Выключенный FreeRTOS — остальные поля не читаются и не валидируются
        // вовсе (тот же принцип, что и у необязательных полей toolchain):
        // пользователь мог оставить в файле старый черновик конфигурации
        // задач, временно отключив сам модуль флагом enabled=false.
        return ParseResult<FreeRtosConfig>{true, "", config};
    }

    if (freertos_node.contains("source_mode"))
    {
        if (!freertos_node["source_mode"].is_string())
        {
            return makeError("freertos.source_mode должно быть строкой (\"bundled\", \"network\" или \"local_path\")");
        }
        const std::string mode = freertos_node["source_mode"].get<std::string>();
        if (mode == "bundled")
        {
            config.source_mode = FreeRtosSourceMode::Bundled;
        }
        else if (mode == "network")
        {
            config.source_mode = FreeRtosSourceMode::Network;
        }
        else if (mode == "local_path")
        {
            config.source_mode = FreeRtosSourceMode::LocalPath;
        }
        else
        {
            return makeError("freertos.source_mode содержит недопустимое значение \"" + mode
                + "\" (разрешено bundled, network, local_path)");
        }
    }

    if (freertos_node.contains("source_local_path"))
    {
        if (!freertos_node["source_local_path"].is_string())
        {
            return makeError("freertos.source_local_path должно быть строкой");
        }
        config.source_local_path = freertos_node["source_local_path"].get<std::string>();
    }
    if (config.source_mode == FreeRtosSourceMode::LocalPath && config.source_local_path.empty())
    {
        return makeError("freertos.source_mode=\"local_path\" требует непустого freertos.source_local_path");
    }

    // --- os_config ---------------------------------------------------------
    if (freertos_node.contains("os_config"))
    {
        const nlohmann::json& os = freertos_node["os_config"];
        if (!os.is_object())
        {
            return makeError("freertos.os_config должно быть объектом");
        }
        if (os.contains("tick_rate_hz"))
        {
            if (!isPositiveInt(os["tick_rate_hz"]))
            {
                return makeError("freertos.os_config.tick_rate_hz должно быть положительным целым числом");
            }
            config.os_config.tick_rate_hz = os["tick_rate_hz"].get<int>();
        }
        if (os.contains("max_priorities"))
        {
            if (!isPositiveInt(os["max_priorities"]) || os["max_priorities"].get<int>() < 2)
            {
                return makeError("freertos.os_config.max_priorities должно быть целым числом >= 2 "
                                  "(минимум: приоритет idle-задачи 0 и хотя бы один пользовательский)");
            }
            config.os_config.max_priorities = os["max_priorities"].get<int>();
        }
        if (os.contains("minimal_stack_size_words"))
        {
            if (!isPositiveInt(os["minimal_stack_size_words"]))
            {
                return makeError("freertos.os_config.minimal_stack_size_words должно быть положительным целым числом");
            }
            config.os_config.minimal_stack_size_words = os["minimal_stack_size_words"].get<int>();
        }
        if (os.contains("total_heap_size_bytes"))
        {
            if (!isPositiveInt(os["total_heap_size_bytes"]))
            {
                return makeError("freertos.os_config.total_heap_size_bytes должно быть положительным целым числом");
            }
            config.os_config.total_heap_size_bytes = os["total_heap_size_bytes"].get<int>();
        }
        if (os.contains("use_preemption"))
        {
            if (!os["use_preemption"].is_boolean())
            {
                return makeError("freertos.os_config.use_preemption должно быть булевым значением");
            }
            config.os_config.use_preemption = os["use_preemption"].get<bool>();
        }
        if (os.contains("stack_overflow_check"))
        {
            if (!os["stack_overflow_check"].is_number_integer())
            {
                return makeError("freertos.os_config.stack_overflow_check должно быть целым числом (0, 1 или 2)");
            }
            const int value = os["stack_overflow_check"].get<int>();
            if (value < 0 || value > 2)
            {
                return makeError("freertos.os_config.stack_overflow_check допускает только 0, 1 или 2 "
                                  "(configCHECK_FOR_STACK_OVERFLOW — выключено/метод 1/метод 2)");
            }
            config.os_config.stack_overflow_check = value;
        }
        if (os.contains("heap_scheme"))
        {
            if (!os["heap_scheme"].is_string() || !kValidHeapSchemes.count(os["heap_scheme"].get<std::string>()))
            {
                return makeError("freertos.os_config.heap_scheme должно быть одним из heap_1, heap_2, heap_3, heap_4, heap_5");
            }
            config.os_config.heap_scheme = os["heap_scheme"].get<std::string>();
        }
    }

    std::set<std::string> all_names; // общий C-неймспейс всех объектов ОС

    auto checkUnique = [&](const std::string& name, const std::string& kind_label, size_t index) -> std::string
    {
        if (!all_names.insert(name).second)
        {
            return kind_label + " #" + std::to_string(index + 1) + " (\"" + name
                + "\"): имя уже занято другим объектом FreeRTOS (задачи/очереди/семафоры/"
                  "мьютексы/таймеры делят один общий C-неймспейс — все они становятся "
                  "глобальными хендлами по этому имени)";
        }
        return "";
    };

    // --- tasks ---------------------------------------------------------
    if (freertos_node.contains("tasks"))
    {
        if (!freertos_node["tasks"].is_array())
        {
            return makeError("freertos.tasks должно быть массивом");
        }
        const nlohmann::json& tasks = freertos_node["tasks"];
        for (size_t i = 0; i < tasks.size(); ++i)
        {
            const nlohmann::json& node = tasks[i];
            FreeRtosTaskConfig task;
            std::string error;
            if (!readName(node, "freertos.tasks", i, task.name, error))
            {
                return makeError(error);
            }
            if (std::string dup = checkUnique(task.name, "freertos.tasks", i); !dup.empty())
            {
                return makeError(dup);
            }
            if (!node.contains("priority") || !node["priority"].is_number_integer())
            {
                return makeError("freertos.tasks #" + std::to_string(i + 1) + " (\"" + task.name
                    + "\"): отсутствует целочисленное поле \"priority\"");
            }
            task.priority = node["priority"].get<int>();
            if (task.priority < 0 || task.priority >= config.os_config.max_priorities)
            {
                return makeError("freertos.tasks #" + std::to_string(i + 1) + " (\"" + task.name
                    + "\"): priority должен быть в диапазоне [0, "
                    + std::to_string(config.os_config.max_priorities - 1)
                    + "] (freertos.os_config.max_priorities=" + std::to_string(config.os_config.max_priorities) + ")");
            }
            if (node.contains("stack_size_words"))
            {
                if (!isPositiveInt(node["stack_size_words"]))
                {
                    return makeError("freertos.tasks #" + std::to_string(i + 1) + " (\"" + task.name
                        + "\"): stack_size_words должно быть положительным целым числом");
                }
                task.stack_size_words = node["stack_size_words"].get<int>();
            }
            config.tasks.push_back(task);
        }
    }

    // --- queues --------------------------------------------------------
    if (freertos_node.contains("queues"))
    {
        if (!freertos_node["queues"].is_array())
        {
            return makeError("freertos.queues должно быть массивом");
        }
        const nlohmann::json& queues = freertos_node["queues"];
        for (size_t i = 0; i < queues.size(); ++i)
        {
            const nlohmann::json& node = queues[i];
            FreeRtosQueueConfig queue;
            std::string error;
            if (!readName(node, "freertos.queues", i, queue.name, error))
            {
                return makeError(error);
            }
            if (std::string dup = checkUnique(queue.name, "freertos.queues", i); !dup.empty())
            {
                return makeError(dup);
            }
            if (!node.contains("length") || !isPositiveInt(node["length"]))
            {
                return makeError("freertos.queues #" + std::to_string(i + 1) + " (\"" + queue.name
                    + "\"): length должно быть положительным целым числом");
            }
            queue.length = node["length"].get<int>();
            if (!node.contains("item_size_bytes") || !isPositiveInt(node["item_size_bytes"]))
            {
                return makeError("freertos.queues #" + std::to_string(i + 1) + " (\"" + queue.name
                    + "\"): item_size_bytes должно быть положительным целым числом");
            }
            queue.item_size_bytes = node["item_size_bytes"].get<int>();
            config.queues.push_back(queue);
        }
    }

    // --- semaphores ------------------------------------------------------
    if (freertos_node.contains("semaphores"))
    {
        if (!freertos_node["semaphores"].is_array())
        {
            return makeError("freertos.semaphores должно быть массивом");
        }
        const nlohmann::json& semaphores = freertos_node["semaphores"];
        for (size_t i = 0; i < semaphores.size(); ++i)
        {
            const nlohmann::json& node = semaphores[i];
            FreeRtosSemaphoreConfig sem;
            std::string error;
            if (!readName(node, "freertos.semaphores", i, sem.name, error))
            {
                return makeError(error);
            }
            if (std::string dup = checkUnique(sem.name, "freertos.semaphores", i); !dup.empty())
            {
                return makeError(dup);
            }
            if (!node.contains("kind") || !node["kind"].is_string() || !kValidSemaphoreKinds.count(node["kind"].get<std::string>()))
            {
                return makeError("freertos.semaphores #" + std::to_string(i + 1) + " (\"" + sem.name
                    + "\"): kind должно быть \"binary\" или \"counting\"");
            }
            sem.kind = node["kind"].get<std::string>();
            if (sem.kind == "counting")
            {
                if (!node.contains("max_count") || !isPositiveInt(node["max_count"]))
                {
                    return makeError("freertos.semaphores #" + std::to_string(i + 1) + " (\"" + sem.name
                        + "\"): kind=\"counting\" требует положительного целого max_count");
                }
                sem.max_count = node["max_count"].get<int>();
                sem.initial_count = 0;
                if (node.contains("initial_count"))
                {
                    if (!node["initial_count"].is_number_integer())
                    {
                        return makeError("freertos.semaphores #" + std::to_string(i + 1) + " (\"" + sem.name
                            + "\"): initial_count должно быть целым числом");
                    }
                    sem.initial_count = node["initial_count"].get<int>();
                }
                if (sem.initial_count < 0 || sem.initial_count > sem.max_count)
                {
                    return makeError("freertos.semaphores #" + std::to_string(i + 1) + " (\"" + sem.name
                        + "\"): initial_count должен быть в диапазоне [0, max_count=" + std::to_string(sem.max_count) + "]");
                }
            }
            config.semaphores.push_back(sem);
        }
    }

    // --- mutexes ---------------------------------------------------------
    if (freertos_node.contains("mutexes"))
    {
        if (!freertos_node["mutexes"].is_array())
        {
            return makeError("freertos.mutexes должно быть массивом");
        }
        const nlohmann::json& mutexes = freertos_node["mutexes"];
        for (size_t i = 0; i < mutexes.size(); ++i)
        {
            const nlohmann::json& node = mutexes[i];
            FreeRtosMutexConfig mutex;
            std::string error;
            if (!readName(node, "freertos.mutexes", i, mutex.name, error))
            {
                return makeError(error);
            }
            if (std::string dup = checkUnique(mutex.name, "freertos.mutexes", i); !dup.empty())
            {
                return makeError(dup);
            }
            if (!node.contains("kind") || !node["kind"].is_string() || !kValidMutexKinds.count(node["kind"].get<std::string>()))
            {
                return makeError("freertos.mutexes #" + std::to_string(i + 1) + " (\"" + mutex.name
                    + "\"): kind должно быть \"normal\" или \"recursive\"");
            }
            mutex.kind = node["kind"].get<std::string>();
            config.mutexes.push_back(mutex);
        }
    }

    // --- timers ------------------------------------------------------------
    if (freertos_node.contains("timers"))
    {
        if (!freertos_node["timers"].is_array())
        {
            return makeError("freertos.timers должно быть массивом");
        }
        const nlohmann::json& timers = freertos_node["timers"];
        for (size_t i = 0; i < timers.size(); ++i)
        {
            const nlohmann::json& node = timers[i];
            FreeRtosTimerConfig timer;
            std::string error;
            if (!readName(node, "freertos.timers", i, timer.name, error))
            {
                return makeError(error);
            }
            if (std::string dup = checkUnique(timer.name, "freertos.timers", i); !dup.empty())
            {
                return makeError(dup);
            }
            if (!node.contains("kind") || !node["kind"].is_string() || !kValidTimerKinds.count(node["kind"].get<std::string>()))
            {
                return makeError("freertos.timers #" + std::to_string(i + 1) + " (\"" + timer.name
                    + "\"): kind должно быть \"one_shot\" или \"periodic\"");
            }
            timer.kind = node["kind"].get<std::string>();
            if (!node.contains("period_ms") || !isPositiveInt(node["period_ms"]))
            {
                return makeError("freertos.timers #" + std::to_string(i + 1) + " (\"" + timer.name
                    + "\"): period_ms должно быть положительным целым числом");
            }
            timer.period_ms = node["period_ms"].get<int>();
            config.timers.push_back(timer);
        }
    }

    if (config.tasks.empty())
    {
        return makeError("freertos.enabled=true, но freertos.tasks пуст — планировщику нечего выполнять "
                          "(кроме служебной idle-задачи), генерировать нечего");
    }

    return ParseResult<FreeRtosConfig>{true, "", config};
}

//
// Точка входа GUI (ТЗ §3.1.9). Делает ровно три вещи и ничего больше:
// находит каталог ресурсов конфигуратора, загружает базы знаний, открывает
// главное окно. Вся логика — в MainWindow и панелях.
//
// Почему поиск каталога ресурсов вообще нужен. Консольное приложение
// (src/main.cpp) читает knowledge_base/*.json по относительному пути и требует
// запуска из корня репозитория — для отладочной утилиты это приемлемо. Для
// программы с ярлыком — нет: её запускают из произвольного каталога, и
// «файл не найден» вместо окна был бы единственным, что увидит пользователь.
// Поэтому корень ищется по нескольким кандидатам, а если не найден —
// показывается диалог с объяснением, а не тихое падение.
//
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QString>
#include <QStringList>

#include "main_window.h"
#include "gpio_knowledge.h"
#include "uart_knowledge.h"
#include "timer16_knowledge.h"
#include "i2c_knowledge.h"
#include "timer32_knowledge.h"
#include "spi_knowledge.h"
#include "package_knowledge.h"

namespace
{
    // Признак корня — наличие knowledge_base/package.json: без базы знаний о
    // корпусе GUI бессмысленен (нечем рисовать центральную панель), поэтому
    // именно она и служит маркером.
    bool looksLikeRoot(const QString& directory)
    {
        return QFileInfo::exists(directory + "/knowledge_base/package.json");
    }

    QString findResourceRoot()
    {
        QStringList candidates;
        // 1. Явное указание переменной окружения — последнее слово за
        // пользователем/упаковщиком, поэтому проверяется первым.
        const QByteArray from_environment = qgetenv("MIK32_CONFIGURATOR_ROOT");
        if (!from_environment.isEmpty())
        {
            candidates << QString::fromLocal8Bit(from_environment);
        }
        // 2. Текущий каталог — случай «запустил из корня репозитория», как
        // консольное приложение.
        candidates << QDir::currentPath();
        // 3. Каталог сборки и один-два уровня вверх от него: типичный запуск
        // из build/ или build/<конфигурация>/ прямо из IDE.
        const QString application_dir = QCoreApplication::applicationDirPath();
        candidates << application_dir
                   << application_dir + "/.."
                   << application_dir + "/../..";

        for (const QString& candidate : candidates)
        {
            if (looksLikeRoot(candidate))
            {
                return QDir(candidate).absolutePath();
            }
        }
        return QString();
    }

    // Загрузка баз знаний. Все пять читаются всегда, даже если соответствующей
    // периферии в конфиге не будет: кросс-модульная валидация пинов
    // (ТЗ §3.1.6.2) обходит блоки всех типов сразу и не работает с частично
    // загруженной базой — то же решение, что и в src/main.cpp.
    template <typename T, typename Loader>
    bool loadOne(T& destination, Loader loader, const QString& path, QString& error)
    {
        ParseResult<T> result = loader(path.toStdString());
        if (!result.success)
        {
            error = QString("%1: %2").arg(path, QString::fromStdString(result.error));
            return false;
        }
        destination = *result.config;
        return true;
    }
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    // organizationName обязателен для QSettings (MainWindow::loadRecentProjects/
    // pushRecentProject) — без него Qt не может определить путь к конфигу
    // на диске.
    QCoreApplication::setOrganizationName("MIK32");
    QCoreApplication::setApplicationName("MIK32 Configurator");

    const QString root = findResourceRoot();
    if (root.isEmpty())
    {
        QMessageBox::critical(nullptr, QString::fromUtf8("Не найдены ресурсы конфигуратора"),
            QString::fromUtf8(
                "Не найден каталог с knowledge_base/ и templates/.\n\n"
                "Запустите приложение из корня репозитория конфигуратора или "
                "укажите путь переменной окружения MIK32_CONFIGURATOR_ROOT."));
        return 1;
    }

    ProjectKnowledge knowledge;
    PackageKnowledge package;
    QString error;
    const bool loaded =
        loadOne(knowledge.gpio, loadGpioKnowledge, root + "/knowledge_base/gpio.json", error)
        && loadOne(knowledge.uart, loadUartKnowledge, root + "/knowledge_base/uart.json", error)
        && loadOne(knowledge.timer16, loadTimer16Knowledge, root + "/knowledge_base/timer16.json", error)
        && loadOne(knowledge.i2c, loadI2cKnowledge, root + "/knowledge_base/i2c.json", error)
        && loadOne(knowledge.timer32, loadTimer32Knowledge, root + "/knowledge_base/timer32.json", error)
        && loadOne(knowledge.spi, loadSpiKnowledge, root + "/knowledge_base/spi.json", error)
        && loadOne(package, loadPackageKnowledge, root + "/knowledge_base/package.json", error);
    if (!loaded)
    {
        QMessageBox::critical(nullptr, QString::fromUtf8("Ошибка базы знаний"), error);
        return 1;
    }

    GuiPaths paths;
    paths.skeleton_dir = (root + "/templates/pio_skeleton").toStdString();
    paths.generated_root_dir = (root + "/generated_projects").toStdString();
    paths.cmake_skeleton_dir = (root + "/templates/cmake_skeleton").toStdString();
    paths.sdk_snapshot_dir = (root + "/templates/mik32_sdk_snapshot").toStdString();
    paths.skeleton_dir_freertos = (root + "/templates/pio_skeleton_freertos").toStdString();
    paths.freertos_kernel_snapshot_dir = (root + "/templates/freertos_kernel_snapshot").toStdString();
    paths.cargo_skeleton_dir = (root + "/templates/cargo_skeleton").toStdString();
    paths.rust_hal_snapshot_dir = (root + "/templates/mik32_rust_hal_snapshot").toStdString();

    MainWindow window(knowledge, package, paths);
    window.show();

    // Путь к конфигурации можно передать аргументом — так же, как консольному
    // приложению, чтобы одна и та же команда открывала один и тот же проект.
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() > 1)
    {
        window.openPath(arguments.at(1));
    }

    return application.exec();
}

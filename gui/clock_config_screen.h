//
// Верхнеуровневая вкладка «Clock Configuration» — лайт-версия конфигурации
// тактирования (OVERNIGHT_FIELD_REFERENCE.md). Вынесена из-под
// ProjectManagerScreen в отдельный экран по прямому требованию пользователя:
// в STM32CubeMX Clock Configuration — верхнеуровневая вкладка на одном
// уровне с Pinout & Configuration / Project Manager, а не спрятана внутрь
// Project Manager. MainWindow размещает её в tab_strip_/screens_stack_
// между Config и ProjectManager (см. screen_navigator.h, Screen::ClockConfig).
//
// НЕ визуальная диаграмма тактового дерева, как в STM32CubeMX — только
// форма: источник тактирования + три делителя шин + вычисляемая итоговая
// частота текстом. Полноценная диаграмма сознательно отложена на будущую
// сессию (grill-решение по вопросу 7 ночной сессии 12->13.10.2026).
//
#ifndef MIK32_CONFIGURATOR_CLOCK_CONFIG_SCREEN_H
#define MIK32_CONFIGURATOR_CLOCK_CONFIG_SCREEN_H

#include <QWidget>
#include "json.hpp"

class ProjectDocument;
class QComboBox;
class QSpinBox;
class QLabel;

class ClockConfigScreen : public QWidget
{
    Q_OBJECT

public:
    explicit ClockConfigScreen(QWidget* parent = nullptr);

    // Документ нужен только для записи — тот же приём, что у
    // ProjectManagerScreen::setContext.
    void setContext(ProjectDocument* document);

    // Текущее содержимое document["clock"] (может быть JSON null — тогда
    // подставляются дефолты codegen/project/project_config.h::ClockConfig).
    void setClockSettings(const nlohmann::json& clock_settings);

signals:
    void documentChanged();

private:
    void updateFrequencyLabels();
    void writeClockSetting();

    ProjectDocument* document_ = nullptr;
    bool syncing_ = false;

    QComboBox* oscillator_combo_ = nullptr;
    QSpinBox* ahb_divider_ = nullptr;
    QSpinBox* apbm_divider_ = nullptr;
    QSpinBox* apbp_divider_ = nullptr;
    QLabel* ahb_frequency_label_ = nullptr;
    QLabel* apbm_frequency_label_ = nullptr;
    QLabel* apbp_frequency_label_ = nullptr;
};

#endif //MIK32_CONFIGURATOR_CLOCK_CONFIG_SCREEN_H

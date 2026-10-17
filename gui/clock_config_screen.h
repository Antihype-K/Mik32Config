//
// Верхнеуровневая вкладка «Clock Configuration» — визуальное тактовое дерево
// в стиле STM32CubeMX (OVERNIGHT_FIELD_REFERENCE.md; ранее была лайт-форма,
// полноценная диаграмма отложена на будущую сессию — эта сессия и есть та
// будущая сессия, см. AUDIT_TZ_PERIPHERY_GAPS.md). Вынесена из-под
// ProjectManagerScreen в отдельный экран по прямому требованию пользователя:
// в STM32CubeMX Clock Configuration — верхнеуровневая вкладка на одном
// уровне с Pinout & Configuration / Project Manager, а не спрятана внутрь
// Project Manager. MainWindow размещает её в tab_strip_/screens_stack_
// между Config и ProjectManager (см. screen_navigator.h, Screen::ClockConfig).
//
// В отличие от STM32 у MIK32 нет PLL и нет многоступенчатого мультиплексора —
// два источника 32 МГц (HSI32M/OSC32M, один из них активен как
// oscillator_system) идут через AHB/APB_M/APB_P делители, и два источника
// 32 кГц (LSI32K/OSC32K) идут отдельной цепочкой в RTC. Диаграмма ниже — это
// прямое отображение codegen/project/project_config.h::ClockConfig, а не
// попытка скопировать PLL-дерево STM32 один в один.
//
#ifndef MIK32_CONFIGURATOR_CLOCK_CONFIG_SCREEN_H
#define MIK32_CONFIGURATOR_CLOCK_CONFIG_SCREEN_H

#include <QWidget>
#include "json.hpp"

class ProjectDocument;
class QComboBox;
class QSpinBox;
class QLabel;
class QCheckBox;
class QGroupBox;

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
    // Пересчитывает все частоты в диаграмме и обновляет видимость
    // предупреждений (HSI32M-калибровка < 16 — Errata §12.4.1; попытка
    // выключить активный системный источник).
    void updateComputedState();
    // Собирает весь ClockConfig в один JSON-объект и пишет его целиком —
    // тот же контракт, что был у старой формы (document_->setProjectSetting
    // ("clock", ...) одним блоком, не по полю).
    void writeClockSetting();
    // oscillator_system обязан входить в oscillator_enable (иначе
    // HAL_PCC_Config выключит источник, от которого тактируется ядро,
    // project_config.cpp это проверяет и отказывает) — здесь это
    // гарантируется на уровне UI: при смене системного источника он
    // автоматически отмечается в oscillator_enable, а чекбокс активного
    // источника блокируется от снятия.
    void enforceSystemOscillatorInvariant();

    ProjectDocument* document_ = nullptr;
    bool syncing_ = false;

    // --- Источники 32 МГц (HSI32M/OSC32M) -----------------------------
    QCheckBox* hsi32m_enable_ = nullptr;
    QSpinBox* hsi32m_calibration_ = nullptr;
    QLabel* hsi32m_warning_ = nullptr;
    QCheckBox* osc32m_enable_ = nullptr;

    // --- Источники 32 кГц (LSI32K/OSC32K) ------------------------------
    QCheckBox* lsi32k_enable_ = nullptr;
    QSpinBox* lsi32k_calibration_ = nullptr;
    QCheckBox* osc32k_enable_ = nullptr;

    // --- Мультиплексор системного тактирования -------------------------
    QComboBox* oscillator_combo_ = nullptr;
    QCheckBox* force_osc_sys_ = nullptr;
    QComboBox* force_32k_clk_combo_ = nullptr;

    // --- Делители шин + вычисленные частоты -----------------------------
    QSpinBox* ahb_divider_ = nullptr;
    QSpinBox* apbm_divider_ = nullptr;
    QSpinBox* apbp_divider_ = nullptr;
    QLabel* sysclk_frequency_label_ = nullptr;
    QLabel* ahb_frequency_label_ = nullptr;
    QLabel* apbm_frequency_label_ = nullptr;
    QLabel* apbp_frequency_label_ = nullptr;

    // --- Цепочка RTC -----------------------------------------------------
    QComboBox* rtc_clock_source_combo_ = nullptr;
    QComboBox* rtc_clock_cpu_source_combo_ = nullptr;
    QLabel* rtc_frequency_label_ = nullptr;
};

#endif //MIK32_CONFIGURATOR_CLOCK_CONFIG_SCREEN_H

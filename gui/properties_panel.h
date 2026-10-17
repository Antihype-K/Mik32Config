//
// Правая панель — свойства выбранного блока (ТЗ §3.1.9 п.2, «панель свойств
// выбранного блока»).
//
// Концепция. Форма строится заново на каждую смену выбора, а не прячется
// стопкой заранее созданных страниц: набор полей зависит не только от типа
// блока, но и от значений внутри него (у I2C состав полей разный для master и
// slave, у GPIO число доступных разрядов разное для GPIO_2 и GPIO_0/1).
// Пересборка формы гарантирует, что на экране нет полей, которых в этой
// конфигурации быть не должно.
//
// Допустимые значения выпадающих списков берутся НЕ из головы: они обязаны
// совпадать с наборами, по которым валидируют парсеры (kValidBaudRates и
// прочие в parser/*). Расхождение здесь означало бы, что GUI позволяет ввести
// значение, которое затем отвергнет валидатор — то есть заведомо неисправное
// поведение. Источник каждого набора указан комментарием в .cpp.
//
#ifndef MIK32_CONFIGURATOR_PROPERTIES_PANEL_H
#define MIK32_CONFIGURATOR_PROPERTIES_PANEL_H

#include <QWidget>
#include <QString>
#include "project_document.h"
#include "project_codegen.h"

class QVBoxLayout;

class PropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PropertiesPanel(QWidget* parent = nullptr);

    void setContext(ProjectDocument* document, const ProjectKnowledge* knowledge);

    // type пустой — показать заглушку «блок не выбран».
    void showPeripheral(const QString& type, int instance);

    void refresh();

signals:
    // Изменилось содержимое документа: окно обязано перевалидировать
    // конфигурацию и перекрасить схему корпуса.
    void documentChanged();

private:
    void buildGpioForm(QVBoxLayout* layout, int instance);
    void buildUartForm(QVBoxLayout* layout, int instance);
    void buildTimer16Form(QVBoxLayout* layout, int instance);
    void buildI2cForm(QVBoxLayout* layout, int instance);
    void buildSpiForm(QVBoxLayout* layout, int instance);
    void buildTimer32Form(QVBoxLayout* layout, int instance);
    // Один канал внутри динамического списка Timer32 (0-4 элемента, только
    // instance 1/2). Отдельный метод, а не код прямо в buildTimer32Form:
    // тело на каждый канал самодостаточно перечитывает/переписывает весь
    // массив "channels" из документа (см. properties_panel.cpp), и вынесение
    // его наружу убирает глубокую вложенность лямбд.
    void buildTimer32ChannelRow(QVBoxLayout* channels_layout, int block_index,
                                int channel_position, const nlohmann::json& channel,
                                int instance);
    // Тир 2/3 (AUDIT_TZ_PERIPHERY_GAPS.md) — 11 новых типов, добавленных
    // одним заходом. rtc/crc/eeprom/wdt/adc/crypto/otp/tsens/pvd/bus_watchdog
    // единственные глобальные блоки (instance всегда 0, тот же приём, что
    // уже применяется к "Clock" в дереве) — int instance в сигнатуре не
    // нужен для них, но оставлен у dac (два физических канала, как Timer32/SPI).
    void buildRtcForm(QVBoxLayout* layout);
    void buildCrcForm(QVBoxLayout* layout);
    void buildEepromForm(QVBoxLayout* layout);
    void buildWdtForm(QVBoxLayout* layout);
    void buildDacForm(QVBoxLayout* layout, int instance);
    void buildAdcForm(QVBoxLayout* layout);
    void buildCryptoForm(QVBoxLayout* layout);
    void buildOtpForm(QVBoxLayout* layout);
    void buildTsensForm(QVBoxLayout* layout);
    void buildPvdForm(QVBoxLayout* layout);
    void buildBusWatchdogForm(QVBoxLayout* layout);
    // Standalone DMA-канал (docs/implementations/dma.md) — instance это
    // номер канала (0-3), как у dac выше.
    void buildDmaForm(QVBoxLayout* layout, int instance);
    // FreeRTOS — верхнеуровневый ключ документа "freertos", не peripherals[]
    // (см. комментарий в refresh()). Перенесено сюда из ProjectManagerScreen
    // по требованию пользователя: в STM32CubeMX middleware выбирается в
    // дереве слева, а не вкладкой внутри Project Manager.
    void buildFreertosForm(QVBoxLayout* layout);

    void buildDisabledPlaceholder(QVBoxLayout* layout, int instance);

    void clearForm();

    ProjectDocument* document_ = nullptr;
    const ProjectKnowledge* knowledge_ = nullptr;
    QString type_;
    int instance_ = -1;

    // Индекс редактируемого блока среди блоков этого type+instance.
    // Значим только для gpio: у остальных типов блок всегда один.
    int gpio_block_position_ = 0;

    QWidget* content_ = nullptr;
};

#endif //MIK32_CONFIGURATOR_PROPERTIES_PANEL_H

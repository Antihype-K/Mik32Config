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

//
// Общий механизм /* USER CODE BEGIN <ID> */ ... /* USER CODE END <ID> */ —
// требование §3.1.7.3 официального ТЗ (docs/Приложение_А_Техническое_задание_МИК32.docx):
// пользовательский код между этими маркерами должен сохраняться при
// повторной генерации в существующий проект, а не затираться целиком.
//
// Концепция: сборщик проекта (codegen/project/) всегда выпускает файл с
// маркерами, внутри которых лежит ЕГО дефолтное содержимое (может быть
// пустым, может быть примером кода). Перед фактической записью на диск
// writeGeneratedProject вызывает mergeUserCodeSections(new_content,
// existing_content на диске) — для каждого ID, чьи маркеры нашлись в ОБОИХ
// текстах, содержимое между маркерами берётся из СУЩЕСТВУЮЩЕГО файла
// (то, что реально написал пользователь), а не из свежесгенерированного.
// Если файла ещё не было или в нём нет такого ID — используется дефолт
// генератора как есть.
//
#ifndef MIK32_CONFIGURATOR_USER_CODE_MARKERS_H
#define MIK32_CONFIGURATOR_USER_CODE_MARKERS_H

#include <string>

std::string mergeUserCodeSections(
    const std::string& newly_generated_content,
    const std::string& existing_file_content);

#endif //MIK32_CONFIGURATOR_USER_CODE_MARKERS_H

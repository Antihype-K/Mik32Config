#include "otp_contribution.h"
#include "contribution_utils.h"
#include <sstream>

// Концепция: OTP — единственный глобальный блок (hotp.Instance = OTP,
// mik32_memory_map.h), как RTC/CRC/EEPROM/WDT/CRYPTO. HAL_OTP_Init сама
// вызывает HAL_OTP_MspInit (clock-enable) — безопасный случай.
//
// ВАЖНО: генерирует ТОЛЬКО HAL_OTP_Init (режим чтения). HAL_OTP_Write/
// WriteTestColumn/WriteTestRow/WriteTestBit НИКОГДА не вызываются
// автоматически — OTP (One-Time Programmable) физически необратима: запись
// прожигает биты, откатить её невозможно. Автогенерация кода, который бы
// писал в OTP без явного, осознанного запроса пользователя на каждое
// конкретное значение, недопустима (см. docs/implementations/otp.md).
ParseResult<PeripheralContribution> buildOtpContribution(const OtpConfig& config)
{
    const bool is_three_stages = (config.read_mode == "three_stages");

    PeripheralContribution contribution;
    contribution.label = "OTP";
    contribution.init_order = kInitOrderOtp;
    contribution.init_function_name = "OTP_Init";
    contribution.init_c_includes = {"mik32_hal_otp.h", "mik32_hal_pcc.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};
    contribution.global_definitions = "OTP_HandleTypeDef hotp;\n";

    std::ostringstream init_c;
    init_c << "/*\n"
              " * OTP: read_mode=" << config.read_mode << ".\n"
              " * ВАЖНО: этот кодоген настраивает только режим чтения. HAL_OTP_Write* никогда\n"
              " * не вызывается автоматически — OTP физически необратима (прожигает биты),\n"
              " * запись остаётся полностью на стороне пользователя (см. docs/implementations/otp.md).\n"
              " * Errata п. 5.1 (mik32v2errataV1_0_1_1 v1.0.1): при чтении в 2 этапа данные\n"
              " * читаются по предыдущему адресу — обход (двойное чтение OTPDAT) уже встроен\n"
              " * в HAL_OTP_Read, отдельных действий не требует.\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    hotp.Instance = OTP;\n"
              "    hotp.ReadMode = " << (is_three_stages ? "OPT_READ_3STAGES" : "OPT_READ_2STAGES") << ";\n"
              "\n"
              "    HAL_OTP_Init(&hotp);\n"
              "}\n";
    contribution.init_function_text = init_c.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}

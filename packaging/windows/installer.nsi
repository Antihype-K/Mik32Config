; Установщик MIK32-Configurator для Windows (NSIS).
;
; Собирается из уже готового portable-каталога (см. packaging/windows/README.md
; и build_win64_package.sh) — сам installer.nsi не пересобирает ни движок, ни
; GUI, только упаковывает то, что уже лежит в PACKAGE_DIR, в install.exe с
; ярлыками и деинсталлятором. RequestExecutionLevel user — установка идёт в
; %LOCALAPPDATA%, без запроса прав администратора и без цифровой подписи;
; Windows SmartScreen всё равно покажет предупреждение «Windows защитила ваш
; ПК» при первом запуске (нормально для неподписанных .exe) — обходится через
; «Подробнее» -> «Выполнить всё равно».

!define PRODUCT_NAME "MIK32-Configurator"
!define PRODUCT_PUBLISHER "MIK32-Configurator project"
!ifndef PRODUCT_VERSION
    !define PRODUCT_VERSION "0.0.0-dev"
!endif
!ifndef PACKAGE_DIR
    !define PACKAGE_DIR "..\..\..\mik32-win64-package\MIK32-Configurator-win64"
!endif
!ifndef OUTPUT_FILE
    !define OUTPUT_FILE "MIK32-Configurator-install.exe"
!endif

Name "${PRODUCT_NAME}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\${PRODUCT_NAME}"
RequestExecutionLevel user
SetCompressor /SOLID lzma

!include "MUI2.nsh"

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\MIK32-Configurator-GUI.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Запустить MIK32-Configurator"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Russian"

Section "MIK32-Configurator" SEC_MAIN
    SectionIn RO
    SetOutPath "$INSTDIR"
    ; /r — весь дерево пакета целиком (exe, dll, knowledge_base/, schemas/,
    ; templates/, examples/, platforms/) одной командой, без перечисления
    ; файлов вручную здесь: список файлов пакета уже определён и проверен
    ; отдельно (packaging/windows/build_win64_package.sh), дублировать его
    ; здесь построчно значило бы держать один и тот же список синхронным в
    ; двух местах.
    File /r "${PACKAGE_DIR}\*.*"

    WriteUninstaller "$INSTDIR\Uninstall.exe"

    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\MIK32-Configurator-GUI.exe"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Удалить ${PRODUCT_NAME}.lnk" "$INSTDIR\Uninstall.exe"
    CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\MIK32-Configurator-GUI.exe"

    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "NoRepair" 1
SectionEnd

Section "Uninstall"
    Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\Удалить ${PRODUCT_NAME}.lnk"
    RMDir "$SMPROGRAMS\${PRODUCT_NAME}"

    ; generated_projects — рабочие файлы пользователя (сгенерированные
    ; прошивки), деинсталлятор их не трогает, если они оказались внутри
    ; $INSTDIR: RMDir без /r ниже удаляет только то, что установил инсталлятор
    ; (перечислено явно), после чего RMDir "$INSTDIR" без /r откажется
    ; удалить непустую папку — это осознанная защита, а не недосмотр.
    RMDir /r "$INSTDIR\knowledge_base"
    RMDir /r "$INSTDIR\schemas"
    RMDir /r "$INSTDIR\templates"
    RMDir /r "$INSTDIR\examples"
    RMDir /r "$INSTDIR\platforms"
    Delete "$INSTDIR\*.exe"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
SectionEnd

; ClashWin NSIS 安装脚本（1:1 对照 Clash Mini 的 src-tauri/packages/windows/installer.nsi）
; 安装形态与 Mini 一致：perMachine（Program Files，管理员权限），Start Menu + 桌面快捷方式，
; 卸载器写入 HKLM Uninstall，服务名固定为 clash_verge_service（与 Clash Mini 共用同一特权服务）。
; 版本号只有一个来源：下面的 VERSION（可由 build_installer.ps1 以 /DVERSION=... 覆盖）。
;
; 用法：
;   构建： installer\build_installer.ps1 -Version 0.1.0
;          （或 cmake --build build --config Release --target installer）
;   静默安装： ClashWin_0.1.0_x64-setup.exe /S [/KEEP] [/DIR=<目录>]
;   静默卸载： <安装目录>\uninstall.exe /S [/DELAPPDATA]
Unicode true
ManifestDPIAware true
ManifestDPIAwareness PerMonitorV2
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "WinVer.nsh"
!include "x64.nsh"

; 卸载器签名：NSIS 3.11 的 !uninstfinalize 不注入输出路径/环境变量（实测），
; 但卸载器 blob 会以 %TEMP%\nstXXXX.tmp 在编译期落盘；钩子脚本就地签名后，
; NSIS 再把它嵌入安装包，安装后的 uninstall.exe 即带有效 Authenticode 签名。
; 脚本由 build_installer.ps1 以管理员身份调用，未定义该宏时跳过（不影响编译）。
!ifdef UNINST_SIGN_SCRIPT
  !uninstfinalize 'powershell -NoProfile -ExecutionPolicy Bypass -File "${UNINST_SIGN_SCRIPT}" -LogFile "${UNINST_SIGN_LOG}"'
!endif

; 可选：Tauri 的 nsis_tauri_utils 插件（仅用于以当前用户身份启动应用；缺失时退回 ExecShell）
!ifdef TAURI_UTILS_PLUGIN_DIR
  !addplugindir "${TAURI_UTILS_PLUGIN_DIR}"
  !define HAVE_TAURI_UTILS
!endif

; ---------- 可覆盖定义（构建脚本传入绝对路径） ----------
!ifndef VERSION
  !define VERSION "0.1.0"
!endif
!ifndef STAGEDIR
  !define STAGEDIR "stage"
!endif
!ifndef OUTFILE
  !define OUTFILE "..\..\dist\ClashWin_${VERSION}_x64-setup.exe"
!endif
!ifndef ICONFILE
  !define ICONFILE "..\resources\icons\icon.ico"
!endif
!ifndef LICENSEFILE
  !define LICENSEFILE "..\LICENSE"
!endif

; ---------- 产品常量 ----------
!define PRODUCTNAME "ClashWin"
!define MANUFACTURER "Clash Win"
!define COPYRIGHT "GNU General Public License v3.0 (GPL-3.0-only)"
!define APPID "io.github.clash-win.clash-win"
!define MAINBINARY "clashwin.exe"
!define SERVICE_NAME "clash_verge_service"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCTNAME}"
!define MANUPRODUCTKEY "Software\${MANUFACTURER}\${PRODUCTNAME}"
!define STARTMENUFOLDER "ClashWin"

!if ! /fileexists "${STAGEDIR}\${MAINBINARY}"
  !error "缺少 ${STAGEDIR}\${MAINBINARY}：请先运行 installer\build_installer.ps1 暂存 Release 产物。"
!endif
!if ! /fileexists "${STAGEDIR}\sidecar\mini-mihomo.exe"
  !error "缺少 ${STAGEDIR}\sidecar\mini-mihomo.exe：请先运行 installer\build_installer.ps1。"
!endif
!if ! /fileexists "${STAGEDIR}\resources\clash-verge-service-install.exe"
  !error "缺少 ${STAGEDIR}\resources\clash-verge-service-install.exe：请先运行 installer\build_installer.ps1。"
!endif

Name "${PRODUCTNAME} ${VERSION}"
BrandingText "${COPYRIGHT}"
OutFile "${OUTFILE}"
RequestExecutionLevel admin
InstallDir "$PROGRAMFILES64\${PRODUCTNAME}"
InstallDirRegKey HKLM "${MANUPRODUCTKEY}" ""

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${PRODUCTNAME}"
VIAddVersionKey "CompanyName" "${MANUFACTURER}"
VIAddVersionKey "FileDescription" "ClashWin - 原生 Windows Clash 客户端"
VIAddVersionKey "LegalCopyright" "${COPYRIGHT}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"

!define MUI_ABORTWARNING
!define MUI_ICON "${ICONFILE}"
!define MUI_UNICON "${ICONFILE}"
!define MUI_LANGDLL_REGISTRY_ROOT "HKLM"
!define MUI_LANGDLL_REGISTRY_KEY "${MANUPRODUCTKEY}"
!define MUI_LANGDLL_REGISTRY_VALUENAME "Installer Language"

Var KeepSettingsState
Var KeepSettingsCheckbox
Var HasExistingSettings
Var ServiceExists
Var DeleteAppDataCheckbox
Var DeleteAppDataState
Var AppStartMenuFolder

; 语言与自定义字符串在 MUI 页面宏之后声明（见“卸载页面”之后）

; ---------- 安装页面 ----------
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSEFILE}"
!insertmacro MUI_PAGE_DIRECTORY
Page custom SettingsPage SettingsPageLeave
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "${STARTMENUFOLDER}"
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKLM"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "${MANUPRODUCTKEY}"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "StartMenuFolder"
!insertmacro MUI_PAGE_STARTMENU Application $AppStartMenuFolder
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION RunMainBinary
!define MUI_FINISHPAGE_RUN_TEXT "$(^Run)"
!insertmacro MUI_PAGE_FINISH

; ---------- 卸载页面 ----------
UninstPage custom un.ConfirmPage un.ConfirmLeave
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; ---------- 语言（MUI 要求：语言表必须在页面宏之后插入） ----------
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Russian"

LangString STR_SETTINGS_TITLE ${LANG_SIMPCHINESE} "安装选项"
LangString STR_SETTINGS_TITLE ${LANG_ENGLISH} "Install Options"
LangString STR_SETTINGS_TITLE ${LANG_RUSSIAN} "Параметры установки"
LangString STR_SETTINGS_SUBTITLE ${LANG_SIMPCHINESE} "选择是否保留已有配置"
LangString STR_SETTINGS_SUBTITLE ${LANG_ENGLISH} "Choose whether to keep existing settings"
LangString STR_SETTINGS_SUBTITLE ${LANG_RUSSIAN} "Выберите, сохранять ли существующие настройки"
LangString STR_SETTINGS_TEXT ${LANG_SIMPCHINESE} "【保留原设定】：保留现有的订阅、节点与全部应用设置（升级安装推荐勾选）。$\r$\n取消勾选则执行全新安装：现有配置会先备份为 ${APPID}.bak-N，然后以默认设置启动。"
LangString STR_SETTINGS_TEXT ${LANG_ENGLISH} "Keep existing settings: preserves current subscriptions, selected nodes and all preferences (recommended when upgrading).$\r$\nIf unchecked, a clean install is performed: the existing config is backed up to ${APPID}.bak-N first."
LangString STR_SETTINGS_TEXT ${LANG_RUSSIAN} "【Сохранить настройки】：сохранить текущие подписки, узлы и все параметры приложения (рекомендуется при обновлении).$\r$\nЕсли снять флажок, выполняется чистая установка: существующая конфигурация сначала будет сохранена в ${APPID}.bak-N, затем приложение запустится с настройками по умолчанию."
LangString STR_KEEP_SETTINGS ${LANG_SIMPCHINESE} "保留原设定（保留现有订阅、节点与设置）"
LangString STR_KEEP_SETTINGS ${LANG_ENGLISH} "Keep existing settings (subscriptions, nodes and preferences)"
LangString STR_KEEP_SETTINGS ${LANG_RUSSIAN} "Сохранить настройки (подписки, узлы и параметры)"
LangString STR_NO_SETTINGS ${LANG_SIMPCHINESE} "未检测到现有配置，本次为全新安装。"
LangString STR_NO_SETTINGS ${LANG_ENGLISH} "No existing configuration detected; this will be a clean install."
LangString STR_NO_SETTINGS ${LANG_RUSSIAN} "Существующая конфигурация не найдена; будет выполнена чистая установка."
LangString STR_UNINST_TITLE ${LANG_SIMPCHINESE} "卸载 ClashWin"
LangString STR_UNINST_TITLE ${LANG_ENGLISH} "Uninstall ClashWin"
LangString STR_UNINST_TITLE ${LANG_RUSSIAN} "Удаление ClashWin"
LangString STR_UNINST_SUBTITLE ${LANG_SIMPCHINESE} "确认卸载选项"
LangString STR_UNINST_SUBTITLE ${LANG_ENGLISH} "Confirm uninstall options"
LangString STR_UNINST_SUBTITLE ${LANG_RUSSIAN} "Подтверждение параметров удаления"
LangString STR_UNINST_TEXT ${LANG_SIMPCHINESE} "将删除 ClashWin 程序文件、快捷方式与注册表项，并停止/移除本安装所创建的 clash_verge_service 服务。$\r$\n配置数据默认保留，以便日后重装时继续使用。"
LangString STR_UNINST_TEXT ${LANG_ENGLISH} "This removes ClashWin files, shortcuts and registry entries, and stops/removes the clash_verge_service installed by this copy.$\r$\nConfiguration data is kept by default so it can be reused later."
LangString STR_UNINST_TEXT ${LANG_RUSSIAN} "Будут удалены файлы ClashWin, ярлыки и записи реестра, а также остановлена и удалена служба clash_verge_service, созданная этой установкой.$\r$\nДанные конфигурации по умолчанию сохраняются для дальнейшего использования."
LangString STR_DELETE_APPDATA ${LANG_SIMPCHINESE} "同时删除配置数据（订阅、节点与设置）"
LangString STR_DELETE_APPDATA ${LANG_ENGLISH} "Also delete configuration data (subscriptions, nodes and settings)"
LangString STR_DELETE_APPDATA ${LANG_RUSSIAN} "Также удалить данные конфигурации (подписки, узлы и параметры)"
LangString STR_UPGRADE ${LANG_SIMPCHINESE} "检测到已安装版本，将执行升级安装（保留程序文件覆盖，配置按下方选项处理）。"
LangString STR_UPGRADE ${LANG_ENGLISH} "An existing installation was detected; performing an upgrade (files are replaced, settings follow the option below)."
LangString STR_UPGRADE ${LANG_RUSSIAN} "Обнаружена установленная версия; выполняется обновление (файлы заменяются, настройки обрабатываются по параметру ниже)."
LangString STR_SERVICE_FAIL ${LANG_SIMPCHINESE} "clash_verge_service 服务安装失败。TUN 模式可能不可用，可稍后在应用内修复。"
LangString STR_SERVICE_FAIL ${LANG_ENGLISH} "Failed to install clash_verge_service. TUN mode may be unavailable; you can retry from the app later."
LangString STR_SERVICE_FAIL ${LANG_RUSSIAN} "Не удалось установить службу clash_verge_service. Режим TUN может быть недоступен; повторите попытку в приложении позже."
LangString STR_NEED_WIN10 ${LANG_SIMPCHINESE} "ClashWin 需要 Windows 10 或更高版本。"
LangString STR_NEED_WIN10 ${LANG_ENGLISH} "ClashWin requires Windows 10 or later."
LangString STR_NEED_WIN10 ${LANG_RUSSIAN} "ClashWin требует Windows 10 или более поздней версии."

; ---------- 初始化 ----------
Function .onInit
  SetShellVarContext all
  SetRegView 64

  ${IfNot} ${AtLeastWin10}
    IfSilent +2 0
    MessageBox MB_OK|MB_ICONSTOP "$(STR_NEED_WIN10)"
    Abort
  ${EndIf}

  !insertmacro MUI_LANGDLL_DISPLAY

  ; 沿用上次安装位置（Mini 的 RestorePreviousInstallLocation）
  ReadRegStr $0 HKLM "${MANUPRODUCTKEY}" ""
  ${If} $0 != ""
    StrCpy $INSTDIR $0
  ${EndIf}

  ; 升级提示（仅界面，不阻塞；静默安装时跳过）
  ReadRegStr $0 HKLM "${UNINSTKEY}" "DisplayVersion"
  ${If} $0 != ""
    IfSilent +2 0
    MessageBox MB_OK|MB_ICONINFORMATION "$(STR_UPGRADE)"
  ${EndIf}

  ; 检测现有配置目录（%APPDATA%\io.github.clash-win.clash-win）
  StrCpy $HasExistingSettings 0
  IfFileExists "$APPDATA\${APPID}\*.*" 0 +2
    StrCpy $HasExistingSettings 1

  ; 静默安装默认“全新安装”；/KEEP 可保留原设定
  StrCpy $KeepSettingsState 0
  ${GetOptions} $CMDLINE "/KEEP" $0
  ${IfNot} ${Errors}
    StrCpy $KeepSettingsState 1
  ${EndIf}
FunctionEnd

Function RunMainBinary
  !ifdef HAVE_TAURI_UTILS
    nsis_tauri_utils::RunAsUser "$INSTDIR\${MAINBINARY}" ""
  !else
    ExecShell "open" "$INSTDIR\${MAINBINARY}"
  !endif
FunctionEnd

; ---------- 保留原设定页面 ----------
Function SettingsPage
  !insertmacro MUI_HEADER_TEXT "$(STR_SETTINGS_TITLE)" "$(STR_SETTINGS_SUBTITLE)"
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0 0 100% 40u "$(STR_SETTINGS_TEXT)"
  Pop $0
  ${NSD_CreateCheckbox} 0 52u 100% 12u "$(STR_KEEP_SETTINGS)"
  Pop $KeepSettingsCheckbox
  ${If} $HasExistingSettings == 0
    EnableWindow $KeepSettingsCheckbox 0
    ${NSD_CreateLabel} 0 70u 100% 24u "$(STR_NO_SETTINGS)"
    Pop $0
  ${EndIf}
  ${If} $KeepSettingsState == 1
    ${NSD_Check} $KeepSettingsCheckbox
  ${EndIf}

  nsDialogs::Show
FunctionEnd

Function SettingsPageLeave
  ${NSD_GetState} $KeepSettingsCheckbox $KeepSettingsState
FunctionEnd

; ---------- 卸载确认页面 ----------
Function un.ConfirmPage
  !insertmacro MUI_HEADER_TEXT "$(STR_UNINST_TITLE)" "$(STR_UNINST_SUBTITLE)"
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  ${NSD_CreateLabel} 0 0 100% 44u "$(STR_UNINST_TEXT)"
  Pop $0
  ${NSD_CreateCheckbox} 0 56u 100% 12u "$(STR_DELETE_APPDATA)"
  Pop $DeleteAppDataCheckbox
  ${If} $DeleteAppDataState == 1
    ${NSD_Check} $DeleteAppDataCheckbox
  ${EndIf}
  nsDialogs::Show
FunctionEnd

Function un.ConfirmLeave
  ${NSD_GetState} $DeleteAppDataCheckbox $DeleteAppDataState
FunctionEnd

Function un.onInit
  SetShellVarContext all
  SetRegView 64
  !insertmacro MUI_UNGETLANGUAGE
  StrCpy $DeleteAppDataState 0
  ${GetOptions} $CMDLINE "/DELAPPDATA" $0
  ${IfNot} ${Errors}
    StrCpy $DeleteAppDataState 1
  ${EndIf}
FunctionEnd

; ---------- 安装 ----------
Section "Install"
  SetRegView 64
  SetOutPath $INSTDIR

  Call KillAppProcesses

  ; “保留原设定”未勾选 => 现有配置先备份到 ${APPID}.bak-N，再以默认设置启动
  ${If} $KeepSettingsState != 1
  ${AndIf} $HasExistingSettings == 1
    Call BackupSettingsDir
  ${EndIf}

  File "${STAGEDIR}\${MAINBINARY}"

  SetOutPath "$INSTDIR\sidecar"
  File "${STAGEDIR}\sidecar\*.exe"
  SetOutPath "$INSTDIR\resources"
  File /r "${STAGEDIR}\resources\*.*"
  SetOutPath "$INSTDIR\locales"
  File /r "${STAGEDIR}\locales\*.*"
  SetOutPath $INSTDIR

  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; 记录安装位置（下次安装/升级沿用）
  WriteRegStr HKLM "${MANUPRODUCTKEY}" "" $INSTDIR

  ; 添加/删除程序条目（Mini 同款字段）
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "${PRODUCTNAME}"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$\"$INSTDIR\${MAINBINARY}$\""
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "${MANUFACTURER}"
  WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$\"$INSTDIR$\""
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
  WriteRegStr HKLM "${UNINSTKEY}" "QuietUninstallString" "$\"$INSTDIR\uninstall.exe$\" /S"
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
  WriteRegStr HKLM "${UNINSTKEY}" "MainBinaryName" "${MAINBINARY}"

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNINSTKEY}" "EstimatedSize" "$0"

  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${MAINBINARY}" "" "$INSTDIR\${MAINBINARY}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${MAINBINARY}" "Path" $INSTDIR

  ; 快捷方式：开始菜单（可选文件夹，用户可勾选“不创建”）+ 桌面
  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
    StrCpy $R0 $AppStartMenuFolder 1
    ${If} $R0 != ">"
      CreateDirectory "$SMPROGRAMS\$AppStartMenuFolder"
      CreateShortcut "$SMPROGRAMS\$AppStartMenuFolder\${PRODUCTNAME}.lnk" "$INSTDIR\${MAINBINARY}" "" "$INSTDIR\${MAINBINARY}" 0
    ${EndIf}
  !insertmacro MUI_STARTMENU_WRITE_END
  CreateShortcut "$DESKTOP\${PRODUCTNAME}.lnk" "$INSTDIR\${MAINBINARY}" "" "$INSTDIR\${MAINBINARY}" 0

  ; 服务（名称固定 clash_verge_service；已存在则共用，不重装）
  Call InstallOrStartService
SectionEnd

; ---------- 卸载 ----------
Section "Uninstall"
  SetRegView 64
  Call un.KillAppProcesses
  Call un.RemoveServiceIfOurs

  Delete "$INSTDIR\${MAINBINARY}"
  Delete "$INSTDIR\uninstall.exe"
  RMDir /r "$INSTDIR\sidecar"
  RMDir /r "$INSTDIR\resources"
  RMDir /r "$INSTDIR\locales"
  RMDir "$INSTDIR"

  !insertmacro MUI_STARTMENU_GETFOLDER Application $AppStartMenuFolder
  Delete "$SMPROGRAMS\$AppStartMenuFolder\${PRODUCTNAME}.lnk"
  RMDir "$SMPROGRAMS\$AppStartMenuFolder"
  Delete "$DESKTOP\${PRODUCTNAME}.lnk"

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${MAINBINARY}"
  DeleteRegKey HKLM "${UNINSTKEY}"
  DeleteRegKey HKLM "${MANUPRODUCTKEY}"
  DeleteRegKey /ifempty HKLM "Software\${MANUFACTURER}"

  ; 应用自启动项（AutostartManager 的 Run 值名为 "Clash Win"）
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "Clash Win"
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "Clash Win"

  ; 默认保留配置；仅勾选“同时删除配置数据”时清理
  ${If} $DeleteAppDataState == 1
    RMDir /r "$APPDATA\${APPID}"
    RMDir /r "$LOCALAPPDATA\${APPID}"
  ${EndIf}
SectionEnd

; ---------- 辅助函数 ----------
Function KillAppProcesses
  nsExec::Exec "$SYSDIR\taskkill.exe /F /IM ${MAINBINARY}"
  Pop $0
  ; 只结束从本安装目录启动的内核，避免误杀 Clash Mini 的同名 mini-mihomo.exe
  nsExec::Exec "powershell -NoProfile -NonInteractive -Command $\"Get-Process mini-mihomo -ErrorAction SilentlyContinue | Where-Object Path -Like '$INSTDIR*' | Stop-Process -Force$\""
  Pop $0
FunctionEnd

Function un.KillAppProcesses
  nsExec::Exec "$SYSDIR\taskkill.exe /F /IM ${MAINBINARY}"
  Pop $0
  nsExec::Exec "powershell -NoProfile -NonInteractive -Command $\"Get-Process mini-mihomo -ErrorAction SilentlyContinue | Where-Object Path -Like '$INSTDIR*' | Stop-Process -Force$\""
  Pop $0
FunctionEnd

Function BackupSettingsDir
  StrCpy $R1 0
  backup_loop:
    IntOp $R1 $R1 + 1
    IfFileExists "$APPDATA\${APPID}.bak-$R1\*.*" 0 backup_do
    Goto backup_loop
  backup_do:
    ClearErrors
    Rename "$APPDATA\${APPID}" "$APPDATA\${APPID}.bak-$R1"
    ${If} ${Errors}
      DetailPrint "警告：现有配置目录无法移动，本次安装将保留原设定。"
    ${Else}
      DetailPrint "已备份现有配置到 %APPDATA%\${APPID}.bak-$R1（全新安装）。"
    ${EndIf}
FunctionEnd

Function InstallOrStartService
  nsExec::ExecToStack '$SYSDIR\sc.exe query ${SERVICE_NAME}'
  Pop $0
  Pop $1
  ${If} $0 == 0
    StrCpy $ServiceExists 1
  ${Else}
    StrCpy $ServiceExists 0
  ${EndIf}

  ${If} $ServiceExists == 1
    DetailPrint "${SERVICE_NAME} 已存在（可能与 Clash Mini 共用），跳过安装并尝试启动。"
  ${Else}
    DetailPrint "安装 ${SERVICE_NAME} 服务..."
    ; install_service.rs 在“当前目录”查找 clash-verge-service.exe
    SetOutPath "$INSTDIR\resources"
    ExecWait '"$INSTDIR\resources\clash-verge-service-install.exe"' $0
    SetOutPath "$INSTDIR"
    ${If} $0 != 0
      DetailPrint "警告：clash_verge_service 安装返回 $0，TUN 模式可能不可用。"
      IfSilent +2 0
      MessageBox MB_OK|MB_ICONEXCLAMATION "$(STR_SERVICE_FAIL)"
    ${EndIf}
  ${EndIf}
  nsExec::Exec '$SYSDIR\sc.exe start ${SERVICE_NAME}'
  Pop $0
FunctionEnd

; 仅当服务指向本安装目录时才移除，避免破坏 Clash Mini 共用的服务
Function un.RemoveServiceIfOurs
  ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Services\${SERVICE_NAME}" "ImagePath"
  ${If} $0 == ""
    Return
  ${EndIf}

  StrCpy $1 $0 1
  ${If} $1 == '"'
    StrCpy $0 $0 "" 1
  ${EndIf}
  StrLen $2 "$INSTDIR"
  StrCpy $3 $0 $2
  ${If} $3 != "$INSTDIR"
    DetailPrint "${SERVICE_NAME} 不属于本安装目录，保留该共享服务。"
    Return
  ${EndIf}

  nsExec::Exec '$SYSDIR\sc.exe stop ${SERVICE_NAME}'
  Pop $0
  SetOutPath "$INSTDIR\resources"
  ExecWait '"$INSTDIR\resources\clash-verge-service-uninstall.exe"' $0
  ${If} $0 != 0
    nsExec::Exec '$SYSDIR\sc.exe delete ${SERVICE_NAME}'
    Pop $0
  ${EndIf}
FunctionEnd

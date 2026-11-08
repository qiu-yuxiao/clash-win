; ClashWin NSIS 安装脚本（1:1 对照 Clash Mini v3.2.2 自定义模板
;   src-tauri/packages/windows/installer.nsi + tauri.windows.conf.json:14-20，
;   tauri-bundler tauri-v2.11.2 utils.nsh / languages/{SimpChinese,English,Russian}.nsh）
; 安装形态与 Mini 一致：installMode=perMachine（tauri.windows.conf.json:18）→
;   RequestExecutionLevel admin + Program Files + HKLM 卸载项（模板 :108-110, :1247-1248）。
; 语言 = SimpChinese/English/Russian + displayLanguageSelector（tauri.windows.conf.json:15,17）。
; 无 License 页（tauri.conf.json bundle.license 未配置 → 模板 :161 条件不成立）。
; 无开始菜单文件夹页（bundler 未传 start_menu_folder → 模板 :381-386 PRE Skip，
;   快捷方式直接落 $SMPROGRAMS\${PRODUCTNAME}.lnk（模板 :1344-1345））。
; 服务名固定 clash_verge_service（与 Clash Mini 共用同一特权服务，共存豁免）。
; 版本号只有一个来源：下面的 VERSION（可由 build_installer.ps1 以 /DVERSION=... 覆盖）。
;
; 用法：
;   构建： installer\build_installer.ps1 -Version 0.1.0
;   交互安装： ClashWin_0.1.0_x64-setup.exe
;   被动安装： ... /P            （仅进度页，自动关页；桌面快捷方式自动创建）
;   静默安装： ... /S [/NS] [/R [/ARGS <args>]]
;   更新模式（由应用内更新器启动，← updater.rs:808-812）：
;              ... /UPDATE /ARGS <当前进程参数>
;   静默卸载： <安装目录>\uninstall.exe /S [/P] [/UPDATE]
; Unicode true
ManifestDPIAware true
ManifestDPIAwareness PerMonitorV2
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "WinVer.nsh"
!include "WordFunc.nsh"   ; ${VersionCompare}（重安装页版本比较，纯 x.y.z 下与
                          ; nsis_tauri_utils::SemverCompare 用户可见结果一致）
!include "x64.nsh"
!include "StrFunc.nsh"
!include "Win\COM.nsh"    ; SetLnkAppUserModelId / UnpinShortcut 等 COM 宏（← utils.nsh）
!include "Win\Propkey.nsh"

${StrStr}      ; 运行中进程检测（tasklist 输出按进程名匹配，语言无关）
${UnStrStr}

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
  !define VERSION "0.1.3"
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

; ---------- 产品常量 ----------
!define PRODUCTNAME "ClashWin"
!define MANUFACTURER "Clash Win"
!define COPYRIGHT "GNU General Public License v3.0 (GPL-3.0-only)"
!define BUNDLEID "io.github.clash-win.clash-win"
!define MAINBINARY "clashwin.exe"
!define SERVICE_NAME "clash_verge_service"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCTNAME}"
!define MANUPRODUCTKEY "Software\${MANUFACTURER}\${PRODUCTNAME}"
!define ALLOWDOWNGRADES "true"   ; tauri-bundler 默认 true（tauri-utils config.rs:1061-1062
                                 ; default_true；Mini 未配置 → 降级单选可用）

!if ! /fileexists "${STAGEDIR}\${MAINBINARY}"
  !error "缺少 ${STAGEDIR}\${MAINBINARY}：请先运行 installer\build_installer.ps1 暂存 Release 产物。"
!endif
!if ! /fileexists "${STAGEDIR}\sidecar\mini-mihomo.exe"
  !error "缺少 ${STAGEDIR}\sidecar\mini-mihomo.exe：请先运行 installer\build_installer.ps1。"
!endif
!if ! /fileexists "${STAGEDIR}\resources\clash-verge-service-install.exe"
  !error "缺少 ${STAGEDIR}\resources\clash-verge-service-install.exe：请先运行 installer\build_installer.ps1。"
!endif

; Name 仅产品名（← 模板 :80 Name "${PRODUCTNAME}"，不带版本号）
Name "${PRODUCTNAME}"
BrandingText "${COPYRIGHT}"
OutFile "${OUTFILE}"
; perMachine（tauri.windows.conf.json:18 → 模板 :108-110）
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
; 安装语言记忆（← 模板 :150-152：HKCU + MANUPRODUCTKEY + "Installer Language"）
!define MUI_LANGDLL_REGISTRY_ROOT "HKCU"
!define MUI_LANGDLL_REGISTRY_KEY "${MANUPRODUCTKEY}"
!define MUI_LANGDLL_REGISTRY_VALUENAME "Installer Language"

Var PassiveMode      ; /P（← 模板 :70）
Var UpdateMode       ; /UPDATE（← 模板 :71）
Var NoShortcutMode   ; /NS（← 模板 :72）
Var ReinstallPageCheck  ; 重安装页单选记忆（← 模板 :174）
Var OldMainBinaryName   ; 主程序名迁移（← 模板 :74）
Var DeleteAppDataCheckbox
Var DeleteAppDataCheckboxState
Var ServiceExists

; ---------- 安装页面（← 模板 :154-405 顺序） ----------
!define MUI_CUSTOMFUNCTION_GUIINIT myGuiInit
; 1. Welcome（PRE SkipIfPassive ← 模板 :157）
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipIfPassive
!insertmacro MUI_PAGE_WELCOME
; 2. 重安装维护页（← 模板 :172-175 Page custom PageReinstall PageLeaveReinstall）
Page custom PageReinstall PageLeaveReinstall
; 3. 目录页（PRE SkipIfPassive ← 模板 :376-377）
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipIfPassive
!insertmacro MUI_PAGE_DIRECTORY
; 4. 安装页
!insertmacro MUI_PAGE_INSTFILES
; 5. Finish（← 模板 :394-405：NOAUTOCLOSE + SHOWREADME 借用为桌面快捷方式勾选 + 运行）
!define MUI_FINISHPAGE_NOAUTOCLOSE
!define MUI_FINISHPAGE_SHOWREADME
!define MUI_FINISHPAGE_SHOWREADME_TEXT "$(createDesktop)"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION CreateOrUpdateDesktopShortcut
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION RunMainBinary
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipIfPassive
!insertmacro MUI_PAGE_FINISH

; ---------- 卸载页面（← 模板 :411-454：标准确认页注入 deleteAppData 复选框） ----------
!define MUI_PAGE_CUSTOMFUNCTION_SHOW un.ConfirmShow
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE un.ConfirmLeave
!define MUI_PAGE_CUSTOMFUNCTION_PRE un.SkipIfPassive
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; ---------- 语言（← tauri.windows.conf.json:17 languages 三语 + :15 选择器） ----------
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Russian"

; 语言串（← tauri-bundler tauri-v2.11.2 languages/{SimpChinese,English,Russian}.nsh 逐字；
;   {{product_name}} 占位已按显示名豁免替换为 ClashWin）
LangString appRunning ${LANG_SIMPCHINESE} "ClashWin 正在运行！请关闭后再试。"
LangString appRunning ${LANG_ENGLISH} "ClashWin is running! Please close it first then try again."
LangString appRunning ${LANG_RUSSIAN} "ClashWin запущен! Пожалуйста, закройте приложение и попробуйте еще раз."
LangString appRunningOkKill ${LANG_SIMPCHINESE} "ClashWin 正在运行！$\n点击确定以终止运行。"
LangString appRunningOkKill ${LANG_ENGLISH} "ClashWin is running!$\nClick OK to kill it"
LangString appRunningOkKill ${LANG_RUSSIAN} "ClashWin запущен!$\nНажмите OK чтобы закрыть приложение"
LangString failedToKillApp ${LANG_SIMPCHINESE} "无法终止 ClashWin。请关闭后再试。"
LangString failedToKillApp ${LANG_ENGLISH} "Failed to kill ClashWin. Please close it first then try again"
LangString failedToKillApp ${LANG_RUSSIAN} "Не удалось закрыть ClashWin. Пожалуйста, закройте приложение и попробуйте еще раз"
LangString alreadyInstalled ${LANG_SIMPCHINESE} "已安装"
LangString alreadyInstalled ${LANG_ENGLISH} "Already Installed"
LangString alreadyInstalled ${LANG_RUSSIAN} "Уже установлено"
LangString alreadyInstalledLong ${LANG_SIMPCHINESE} "${PRODUCTNAME} ${VERSION} 已经安装了。选择你想要执行的操作后点击下一步以继续。"
LangString alreadyInstalledLong ${LANG_ENGLISH} "${PRODUCTNAME} ${VERSION} is already installed. Select the operation you want to perform and click Next to continue."
LangString alreadyInstalledLong ${LANG_RUSSIAN} "${PRODUCTNAME} ${VERSION} уже установлен. Выберите действие, которое вы хотите выполнить и нажмите Далее для продолжения."
LangString addOrReinstall ${LANG_SIMPCHINESE} "添加/重新安装组件"
LangString addOrReinstall ${LANG_ENGLISH} "Add/Reinstall components"
LangString addOrReinstall ${LANG_RUSSIAN} "Добавить/Переустановить компоненты"
LangString uninstallApp ${LANG_SIMPCHINESE} "卸载 ${PRODUCTNAME}"
LangString uninstallApp ${LANG_ENGLISH} "Uninstall ${PRODUCTNAME}"
LangString uninstallApp ${LANG_RUSSIAN} "Удалить ${PRODUCTNAME}"
LangString chooseMaintenanceOption ${LANG_SIMPCHINESE} "选择要执行的维护操作。"
LangString chooseMaintenanceOption ${LANG_ENGLISH} "Choose the maintenance option to perform."
LangString chooseMaintenanceOption ${LANG_RUSSIAN} "Выберите действие, которое вы хотите выполнить."
LangString choowHowToInstall ${LANG_SIMPCHINESE} "选择你想要安装 ${PRODUCTNAME} 的方式。"
LangString choowHowToInstall ${LANG_ENGLISH} "Choose how you want to install ${PRODUCTNAME}."
LangString choowHowToInstall ${LANG_RUSSIAN} "Выберите, как вы хотите установить ${PRODUCTNAME}."
LangString olderOrUnknownVersionInstalled ${LANG_SIMPCHINESE} "系统中已存在版本为 $R4 的 ${PRODUCTNAME}。推荐先卸载当前版本后再进行安装。选择你想要执行的操作后点击下一步以继续。"
LangString olderOrUnknownVersionInstalled ${LANG_ENGLISH} "An $R4 version of ${PRODUCTNAME} is installed on your system. It's recommended that you uninstall the current version before installing. Select the operation you want to perform and click Next to continue."
LangString olderOrUnknownVersionInstalled ${LANG_RUSSIAN} "$R4 версия ${PRODUCTNAME} уже установлена в вашей системе. Рекомендуется удалить текущую версию перед установкой. Выберите действие, которое вы хотите выполнить и нажмите Далее для продолжения."
LangString newerVersionInstalled ${LANG_SIMPCHINESE} "有一个更新版本的 ${PRODUCTNAME} 已经安装了！不推荐你安装旧的版本。如果你真的想要安装这个旧的版本，推荐先卸载当前版本。选择你想要执行的操作后点击下一步以继续。"
LangString newerVersionInstalled ${LANG_ENGLISH} "A newer version of ${PRODUCTNAME} is already installed! It is not recommended that you install an older version. If you really want to install this older version, it's better to uninstall the current version first. Select the operation you want to perform and click Next to continue."
LangString newerVersionInstalled ${LANG_RUSSIAN} "Более новая версия ${PRODUCTNAME} уже установлена! Не рекомендуется устанавливать более раннюю версию. Если вы действительно хотите установить эту версию, рекомендуется сначала удалить текущую. Выберите действие, которое вы хотите выполнить и нажмите Далее для продолжения."
LangString uninstallBeforeInstalling ${LANG_SIMPCHINESE} "安装前卸载"
LangString uninstallBeforeInstalling ${LANG_ENGLISH} "Uninstall before installing"
LangString uninstallBeforeInstalling ${LANG_RUSSIAN} "Удалить перед установкой"
LangString dontUninstall ${LANG_SIMPCHINESE} "请勿卸载"
LangString dontUninstall ${LANG_ENGLISH} "Do not uninstall"
LangString dontUninstall ${LANG_RUSSIAN} "Не удалять"
LangString unableToUninstall ${LANG_SIMPCHINESE} "无法卸载！"
LangString unableToUninstall ${LANG_ENGLISH} "Unable to uninstall!"
LangString unableToUninstall ${LANG_RUSSIAN} "Не удалось удалить!"
LangString older ${LANG_SIMPCHINESE} "旧的"
LangString older ${LANG_ENGLISH} "older"
LangString older ${LANG_RUSSIAN} "Более ранняя"
LangString unknown ${LANG_SIMPCHINESE} "未知"
LangString unknown ${LANG_ENGLISH} "unknown"
LangString unknown ${LANG_RUSSIAN} "Неизвестная"
LangString createDesktop ${LANG_SIMPCHINESE} "创建桌面快捷方式"
LangString createDesktop ${LANG_ENGLISH} "Create desktop shortcut"
LangString createDesktop ${LANG_RUSSIAN} "Добавить ярлык на рабочий стол"
LangString deleteAppData ${LANG_SIMPCHINESE} "删除应用程序数据"
LangString deleteAppData ${LANG_ENGLISH} "Delete the application data"
LangString deleteAppData ${LANG_RUSSIAN} "Удалить данные приложения"
LangString STR_SERVICE_FAIL ${LANG_SIMPCHINESE} "clash_verge_service 服务安装失败。TUN 模式可能不可用，可稍后在应用内修复。"
LangString STR_SERVICE_FAIL ${LANG_ENGLISH} "Failed to install clash_verge_service. TUN mode may be unavailable; you can retry from the app later."
LangString STR_SERVICE_FAIL ${LANG_RUSSIAN} "Не удалось установить службу clash_verge_service. Режим TUN может быть недоступен; повторите попытку в приложении позже."

; ---------- utils.nsh 宏（← tauri-bundler tauri-v2.11.2 utils.nsh；
;   nsis_tauri_utils::FindProcess/KillProcess 以 tasklist/taskkill 等价实现
;   （tasklist 无匹配时输出语言相关 INFO 文案，故按进程名 ${StrStr} 匹配），
;   纯实现层替换、用户可见交互不变。安装/卸载段各一份：StrFunc 要求
;   卸载器内使用 ${UnStrStr} 变体。标签唯一化用编译期计数器
;   （NSIS 3.11 的 ${__LINE__} 在宏内展开为 "行.列" 含点，不能作标签）） ----------
!define CWPROCID 0
!macro CWNextProcId
  !undef CWPROCID
  !define /math CWPROCID CWPROCID + 1
!macroend

!macro CheckIfAppIsRunning executableName
  !insertmacro CWNextProcId
  nsExec::ExecToStack 'tasklist /FI "IMAGENAME eq ${executableName}" /NH'
  Pop $R0
  Pop $R9
  ${StrStr} $R8 $R9 "${executableName}"
  ${If} $R8 != ""
    ${If} $PassiveMode = 1
    ${OrIf} ${Silent}
      Goto cw_kill_${CWPROCID}
    ${Else}
      MessageBox MB_OKCANCEL "$(appRunningOkKill)" IDOK cw_kill_${CWPROCID} IDCANCEL cw_cancel_${CWPROCID}
    ${EndIf}
    cw_cancel_${CWPROCID}:
      Abort "$(appRunning)"
    cw_kill_${CWPROCID}:
      nsExec::Exec '$SYSDIR\taskkill.exe /F /IM ${executableName}'
      Pop $R0
      Sleep 500
      nsExec::ExecToStack 'tasklist /FI "IMAGENAME eq ${executableName}" /NH'
      Pop $R0
      Pop $R9
      ${StrStr} $R8 $R9 "${executableName}"
      ${If} $R8 != ""
        ${If} ${Silent}
          Abort "$(failedToKillApp)"
        ${Else}
          MessageBox MB_ICONEXCLAMATION "$(failedToKillApp)"
          Quit
        ${EndIf}
      ${EndIf}
  ${EndIf}
!macroend

!macro un.CheckIfAppIsRunning executableName
  !insertmacro CWNextProcId
  nsExec::ExecToStack 'tasklist /FI "IMAGENAME eq ${executableName}" /NH'
  Pop $R0
  Pop $R9
  ${UnStrStr} $R8 $R9 "${executableName}"
  ${If} $R8 != ""
    ${If} $PassiveMode = 1
    ${OrIf} ${Silent}
      Goto cw_ukill_${CWPROCID}
    ${Else}
      MessageBox MB_OKCANCEL "$(appRunningOkKill)" IDOK cw_ukill_${CWPROCID} IDCANCEL cw_ucancel_${CWPROCID}
    ${EndIf}
    cw_ucancel_${CWPROCID}:
      Abort "$(appRunning)"
    cw_ukill_${CWPROCID}:
      nsExec::Exec '$SYSDIR\taskkill.exe /F /IM ${executableName}'
      Pop $R0
      Sleep 500
      nsExec::ExecToStack 'tasklist /FI "IMAGENAME eq ${executableName}" /NH'
      Pop $R0
      Pop $R9
      ${UnStrStr} $R8 $R9 "${executableName}"
      ${If} $R8 != ""
        ${If} ${Silent}
          Abort "$(failedToKillApp)"
        ${Else}
          MessageBox MB_ICONEXCLAMATION "$(failedToKillApp)"
          Quit
        ${EndIf}
      ${EndIf}
  ${EndIf}
!macroend

; SetLnkAppUserModelId（← utils.nsh:74-98，BUNDLEID ← 模板 :51）
!macro SetLnkAppUserModelId shortcut
  !insertmacro ComHlpr_CreateInProcInstance ${CLSID_ShellLink} ${IID_IShellLink} r0 ""
  ${If} $0 P<> 0
    ${IUnknown::QueryInterface} $0 '("${IID_IPersistFile}",.r1)'
    ${If} $1 P<> 0
      ${IPersistFile::Load} $1 '("${shortcut}", ${STGM_READWRITE})'
      ${IUnknown::QueryInterface} $0 '("${IID_IPropertyStore}",.r2)'
      ${If} $2 P<> 0
        System::Call 'Oleaut32::SysAllocString(w "${BUNDLEID}") i.r3'
        System::Call '*${SYSSTRUCT_PROPERTYKEY}(${PKEY_AppUserModel_ID})p.r4'
        System::Call '*${SYSSTRUCT_PROPVARIANT}(${VT_BSTR},,&i4 $3)p.r5'
        ${IPropertyStore::SetValue} $2 '($4,$5)'
        System::Call 'Oleaut32::SysFreeString($3)'
        System::Free $4
        System::Free $5
        ${IPropertyStore::Commit} $2 ""
        ${IUnknown::Release} $2 ""
        ${IPersistFile::Save} $1 '("${shortcut}",1)'
      ${EndIf}
      ${IUnknown::Release} $1 ""
    ${EndIf}
    ${IUnknown::Release} $0 ""
  ${EndIf}
!macroend

; DeleteAppUserModelId（← utils.nsh:101-115）
!macro DeleteAppUserModelId
  !insertmacro ComHlpr_CreateInProcInstance ${CLSID_DestinationList} ${IID_ICustomDestinationList} r1 ""
  ${If} $1 P<> 0
    ${ICustomDestinationList::DeleteList} $1 '("${BUNDLEID}")'
    ${IUnknown::Release} $1 ""
  ${EndIf}
!macroend

; UnpinShortcut（← utils.nsh:120-130）
!macro UnpinShortcut shortcut
  !insertmacro ComHlpr_CreateInProcInstance ${CLSID_StartMenuPin} ${IID_IStartMenuPinnedList} r0 ""
  ${If} $0 P<> 0
      System::Call 'SHELL32::SHCreateItemFromParsingName(ws, p0, g "${IID_IShellItem}", *p0r1)' "${shortcut}"
      ${If} $1 P<> 0
          ${IStartMenuPinnedList::RemoveFromList} $0 '(r1)'
          ${IUnknown::Release} $1 ""
      ${EndIf}
      ${IUnknown::Release} $0 ""
  ${EndIf}
!macroend

; IsShortcutTarget（← utils.nsh:160-184；Pop 结果 1=匹配 0=不匹配）
!macro IsShortcutTargetCompat shortcut target
  StrCpy $3 0
  !insertmacro ComHlpr_CreateInProcInstance ${CLSID_ShellLink} ${IID_IShellLink} r0 ""
  ${If} $0 P<> 0
    ${IUnknown::QueryInterface} $0 '("${IID_IPersistFile}", .r1)'
    ${If} $1 P<> 0
      ${IPersistFile::Load} $1 '("${shortcut}", ${STGM_READ})'
      System::Alloc 260
      Pop $2
      ${IShellLink::GetPath} $0 '(.r2, 260, 0, ${SLGP_RAWPATH})'
      ${If} $2 == "${target}"
        StrCpy $3 1
      ${EndIf}
      System::Free $2
      ${IUnknown::Release} $1 ""
    ${EndIf}
    ${IUnknown::Release} $0 ""
  ${EndIf}
  Push $3
!macroend
!define /ifndef SLGP_RAWPATH 0x4

; SetShortcutTarget（← utils.nsh:133-145）
!macro SetShortcutTargetCompat shortcut target
  !insertmacro ComHlpr_CreateInProcInstance ${CLSID_ShellLink} ${IID_IShellLink} r0 ""
  ${If} $0 P<> 0
    ${IUnknown::QueryInterface} $0 '("${IID_IPersistFile}",.r1)'
    ${If} $1 P<> 0
      ${IPersistFile::Load} $1 '("${shortcut}", ${STGM_READWRITE})'
      ${IShellLink::SetPath} $0 '(w "${target}")'
      ${IPersistFile::Save} $1 '("${shortcut}",1)'
      ${IUnknown::Release} $1 ""
    ${EndIf}
    ${IUnknown::Release} $0 ""
  ${EndIf}
!macroend

; ---------- 初始化（← 模板 :470-517 .onInit，perMachine 分支） ----------
Function .onInit
  ${GetOptions} $CMDLINE "/P" $PassiveMode
  ${IfNot} ${Errors}
    StrCpy $PassiveMode 1
  ${EndIf}

  ${GetOptions} $CMDLINE "/NS" $NoShortcutMode
  ${IfNot} ${Errors}
    StrCpy $NoShortcutMode 1
  ${EndIf}

  ${GetOptions} $CMDLINE "/UPDATE" $UpdateMode
  ${IfNot} ${Errors}
    StrCpy $UpdateMode 1
  ${EndIf}

  !insertmacro MUI_LANGDLL_DISPLAY

  SetShellVarContext all
  SetRegView 64
FunctionEnd

; 窗口置顶（← 模板 :465-468 myGuiInit：防杀软拦截提示被遮挡忽略）
Function myGuiInit
  System::Call "user32::SetWindowPos(i $HWNDPARENT, i -1, i 0, i 0, i 0, i 0, i 3)"
FunctionEnd

Function RunMainBinary
  !ifdef HAVE_TAURI_UTILS
    nsis_tauri_utils::RunAsUser "$INSTDIR\${MAINBINARY}" ""
  !else
    ExecShell "open" "$INSTDIR\${MAINBINARY}"
  !endif
FunctionEnd

Function SkipIfPassive
  ${IfThen} $PassiveMode = 1  ${|} Abort ${|}
FunctionEnd
Function un.SkipIfPassive
  ${IfThen} $PassiveMode = 1  ${|} Abort ${|}
FunctionEnd

; ---------- 重安装维护页（← 模板 :174-373 逐段移植；
;   nsis_tauri_utils::SemverCompare 以 WordFunc ${VersionCompare} 等价实现：
;   返回值映射 0→0 同版 / 1→1 升级 / 2→-1 降级） ----------
Function PageReinstall
  ; 检测既有安装（← 模板 :206-208）
  ReadRegStr $R0 HKLM "${UNINSTKEY}" ""
  ReadRegStr $R1 HKLM "${UNINSTKEY}" "UninstallString"
  ${IfThen} "$R0$R1" == "" ${|} Abort ${|}

  ; 版本比较（← 模板 :212-247；关系存 $R0：0 同版 / 1 升级 / -1 降级）
  StrCpy $R4 "$(older)"
  ReadRegStr $6 HKLM "${UNINSTKEY}" "DisplayVersion"
  ${IfThen} $6 == "" ${|} StrCpy $R4 "$(unknown)" ${|}
  ${VersionCompare} "${VERSION}" "$6" $7
  ${If} $7 = 0
    StrCpy $R0 0
  ${ElseIf} $7 = 1
    StrCpy $R0 1
  ${Else}
    StrCpy $R0 -1
  ${EndIf}

  ; 同版本：重安装（← 模板 :224-228）
  ${If} $R0 = 0
    StrCpy $R2 "$(alreadyInstalledLong)"
    StrCpy $R3 "$(addOrReinstall)"
    StrCpy $R5 "$(uninstallApp)"
    !insertmacro MUI_HEADER_TEXT "$(alreadyInstalled)" "$(chooseMaintenanceOption)"
  ; 升级（← 模板 :230-234）
  ${ElseIf} $R0 = 1
    StrCpy $R2 "$(olderOrUnknownVersionInstalled)"
    StrCpy $R3 "$(uninstallBeforeInstalling)"
    StrCpy $R5 "$(dontUninstall)"
    !insertmacro MUI_HEADER_TEXT "$(alreadyInstalled)" "$(choowHowToInstall)"
  ; 降级（← 模板 :236-244；ALLOWDOWNGRADES=true → 单选不禁用）
  ${ElseIf} $R0 = -1
    StrCpy $R2 "$(newerVersionInstalled)"
    StrCpy $R3 "$(uninstallBeforeInstalling)"
    StrCpy $R5 "$(dontUninstall)"
    !insertmacro MUI_HEADER_TEXT "$(alreadyInstalled)" "$(choowHowToInstall)"
  ${Else}
    Abort
  ${EndIf}

  ; 被动模式跳过 UI 直接走 Leave（← 模板 :255-257）
  ${If} $PassiveMode = 1
    Call PageLeaveReinstall
  ${Else}
    nsDialogs::Create 1018
    Pop $R4
    ${NSD_CreateLabel} 0 0 100% 24u $R2
    Pop $R2

    ${NSD_CreateRadioButton} 30u 50u -30u 8u $R3
    Pop $R3
    ${NSD_OnClick} $R3 PageReinstallUpdateSelection

    ${NSD_CreateRadioButton} 30u 70u -30u 8u $R5
    Pop $R5
    ${NSD_OnClick} $R5 PageReinstallUpdateSelection

    ; 首次进入默认选第一项（← 模板 :280-284）
    ${If} $ReinstallPageCheck <> 2
      SendMessage $R3 ${BM_SETCHECK} ${BST_CHECKED} 0
    ${Else}
      SendMessage $R5 ${BM_SETCHECK} ${BST_CHECKED} 0
    ${EndIf}

    ${NSD_SetFocus} $R3
    nsDialogs::Show
  ${EndIf}
FunctionEnd
Function PageReinstallUpdateSelection
  ${NSD_GetState} $R3 $R1
  ${If} $R1 == ${BST_CHECKED}
    StrCpy $ReinstallPageCheck 1
  ${Else}
    StrCpy $ReinstallPageCheck 2
  ${EndIf}
FunctionEnd
Function PageLeaveReinstall
  ${NSD_GetState} $R3 $R1

  ; 更新模式始终不卸载直接继续（← 模板 :307-309）
  ${If} $UpdateMode = 1
    Goto reinst_done
  ${EndIf}

  ; 单选语义随版本关系变化（← 模板 :315-333）：
  ;   同版：1=添加/重安装(继续) 0=先卸载；升级/降级：1=先卸载 0=不卸载继续
  ${If} $R0 = 0
    ${If} $R1 = 1
      Goto reinst_done
    ${Else}
      Goto reinst_uninstall
    ${EndIf}
  ${ElseIf} $R0 = 1
    ${If} $R1 = 1
      Goto reinst_uninstall
    ${Else}
      Goto reinst_done
    ${EndIf}
  ${ElseIf} $R0 = -1
    ${If} $R1 = 1
      Goto reinst_uninstall
    ${Else}
      Goto reinst_done
    ${EndIf}
  ${EndIf}

  reinst_uninstall:
    HideWindow
    ClearErrors
    ReadRegStr $4 HKLM "${MANUPRODUCTKEY}" ""
    ReadRegStr $R1 HKLM "${UNINSTKEY}" "UninstallString"
    ${IfThen} $UpdateMode = 1 ${|} StrCpy $R1 "$R1 /UPDATE" ${|}
    ${IfThen} $PassiveMode = 1 ${|} StrCpy $R1 "$R1 /P" ${|}
    StrCpy $R1 "$R1 _?=$4"
    ExecWait '$R1' $0
    BringToFront

    ${IfThen} ${Errors} ${|} StrCpy $0 2 ${|}
    ${If} $0 <> 0
    ${OrIf} ${FileExists} "$INSTDIR\${MAINBINARY}"
      ; 用户取消 NSIS 卸载 → 返回重安装页（← 模板 :363-366）
      ${If} $0 = 1
        Abort
      ${EndIf}
      MessageBox MB_ICONEXCLAMATION "$(unableToUninstall)"
      Abort
    ${EndIf}
  reinst_done:
FunctionEnd

; ---------- Finish 页辅助（← 模板 :1307-1370；STARTMENUFOLDER 为空分支） ----------
Function CreateOrUpdateStartMenuShortcut
  ; 旧主程序名迁移（← 模板 :1310-1324）
  StrCpy $R0 0
  ${If} $OldMainBinaryName != ""
    !insertmacro IsShortcutTargetCompat "$SMPROGRAMS\${PRODUCTNAME}.lnk" "$INSTDIR\$OldMainBinaryName"
    Pop $0
    ${If} $0 = 1
      !insertmacro SetShortcutTargetCompat "$SMPROGRAMS\${PRODUCTNAME}.lnk" "$INSTDIR\${MAINBINARY}"
      StrCpy $R0 1
    ${EndIf}
  ${EndIf}
  ${If} $R0 = 1
    Return
  ${EndIf}

  ; 更新/无快捷方式模式跳过（← 模板 :1330-1337）
  ${If} $UpdateMode = 1
  ${OrIf} $NoShortcutMode = 1
    Return
  ${EndIf}

  ; 快捷方式直接落 Programs 根（← 模板 :1343-1346）
  CreateShortcut "$SMPROGRAMS\${PRODUCTNAME}.lnk" "$INSTDIR\${MAINBINARY}"
  !insertmacro SetLnkAppUserModelId "$SMPROGRAMS\${PRODUCTNAME}.lnk"
FunctionEnd

Function CreateOrUpdateDesktopShortcut
  ${If} $OldMainBinaryName != ""
    !insertmacro IsShortcutTargetCompat "$DESKTOP\${PRODUCTNAME}.lnk" "$INSTDIR\$OldMainBinaryName"
    Pop $0
    ${If} $0 = 1
      !insertmacro SetShortcutTargetCompat "$DESKTOP\${PRODUCTNAME}.lnk" "$INSTDIR\${MAINBINARY}"
      Return
    ${EndIf}
  ${EndIf}

  ${If} $UpdateMode = 1
  ${OrIf} $NoShortcutMode = 1
    Return
  ${EndIf}

  CreateShortcut "$DESKTOP\${PRODUCTNAME}.lnk" "$INSTDIR\${MAINBINARY}"
  !insertmacro SetLnkAppUserModelId "$DESKTOP\${PRODUCTNAME}.lnk"
FunctionEnd

; ---------- 安装（← 模板 :886-1038 Section Install） ----------
Section "Install"
  SetRegView 64
  SetOutPath $INSTDIR

  ; TCP autotuning 重置（← 模板 :893）
  nsExec::Exec 'netsh int tcp res'
  Pop $0

  ; 运行中检测 + 提示终止（← 模板 :895 CheckIfAppIsRunning；用户可见交互：
  ;   appRunningOkKill 确认/取消 → 静默/被动直接终止）
  !insertmacro CheckIfAppIsRunning "${MAINBINARY}"

  ; 内核进程清理（Mini 模板 :543-623 CheckAllVergeProcesses 为全量 taskkill；
  ;   共存豁免：ClashWin 只清本安装目录启动的 mini-mihomo.exe，不动 Clash Mini 的同名进程，
  ;   也不 kill 共享的 clash_verge_service.exe）
  nsExec::Exec "powershell -NoProfile -NonInteractive -Command $\"Get-Process mini-mihomo -ErrorAction SilentlyContinue | Where-Object Path -Like '$INSTDIR*' | Stop-Process -Force$\""
  Pop $0

  ; 确保启动文件夹存在（← 模板 :898-905）
  CreateDirectory "C:\ProgramData\Microsoft\Windows\Start Menu\Programs\Startup"
  DetailPrint "Ensured system startup folder exists"
  SetShellVarContext current
  StrCpy $0 "$SMPROGRAMS\Startup"
  CreateDirectory "$0"
  DetailPrint "Ensured user startup folder exists: $0"
  SetShellVarContext all

  ; 清理陈旧窗口状态文件（← 模板 :907-910；ClashWin 常量名 window_state.json
  ;   ← constants.h:13 kWindowState，另含 Mini 两个历史文件名变体）
  DetailPrint "Removing window-state files"
  Delete "$APPDATA\${BUNDLEID}\window_state.json"
  Delete "$APPDATA\${BUNDLEID}\window-state.json"
  Delete "$APPDATA\${BUNDLEID}\.window-state.json"

  ; 清理历史自启动注册表值（← 模板 :912-931，品牌位替换 Clash Mini→Clash Win）
  StrCpy $R1 "Software\Microsoft\Windows\CurrentVersion\Run"
  ReadRegStr $R2 HKCU "$R1" "Clash Win"
  ${If} $R2 != ""
    DeleteRegValue HKCU "$R1" "Clash Win"
  ${EndIf}
  ReadRegStr $R2 HKLM "$R1" "Clash Win"
  ${If} $R2 != ""
    DeleteRegValue HKLM "$R1" "Clash Win"
  ${EndIf}
  ReadRegStr $R2 HKCU "$R1" "clash-win"
  ${If} $R2 != ""
    DeleteRegValue HKCU "$R1" "clash-win"
  ${EndIf}
  ReadRegStr $R2 HKLM "$R1" "clash-win"
  ${If} $R2 != ""
    DeleteRegValue HKLM "$R1" "clash-win"
  ${EndIf}

  ; 主程序 + 资源 + sidecar + locales（← 模板 :939-956）
  File "${STAGEDIR}\${MAINBINARY}"
  SetOutPath "$INSTDIR\sidecar"
  File "${STAGEDIR}\sidecar\*.exe"
  SetOutPath "$INSTDIR\resources"
  File /r "${STAGEDIR}\resources\*.*"
  SetOutPath "$INSTDIR\locales"
  File /r "${STAGEDIR}\locales\*.*"
  SetOutPath $INSTDIR

  ; 用户必读.txt 复制到安装根（← 模板 :950-951）
  CopyFiles /SILENT "$INSTDIR\resources\用户必读.txt" "$INSTDIR\用户必读.txt"

  ; 服务（Mini 模板 :958 StartVergeService 仅重启既有服务；ClashWin 因 B-D34
  ;   应用内服务命令禁用而由安装器承担安装职责——已登记偏差。启动语义对齐：
  ;   已运行则不再重复启动）
  Call InstallOrStartService

  ; 卸载器（← 模板 :976）
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; 记录安装位置（← 模板 :979 WriteRegStr SHCTX MANUPRODUCTKEY "" $INSTDIR）
  WriteRegStr HKLM "${MANUPRODUCTKEY}" "" $INSTDIR

  ; 旧主程序名迁移记录（← 模板 :987-995）
  ReadRegStr $OldMainBinaryName HKLM "${UNINSTKEY}" "MainBinaryName"
  ${If} $OldMainBinaryName != ""
  ${AndIf} $OldMainBinaryName != "${MAINBINARY}"
    Delete "$INSTDIR\$OldMainBinaryName"
  ${EndIf}
  WriteRegStr HKLM "${UNINSTKEY}" "MainBinaryName" "${MAINBINARY}"

  ; 添加/删除程序条目（← 模板 :997-1010）
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "${PRODUCTNAME}"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$\"$INSTDIR\${MAINBINARY}$\""
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "${MANUFACTURER}"
  WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$\"$INSTDIR$\""
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNINSTKEY}" "EstimatedSize" "$0"

  ; 开始菜单快捷方式（无文件夹页 → 直接 Programs 根）
  Call CreateOrUpdateStartMenuShortcut

  ; 被动/静默安装时 Finish 页被跳过 → 直接建桌面快捷方式（← 模板 :1023-1028）
  ${If} $PassiveMode = 1
  ${OrIf} ${Silent}
    Call CreateOrUpdateDesktopShortcut
  ${EndIf}

  ; 被动模式自动关页（← 模板 :1034-1037）
  ${If} $PassiveMode = 1
    SetAutoClose true
  ${EndIf}
SectionEnd

; 静默/被动 + /R [/ARGS] 安装后启动应用（← 模板 :1040-1051 .onInstSuccess）
Function .onInstSuccess
  ${If} $PassiveMode = 1
  ${OrIf} ${Silent}
    ${GetOptions} $CMDLINE "/R" $R0
    ${IfNot} ${Errors}
      ${GetOptions} $CMDLINE "/ARGS" $R0
      !ifdef HAVE_TAURI_UTILS
        nsis_tauri_utils::RunAsUser "$INSTDIR\${MAINBINARY}" "$R0"
      !else
        ExecShell "open" "$INSTDIR\${MAINBINARY}"
      !endif
    ${EndIf}
  ${EndIf}
FunctionEnd

; ---------- 卸载初始化（← 模板 :1053-1071 un.onInit） ----------
Function un.onInit
  SetShellVarContext all
  SetRegView 64

  ${GetOptions} $CMDLINE "/P" $PassiveMode
  ${IfNot} ${Errors}
    StrCpy $PassiveMode 1
  ${EndIf}

  ${GetOptions} $CMDLINE "/UPDATE" $UpdateMode
  ${IfNot} ${Errors}
    StrCpy $UpdateMode 1
  ${EndIf}
FunctionEnd

; deleteAppData 复选框注入标准确认页（← 模板 :416-449 un.ConfirmShow，DPI 缩放；
;   ClashWin 三语言无 RTL → RTL 分支省略）
Function un.ConfirmShow
  FindWindow $1 "#32770" "" $HWNDPARENT
  System::Call "user32::GetDpiForWindow(p r1) i .r2"
  IntOp $5 100 * $2
  IntOp $6 400 * $2
  IntOp $7 25 * $2
  IntOp $5 $5 / 96
  IntOp $6 $6 / 96
  IntOp $7 $7 / 96
  ; BS_AUTOCHECKBOX (0x50010003)：点击自动切换勾选态，无需对话框过程处理 BN_CLICKED。
  ; 原 0x50010002 是 BS_CHECKBOX（手动切换），因无 BN_CLICKED 处理导致无法勾选。
  System::Call 'user32::CreateWindowEx(i 0, w "BUTTON", w "$(deleteAppData)", i 0x50010003, i 0, i r5, i r6, i r7, p r1, i0, i0, i0) i .s'
  Pop $DeleteAppDataCheckbox
  SendMessage $HWNDPARENT ${WM_GETFONT} 0 0 $1
  SendMessage $DeleteAppDataCheckbox ${WM_SETFONT} $1 1
FunctionEnd

Function un.ConfirmLeave
  SendMessage $DeleteAppDataCheckbox ${BM_GETCHECK} 0 0 $DeleteAppDataCheckboxState
FunctionEnd

; ---------- 卸载（← 模板 :1073-1288 Section Uninstall） ----------
Section "Uninstall"
  SetRegView 64

  !insertmacro un.CheckIfAppIsRunning "${MAINBINARY}"

  ; 内核进程清理（同安装段：共存豁免，仅本目录）
  nsExec::Exec "powershell -NoProfile -NonInteractive -Command $\"Get-Process mini-mihomo -ErrorAction SilentlyContinue | Where-Object Path -Like '$INSTDIR*' | Stop-Process -Force$\""
  Pop $0

  ; 服务（仅当指向本安装目录才移除，避免破坏 Clash Mini 共用服务——B-D35 裁决保留）
  Call un.RemoveServiceIfOurs

  ; 窗口状态文件（← 模板 :1083-1087）
  DetailPrint "Removing window-state files"
  SetShellVarContext current
  Delete "$APPDATA\${BUNDLEID}\window_state.json"
  Delete "$APPDATA\${BUNDLEID}\window-state.json"
  Delete "$APPDATA\${BUNDLEID}\.window-state.json"

  ; 历史自启动注册表值（← 模板 :1089-1108，品牌位替换）
  StrCpy $R1 "Software\Microsoft\Windows\CurrentVersion\Run"
  ReadRegStr $R2 HKCU "$R1" "Clash Win"
  ${If} $R2 != ""
    DeleteRegValue HKCU "$R1" "Clash Win"
  ${EndIf}
  ReadRegStr $R2 HKLM "$R1" "Clash Win"
  ${If} $R2 != ""
    DeleteRegValue HKLM "$R1" "Clash Win"
  ${EndIf}
  ReadRegStr $R2 HKCU "$R1" "clash-win"
  ${If} $R2 != ""
    DeleteRegValue HKCU "$R1" "clash-win"
  ${EndIf}
  ReadRegStr $R2 HKLM "$R1" "clash-win"
  ${If} $R2 != ""
    DeleteRegValue HKLM "$R1" "clash-win"
  ${EndIf}
  SetShellVarContext all

  ; 程序文件（← 模板 :1116-1129）
  Delete "$INSTDIR\${MAINBINARY}"
  Delete "$INSTDIR\用户必读.txt"
  Delete "$INSTDIR\uninstall.exe"
  RMDir /r "$INSTDIR\sidecar"
  RMDir /r "$INSTDIR\resources"
  RMDir /r "$INSTDIR\locales"
  RMDir "$INSTDIR"

  ; 快捷方式（非更新模式；← 模板 :1155-1181：Unpin + IsShortcutTarget 守卫）
  ${If} $UpdateMode <> 1
    !insertmacro DeleteAppUserModelId

    Delete "$SMPROGRAMS\${PRODUCTNAME}.lnk"
    !insertmacro IsShortcutTargetCompat "$DESKTOP\${PRODUCTNAME}.lnk" "$INSTDIR\${MAINBINARY}"
    Pop $0
    ${If} $0 = 1
      !insertmacro UnpinShortcut "$DESKTOP\${PRODUCTNAME}.lnk"
      Delete "$DESKTOP\${PRODUCTNAME}.lnk"
    ${EndIf}
  ${EndIf}

  ; 卸载注册表项（perMachine → HKLM，← 模板 :1245-1251）
  DeleteRegKey HKLM "${UNINSTKEY}"

  ; 自启动项（非更新模式保留注册表值；← 模板 :1253-1259；ClashWin
  ;   AutostartManager 的 Run 值名为 "Clash Win"——品牌位替换）
  ${If} $UpdateMode <> 1
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "Clash Win"
  ${EndIf}

  ; 勾选删除应用数据时（← 模板 :1261-1277）：清安装位置/语言注册表 + 应用数据目录
  ${If} $DeleteAppDataCheckboxState = 1
  ${AndIf} $UpdateMode <> 1
    DeleteRegKey HKLM "${MANUPRODUCTKEY}"
    DeleteRegKey /ifempty HKLM "Software\${MANUFACTURER}"
    DeleteRegValue HKCU "${MANUPRODUCTKEY}" "Installer Language"
    DeleteRegKey /ifempty HKCU "${MANUPRODUCTKEY}"
    DeleteRegKey /ifempty HKCU "Software\${MANUFACTURER}"
    SetShellVarContext current
    RMDir /r "$APPDATA\${BUNDLEID}"
    RMDir /r "$LOCALAPPDATA\${BUNDLEID}"
    SetShellVarContext all
  ${EndIf}

  ; 被动/更新模式自动关页（← 模板 :1283-1287）
  ${If} $PassiveMode = 1
  ${OrIf} $UpdateMode = 1
    SetAutoClose true
  ${EndIf}
SectionEnd

; ---------- 服务（Mini 模板 :625-688 StartVergeService/RemoveVergeService 语义） ----------
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
    DetailPrint "${SERVICE_NAME} 已存在（可能与 Clash Mini 共用），跳过安装。"
    ; ← 模板 :630-647：已存在且未运行才启动；已运行则不动
    nsExec::ExecToStack '$SYSDIR\sc.exe query ${SERVICE_NAME} | $SYSDIR\find.exe "RUNNING"'
    Pop $0
    Pop $1
    ${If} $0 != 0
      DetailPrint "Restart ${PRODUCTNAME} Service..."
      nsExec::Exec '$SYSDIR\sc.exe start ${SERVICE_NAME}'
      Pop $0
    ${EndIf}
  ${Else}
    DetailPrint "安装 ${SERVICE_NAME} 服务..."
    ; install_service.rs 在“当前目录”查找 clash-verge-service.exe
    SetOutPath "$INSTDIR\resources"
    ExecWait '"$INSTDIR\resources\clash-verge-service-install.exe"' $0
    SetOutPath "$INSTDIR"
    ${If} $0 != 0
      DetailPrint "警告：clash_verge_service 安装返回 $0，TUN 模式可能不可用。"
      ${If} ${Silent}
      ${Else}
        MessageBox MB_OK|MB_ICONEXCLAMATION "$(STR_SERVICE_FAIL)"
      ${EndIf}
    ${EndIf}
    nsExec::Exec '$SYSDIR\sc.exe start ${SERVICE_NAME}'
    Pop $0
  ${EndIf}
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

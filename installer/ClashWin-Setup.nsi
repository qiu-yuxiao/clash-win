; ==============================================================================
; ClashWin Official NSIS Installer Script
; Pure C++20 / Win32 / Direct2D Windows Native Client
; ==============================================================================

Unicode true
ManifestDPIAware true
ManifestDPIAwareness PerMonitorV2
RequestExecutionLevel admin

SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"

!define PRODUCT_NAME "ClashWin"
!define PRODUCT_VERSION "1.0.0"
!define PRODUCT_PUBLISHER "ClashWin Project"
!define PRODUCT_WEB_SITE "https://github.com/clash-verge-rev/clash-verge-rev"
!define PRODUCT_DIR_REGKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\ClashWin.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define SERVICE_NAME "clash_verge_service"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "..\build\bin\ClashWin-Setup-x64.exe"
InstallDir "$PROGRAMFILES64\ClashWin"
InstallDirRegKey HKLM "${PRODUCT_DIR_REGKEY}" ""

; ------------------------------------------------------------------------------
; Interface Settings
; ------------------------------------------------------------------------------
!define MUI_ABORTWARNING
!define MUI_ICON "..\resources\app.ico"
!define MUI_UNICON "..\resources\app.ico"

; Installer pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

; Finish page with launch option
!define MUI_FINISHPAGE_RUN "$INSTDIR\ClashWin.exe"
!define MUI_FINISHPAGE_RUN_TEXT "立即运行 ${PRODUCT_NAME}"
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Language
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

; ------------------------------------------------------------------------------
; Installer Section
; ------------------------------------------------------------------------------
Section "MainSection" SEC01
    SectionIn RO

    ; 1. 终止可能正在运行的旧实例
    DetailPrint "正在检查并终止正在运行的旧版本..."
    nsExec::Exec 'taskkill /F /T /IM ClashWin.exe'
    nsExec::Exec 'taskkill /F /T /IM mini-mihomo.exe'

    ; 2. 部署主程序
    SetOutPath "$INSTDIR"
    File "..\build\bin\ClashWin.exe"

    ; 3. 部署伴生侧车 (Sidecar) 二进制
    SetOutPath "$INSTDIR\sidecar"
    File "..\sidecar\mini-mihomo.exe"
    File "..\sidecar\clash-verge-service.exe"
    File "..\sidecar\clash-verge-service-install.exe"
    File "..\sidecar\clash-verge-service-uninstall.exe"
    File "..\sidecar\enableLoopback.exe"

    ; 4. 预置注册并静默启动 SCM 系统服务 (免去非管理员普通用户后续提权烦恼)
    DetailPrint "正在配置并预注册 TUN 系统服务 (clash_verge_service)..."
    ExecWait '"$INSTDIR\sidecar\clash-verge-service-install.exe"' $0
    DetailPrint "TUN 服务注册返回状态码: $0"

    ; 5. 创建快捷方式
    SetOutPath "$INSTDIR"
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\ClashWin.exe"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\卸载 ${PRODUCT_NAME}.lnk" "$INSTDIR\Uninstall.exe"
    CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\ClashWin.exe"

    ; 6. 写入注册表卸载信息
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\ClashWin.exe"
    WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\ClashWin.exe,0"
    WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"

    ; 计算安装目录大小
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

; ------------------------------------------------------------------------------
; Uninstaller Section
; ------------------------------------------------------------------------------
Section "Uninstall"
    ; 1. 停止并反注册 SCM 服务
    DetailPrint "正在停止并清理 TUN 系统服务..."
    ExecWait '"$INSTDIR\sidecar\clash-verge-service-uninstall.exe"'

    ; 2. 终止残留进程
    nsExec::Exec 'taskkill /F /T /IM ClashWin.exe'
    nsExec::Exec 'taskkill /F /T /IM mini-mihomo.exe'
    nsExec::Exec 'taskkill /F /T /IM clash-verge-service.exe'

    ; 3. 删除快捷方式
    Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\卸载 ${PRODUCT_NAME}.lnk"
    RMDir "$SMPROGRAMS\${PRODUCT_NAME}"

    ; 4. 清理安装文件
    Delete "$INSTDIR\sidecar\mini-mihomo.exe"
    Delete "$INSTDIR\sidecar\clash-verge-service.exe"
    Delete "$INSTDIR\sidecar\clash-verge-service-install.exe"
    Delete "$INSTDIR\sidecar\clash-verge-service-uninstall.exe"
    Delete "$INSTDIR\sidecar\enableLoopback.exe"
    RMDir "$INSTDIR\sidecar"

    Delete "$INSTDIR\ClashWin.exe"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    ; 5. 清理注册表
    DeleteRegKey HKLM "${PRODUCT_UNINST_KEY}"
    DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"

    SetAutoClose true
SectionEnd

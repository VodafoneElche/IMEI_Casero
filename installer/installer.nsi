; Instalador del Comprobador IMEI (por usuario, sin permisos de administrador)
; makensis -DVERSION=1.2.0 installer/installer.nsi

Unicode true
!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!define APPNAME "Comprobador IMEI"
!define EXE "ComprobadorIMEI.exe"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ComprobadorIMEI"
; Carpeta de la versión anterior (Electron), que se desinstala al instalar esta
!define OLD_DIR "$LOCALAPPDATA\Programs\Comprobador IMEI"
!define OLD_UNINST "Uninstall Comprobador IMEI.exe"

Name "${APPNAME}"
OutFile "..\dist\ComprobadorIMEI-Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\ComprobadorIMEI"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "${APPNAME} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=0 "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=0 "FileDescription" "Instalador de ${APPNAME}"
VIAddVersionKey /LANG=0 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "CompanyName" "Grupo Movil Elche"
VIAddVersionKey /LANG=0 "LegalCopyright" "Uso interno"

!include "MUI2.nsh"
!define MUI_ICON "..\assets\icon.ico"
!define MUI_UNICON "..\assets\icon.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Abrir ${APPNAME}"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Spanish"

Section "Instalar"
  ; Cerrar la app si está abierta
  FindWindow $0 "ComprobadorIMEI.Main"
  StrCmp $0 0 +3
    SendMessage $0 ${WM_CLOSE} 0 0
    Sleep 1500

  ; Quitar la versión anterior (Electron). Conserva el historial y los ajustes del usuario.
  IfFileExists "${OLD_DIR}\${OLD_UNINST}" 0 no_old
    DetailPrint "Desinstalando la versión anterior..."
    ExecWait '"${OLD_DIR}\${OLD_UNINST}" /S /currentuser _?=${OLD_DIR}'
    Delete "${OLD_DIR}\${OLD_UNINST}"
    RMDir /r "${OLD_DIR}"
  no_old:

  SetOutPath "$INSTDIR"
  File "..\dist\${EXE}"
  WriteUninstaller "$INSTDIR\Desinstalar.exe"
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${EXE}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}.lnk" "$INSTDIR\${EXE}"

  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "Grupo Movil Elche"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${EXE}"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Desinstalar.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "URLInfoAbout" "https://github.com/VodafoneElche/IMEI_Casero"
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  FindWindow $0 "ComprobadorIMEI.Main"
  StrCmp $0 0 +3
    SendMessage $0 ${WM_CLOSE} 0 0
    Sleep 1500
  Delete "$INSTDIR\${EXE}"
  Delete "$INSTDIR\${EXE}.old"
  Delete "$INSTDIR\${EXE}.new"
  Delete "$INSTDIR\Desinstalar.exe"
  RMDir "$INSTDIR"
  Delete "$DESKTOP\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}.lnk"
  DeleteRegKey HKCU "${UNINST_KEY}"
  ; El historial y los ajustes (%APPDATA%\Comprobador IMEI) se conservan.
SectionEnd

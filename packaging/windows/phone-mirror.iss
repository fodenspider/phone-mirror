#ifndef StageDir
  #error StageDir is required
#endif
#ifndef OutputDir
  #define OutputDir "output"
#endif
[Setup]
AppId={{46F6FF42-5D82-49AE-8C20-CE112713AD74}
AppName=Phone Mirror
AppVersion=0.6.1-preview
AppPublisher=Phone Mirror Project
AppPublisherURL=https://github.com/fodenspider/phone-mirror
DefaultDirName={localappdata}\Programs\PhoneMirror
DefaultGroupName=Phone Mirror
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=PhoneMirror-0.6.1-preview-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\PhoneMirror.exe
[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; Flags: unchecked
[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\Phone Mirror"; Filename: "{app}\PhoneMirror.exe"
Name: "{autodesktop}\Phone Mirror"; Filename: "{app}\PhoneMirror.exe"; Tasks: desktopicon
[Run]
Filename: "{app}\PhoneMirror.exe"; Description: "打开 Phone Mirror"; Flags: nowait postinstall skipifsilent

; Project-authored translations for the visible installation flow.
; Untranslated exceptional system errors retain Inno Setup's default text.
[Messages]
SetupAppTitle=安装
SetupWindowTitle=安装 — %1
UninstallAppTitle=卸载
UninstallAppFullTitle=卸载 %1
InformationTitle=提示
ConfirmTitle=确认
ErrorTitle=错误
ButtonBack=< 上一步(&B)
ButtonNext=下一步(&N) >
ButtonInstall=安装(&I)
ButtonOK=确定
ButtonCancel=取消
ButtonYes=是(&Y)
ButtonNo=否(&N)
ButtonFinish=完成(&F)
ButtonBrowse=浏览(&B)…
ButtonWizardBrowse=浏览(&R)…
ClickNext=点击“下一步”继续，或点击“取消”退出安装。
WelcomeLabel1=欢迎安装 [name]
WelcomeLabel2=将把 [name/ver] 安装到这台电脑。%n%n建议先关闭其他应用程序，再继续安装。
WizardSelectDir=选择安装位置
SelectDirDesc=要将 [name] 安装在哪里？
SelectDirLabel3=[name] 将安装到以下文件夹。
SelectDirBrowseLabel=点击“下一步”继续；如需更换文件夹，请点击“浏览”。
DiskSpaceMBLabel=至少需要 [mb] MB 可用空间。
DiskSpaceGBLabel=至少需要 [gb] GB 可用空间。
WizardSelectProgramGroup=选择开始菜单文件夹
SelectStartMenuFolderDesc=要将快捷方式放在哪里？
SelectStartMenuFolderLabel3=将在以下开始菜单文件夹中创建快捷方式。
SelectStartMenuFolderBrowseLabel=点击“下一步”继续；如需更换文件夹，请点击“浏览”。
NoProgramGroupCheck2=不创建开始菜单文件夹(&D)
WizardSelectTasks=其他选项
SelectTasksDesc=还需要完成哪些操作？
SelectTasksLabel2=请选择安装 [name] 时需要执行的其他操作，然后点击“下一步”。
WizardReady=准备安装
ReadyLabel1=现在可以开始将 [name] 安装到电脑。
ReadyLabel2a=点击“安装”继续，或点击“上一步”检查和修改设置。
ReadyLabel2b=点击“安装”继续。
ReadyMemoDir=安装位置：
ReadyMemoGroup=开始菜单文件夹：
ReadyMemoTasks=其他选项：
WizardInstalling=正在安装
InstallingLabel=正在将 [name] 安装到电脑，请稍候。
FinishedHeadingLabel=[name] 安装完成
FinishedLabelNoIcons=[name] 已安装到电脑。
FinishedLabel=[name] 已安装到电脑，可通过快捷方式打开。
ClickFinish=点击“完成”退出安装。
ExitSetupTitle=退出安装
ExitSetupMessage=安装尚未完成。现在退出将不会安装程序。%n%n你可以稍后重新运行安装程序。%n%n确定退出吗？
ConfirmUninstall=确定卸载 %1 及其所有组件吗？
UninstallStatusLabel=正在卸载 %1，请稍候。
UninstalledAll=%1 已从电脑卸载。

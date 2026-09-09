; ============================================================================
;  8.176：Ember のインストーラー（Phase 217）
;
;  Inno Setup 6 用のスクリプトです。https://jrsoftware.org/isinfo.php
;
;  **手で開く必要はありません。** `Tools/Package-Ember.ps1` が、
;  Inno Setup が入っていれば自動で呼びます。
;
;  手で作るなら：
;      "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" Tools\Ember.iss
;
;  ----------------------------------------------------------------------------
;  決めてあること
;  ----------------------------------------------------------------------------
;
;  - **管理者権限を要求しません**（`PrivilegesRequired=lowest`）。
;    DAWはユーザーのものなので、Program Files へ入れる理由がありません。
;    UACのダイアログが1つ減るぶん、初回の印象がよくなります
;
;  - **`%APPDATA%\Ember` を消します**（Phase 219／本人の方針）。
;
;    ただし**訊いてから消します。** あそこには設定だけでなく、
;    **自分で保存したプラグインのプリセット**（Presets\）と
;    **オートセーブの控え**が入っています。落ちたあとに残っている
;    最後の1つ、ということがあり得るので、黙って消すのは割に合いません。
;
;    **既定の答えは「はい」**なので、方針どおり消えます。
;    訊かずに消すなら、下の `CurUninstallStepChanged` から MsgBox の行を外してください。
;
;    **`ドキュメント\Ember Recordings` と `Ember Templates` は消しません。**
;    あちらは利用者が作ったファイルそのもので、アプリの設定ではありません
;
;  - **プロジェクトの拡張子（.em1）を関連付けます。** ダブルクリックで開けると、
;    「どれを開くか」の画面を通らずに済みます（仕様書5.1）
; ============================================================================

#define AppName        "Ember"
#define AppPublisher   "Ember"
#define AppURL         "https://github.com/"
#define AppExeName     "Ember.exe"

; バージョンは Package-Ember.ps1 が /DAppVersion=... で渡します
#ifndef AppVersion
  #define AppVersion   "0.1.0"
#endif

; ソースの場所も同じく外から渡します（既定はリポジトリ直下からの相対）
#ifndef StageDir
  #define StageDir     "..\out-ember\PersonalDAW_artefacts\Release"
#endif

[Setup]
AppId={{7E2C1A64-3B5D-4C2E-9F13-EM0B3R000001}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\Packaging\Ember\LICENSE.txt
OutputDir=..\out-ember\installer
OutputBaseFilename=Ember-{#AppVersion}-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; **管理者権限を要求しない**（上の説明）
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

; 署名していないので、発行元は「不明」と出ます。READMEに書いてあります
UninstallDisplayIcon={app}\{#AppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate";   Description: "Associate .em1 project files with Ember";     GroupDescription: "File associations:"

[Files]
Source: "{#StageDir}\{#AppExeName}";        DestDir: "{app}"; Flags: ignoreversion
Source: "..\Packaging\Ember\README.md";     DestDir: "{app}"; Flags: ignoreversion
Source: "..\Packaging\Ember\CHANGELOG.md";  DestDir: "{app}"; Flags: ignoreversion
Source: "..\Packaging\Ember\LICENSE.txt";   DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}";        Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}";  Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Registry]
; .em1 の関連付け。**HKCU へ書きます**（管理者権限を要らなくするため）
Root: HKCU; Subkey: "Software\Classes\.em1";                             ValueType: string; ValueName: ""; ValueData: "Ember.Project"; Flags: uninsdeletevalue; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\Ember.Project";                    ValueType: string; ValueName: ""; ValueData: "Ember Project";  Flags: uninsdeletekey;   Tasks: associate
Root: HKCU; Subkey: "Software\Classes\Ember.Project\DefaultIcon";        ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExeName},0"; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\Ember.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
{ 8.177：アンインストールで %APPDATA%\Ember を消す（Phase 219／本人の方針）。

  [UninstallDelete] ではなくコードにしてあるのは、**訊いてから消したい**ためです
  （上の説明。プリセットとオートセーブの控えが入っています）。
  無人アンインストール（/SILENT）のときは訊かずに消します——
  訊けないので、方針どおりに倒します。 }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    DataDir := ExpandConstant('{userappdata}\Ember');

    if DirExists(DataDir) then
    begin
      if UninstallSilent or
         (MsgBox('Also remove your Ember settings, saved plugin presets and auto-save backups?' + #13#10 + #13#10 +
                 DataDir + #13#10 + #13#10 +
                 'Your projects and recordings are not affected.',
                 mbConfirmation, MB_YESNO) = IDYES) then
      begin
        DelTree(DataDir, True, True, True);
      end;
    end;
  end;
end;

; Inno Setup 6.3+, production x64 VST3 installer.
[Setup]
AppId={{4B23C65D-8422-4A8F-9EC7-67FA1BD52497}
AppName=FLOWALIGN
AppVersion={#PluginVersion}
AppPublisher=FLOWMINNERS
AppPublisherURL=https://github.com/Abynarc/FLOWALIGN
DefaultDirName={autopf}\FLOWMINNERS\FLOWALIGN
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
PrivilegesRequired=admin
OutputDir={#DistDir}
OutputBaseFilename=FLOWALIGN-{#PluginVersion}-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=FLOWALIGN VST3
CloseApplications=no
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
russian.WelcomeLabel2=Установщик добавит FLOWALIGN VST3 для 64-разрядных секвенсоров Windows 10 и 11.%n%nСохраните проекты и закройте FL Studio и другие аудиопрограммы перед продолжением.%n%nПредыдущая версия плагина будет скопирована в резервную папку. Аудиофайлы и проекты не изменяются.

[Files]
Source: "{#PluginDir}\*"; DestDir: "{commoncf64}\VST3\FLOWALIGN.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DocPath}"; DestDir: "{app}"; DestName: "Установка.txt"; Flags: ignoreversion

[Code]
var
  Prepared: Boolean;

function CopyBackup(const Source, Dest: String): Boolean;
var
  Entry: TFindRec;
  FromName, ToName: String;
begin
  Result := ForceDirectories(Dest);
  if not Result then Exit;
  if FindFirst(Source + '\*', Entry) then
  begin
    try
      repeat
        if (Entry.Name <> '.') and (Entry.Name <> '..') then
        begin
          FromName := Source + '\' + Entry.Name;
          ToName := Dest + '\' + Entry.Name;
          if (Entry.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
            Result := CopyBackup(FromName, ToName)
          else
          begin
            Result := FileCopy(FromName, ToName, True);
            if Result then Result := CompareText(GetSHA256OfFile(FromName), GetSHA256OfFile(ToName)) = 0;
          end;
          if not Result then Exit;
        end;
      until not FindNext(Entry);
    finally
      FindClose(Entry);
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Existing, Backup: String;
begin
  Result := '';
  if Prepared then Exit;
  Existing := ExpandConstant('{commoncf64}\VST3\FLOWALIGN.vst3');
  if DirExists(Existing) then
  begin
    Backup := ExpandConstant('{commonappdata}\FLOWMINNERS\FLOWALIGN\Backups\') +
      GetDateTimeString('yyyymmdd-hhnnss', '-', ':') + '-' + IntToStr(GetTickCount) + '\FLOWALIGN.vst3';
    if not CopyBackup(Existing, Backup) then
    begin
      Result := 'Не удалось проверить резервную копию FLOWALIGN. Закройте аудиопрограммы и проверьте доступ к папке ProgramData.';
      Exit;
    end;
    Log('Verified backup: ' + Backup);
  end;
  Prepared := True;
end;

#include "{#ManifestInclude}"

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then VerifyPayload;
end;

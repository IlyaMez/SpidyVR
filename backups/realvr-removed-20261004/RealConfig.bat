@ECHO OFF

SET pshell=powershell
WHERE /Q %pshell%
IF NOT ERRORLEVEL 1 GOTO havepshell
SET pshell=%SYSTEMROOT%\System32\WindowsPowerShell\v1.0\powershell
IF EXIST %pshell%.exe GOTO havepshell
ECHO ERROR: PowerShell is needed for this batch file, but it cannot be detected
ECHO        on your system (it should be preinstalled on Windows 10 since version
ECHO        1607). You should either check your Windows installation or go to this
ECHO        Microsoft page and install PowerShell yourself:
ECHO        https://tinyurl.com/y85wsdr4
ECHO(
GOTO abort

:havepshell
IF "%1"=="as_admin" GOTO isadmin
FOR /F "tokens=* USEBACKQ" %%F IN (`%pshell% "[Environment]::GetFolderPath('Personal')"`) DO SET doc=%%F\
FOR /F "tokens=* USEBACKQ" %%F IN (`%pshell% "(new-object -COM Shell.Application).Namespace(0x05).Self.Path"`) DO SET doc1=%%F\
SET "doc2=%USERPROFILE%\Documents\"
IF "%doc1%"=="\" SET "doc1=%doc2%"
IF "%doc%"=="\" SET "doc=%doc1%"
SET "loc=%LOCALAPPDATA%"
SET "app=%APPDATA%"
SET "usrprf=%USERPROFILE%"
SET "prog=%PROGRAMDATA%\RealVR"

IF NOT EXIST RealRepo\elevate.exe GOTO badextract
RealRepo\elevate -c %0 as_admin "%doc%" "%doc1%" "%doc2%" "%loc%" "%app%" "%usrprf%" "%prog%"
IF NOT ERRORLEVEL 1 GOTO exitnow
ECHO ERROR: The installer needs administrative privileges to complete. Please
ECHO        make sure to answer Yes at the User Account Control (UAC) prompt.
ECHO        After the installation completes successfully, it will no longer
ECHO        be necessary to run the game or the mod as admin.
ECHO(
GOTO abort

:isadmin
SET doc=%2
SET doc1=%3
SET doc2=%4
SET loc=%5
SET app=%6
SET usrprf=%7
SET prog=%8
SET "doc=%doc:"=%"
SET "doc1=%doc1:"=%"
SET "doc2=%doc2:"=%"
SET "loc=%loc:"=%"
SET "app=%app:"=%"
SET "usrprf=%usrprf:"=%"
SET "prog=%prog:"=%"

SET base=%~dp0
CD /D %base%

IF EXIST RealRepo GOTO haverepo
:badextract
ECHO ERROR: Mod installation files not found. When unzipping the mod archive,
ECHO        please make sure to overwrite existing files and to use folder
ECHO        names (preserve the zip folder structure).
ECHO(
GOTO abort

:haverepo
FOR /F "tokens=* USEBACKQ" %%F IN (`%pshell% "(Get-Item RealRepo\RealVR64.dll).VersionInfo.FileVersion"`) DO SET modver=%%F
IF NOT EXIST RealVR*.ini GOTO noini
ECHO R.E.A.L. VR settings were found in the game folder,
ECHO presumably from a previous installation of the mod.
CHOICE /C KD /M "Do you want to (K)eep or (D)elete them?"
IF ERRORLEVEL 3 GOTO abort
IF NOT ERRORLEVEL 1 GOTO abort
IF ERRORLEVEL 2 DEL /F /Q RealVR*.ini
ECHO(
ECHO(

:noini
IF EXIST HorizonZeroDawn.exe GOTO installHZD
IF EXIST Cyberpunk2077.exe GOTO installCP2077
IF EXIST eldenring.exe GOTO installELDEN
IF EXIST DarkSoulsRemastered.exe GOTO installDSR
IF EXIST ff7remake_.exe GOTO installFF7R
IF EXIST GWT.exe IF EXIST OpenImageDenoise.dll GOTO installGWT
IF EXIST GWT.exe IF EXIST XCurl.dll GOTO installGWT
IF EXIST Stray-Win64-Shipping.exe GOTO installSTRAY
IF EXIST Ghostrunner-Win64-Shipping.exe GOTO installGRHEL
IF EXIST Maine-Win*-Shipping.exe GOTO installGROUNDED
IF EXIST Spider-Man.exe GOTO installSPIDEY
IF EXIST MilesMorales.exe GOTO installSPIDEYMM
IF EXIST HogwartsLegacy.exe IF EXIST EOSSDK-Win64-Shipping.dll GOTO installHOGWARTS
IF EXIST Oregon-Win*-Shipping.exe GOTO installHOL
IF EXIST AtomicHeart-Win*-Shipping.exe GOTO installAH
IF EXIST u4.exe IF EXIST tll.exe GOTO installULOTC
IF EXIST FarCry6.exe GOTO installFC6
IF EXIST FarCry5.exe GOTO installFC5
IF EXIST FarCry4.exe GOTO installFC4
IF EXIST FarCryNewDawn.exe GOTO installFCND
IF EXIST FCPrimal.exe GOTO installFCP
IF EXIST HorizonForbiddenWest.exe GOTO installHFW
IF EXIST watch_dogs.exe GOTO installWD1
IF EXIST GhostOfTsushima.exe GOTO installGOT
IF EXIST WatchDogs2.exe GOTO installWD2
IF EXIST Outlaws.exe GOTO installSWO
IF EXIST Outlaws_Plus.exe GOTO installSWO
IF EXIST afop.exe GOTO installAFOP
IF EXIST afop_plus.exe GOTO installAFOP
IF EXIST TheGreatCircle.exe GOTO installINDY
IF EXIST ff7rebirth_.exe GOTO installFF7RB
IF EXIST DarkSoulsII.exe GOTO installDS2
IF EXIST KingdomCome.exe GOTO installKCD2
IF EXIST DarkSoulsIII.exe GOTO installDS3
IF EXIST DOOMEternalx64vk.exe GOTO installDOOME
IF EXIST DOOMTheDarkAges.exe GOTO installDOOMTDA
IF EXIST DeathStranding.exe GOTO installDSDC
IF EXIST ds.exe GOTO installDSDC
IF EXIST tlou-i.exe GOTO installTLOU1
IF EXIST tlou-ii.exe GOTO installTLOU2
IF EXIST WatchDogsLegion.exe GOTO installWDL
IF EXIST HorizonZeroDawnRemastered.exe GOTO installHZDR
IF EXIST Spider-Man2.exe GOTO installSPIDEY2
ECHO ERROR: This folder does not seem to belong to any of the supported games.
ECHO        Please make sure that the mod zip archive is extracted to the very
ECHO        same folder where the main game executable is found, and not for
ECHO        example into a subfolder.
ECHO(
GOTO abort


:installHZD
ECHO ******************************************
ECHO * R.E.A.L. VR mod for Horizon: Zero Dawn *
ECHO ******************************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%HorizonZeroDawn.exe"

ECHO Installing graphics settings preset...
SET sub=Horizon Zero Dawn\Saved Game\profile\
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc1%%sub%" SET "doc=%doc1%"
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc2%%sub%" SET "doc=%doc2%"
SET "dst=%doc%%sub%"
IF EXIST "%dst%" (
    IF EXIST "%dst%graphicsconfig.ini" (
        IF NOT EXIST "%dst%graphicsconfig_ori.ini" REN "%dst%graphicsconfig.ini" graphicsconfig_ori.ini
    ) ELSE (
        ECHO WARNING: Unable to find previous graphics settings file
        ECHO          "%dst%graphicsconfig.ini"
    )
) ELSE GOTO neverrun
COPY /Y "RealRepo\HZD\Settings\graphicsconfig.ini" "%dst%"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==HZD IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installCP2077
ECHO **************************************
ECHO * R.E.A.L. VR mod for Cyberpunk 2077 *
ECHO **************************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%Cyberpunk2077.exe"

ECHO Installing graphics settings preset...
SET sub=\CD Projekt Red\Cyberpunk 2077\
SET "dst=%loc%%sub%"
IF EXIST "%dst%UserSettings.json" (
    IF NOT EXIST "%dst%UserSettings_ori.json" COPY /Y "%dst%UserSettings.json" "%dst%UserSettings_ori.json"
) ELSE GOTO neverrun
REN "%dst%UserSettings.json" UserSettings_src.json
RealRepo\ReplaceJSON "%dst%UserSettings_src.json" RealRepo\CP2077\Settings\option.replace "%dst%UserSettings.json"
DEL /F /Q "%dst%UserSettings_src.json"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==CP2077 IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installELDEN
ECHO **********************************
ECHO * R.E.A.L. VR mod for Elden Ring *
ECHO **********************************
ECHO(
CHOICE /C UHML /M "Select (U)ltra, (H)igh, (M)edium or (L)ow config"
IF ERRORLEVEL 5 GOTO abort
IF NOT ERRORLEVEL 1 GOTO abort
IF ERRORLEVEL 1 SET cfg=1ultra
IF ERRORLEVEL 2 SET cfg=2high
IF ERRORLEVEL 3 SET cfg=3medium
IF ERRORLEVEL 4 SET cfg=4low

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%eldenring.exe"

ECHO Installing graphics settings preset...
SET sub=\EldenRing\
SET "dst=%app%%sub%"
IF EXIST "%dst%GraphicsConfig.xml" (
    IF NOT EXIST "%dst%GraphicsConfig_ori.xml" COPY /Y "%dst%GraphicsConfig.xml" "%dst%GraphicsConfig_ori.xml"
) ELSE GOTO neverrun
COPY /Y "RealRepo\ELDEN\Settings\GraphicsConfig_%cfg%.xml" "%dst%GraphicsConfig.xml"

ECHO Copying game specific files...
COPY /Y RealRepo\ELDEN\BackupSaves.bat RealRepo
FOR /F "USEBACKQ" %%F IN ('start_protected_game.exe') DO SET filesize=%%~zF
if %filesize% GTR 1048576 (
    IF EXIST start_protected_game_ori.exe DEL /F /Q start_protected_game_ori.exe
    REN start_protected_game.exe start_protected_game_ori.exe
    COPY /Y RealRepo\ELDEN\start_protected_game.exe .
)
COPY /Y RealRepo\RealVR64.dll .
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==ELDEN IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installDSR
ECHO **********************************************
ECHO * R.E.A.L. VR mod for Dark Souls: Remastered *
ECHO **********************************************
ECHO(

ECHO WARNING: Online features for Dark Souls: Remastered have been reactivated.
ECHO          From initial testing, it appears that it's possible to play online
ECHO          with the VR mod. However that could be interpreted as cheating
ECHO          and/or violating the EULA, which might lead to your account being
ECHO          temporarily or even permanently banned.
ECHO          Playing in offline mode should be safe.
ECHO(
ECHO Neither the author of the R.E.A.L. VR mod nor Patreon can be held responsible
ECHO if anything happens to your account when playing the modded game. Enter the
ECHO word "Yes" without the quotes if you accept this and take full responsibility
ECHO for your actions, anything else to quit without changing the game installation.

SET /P accept=">: "
IF /I NOT %accept%==Yes GOTO abort

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%DarkSoulsRemastered.exe"

ECHO Installing graphics settings preset...
SET sub=\FromSoftware\NBGI\DarkSouls\
SET "dst=%loc%%sub%"
IF EXIST "%dst%DarkSouls.ini" (
    IF NOT EXIST "%dst%DarkSouls_ori.ini" COPY /Y "%dst%DarkSouls.ini" "%dst%DarkSouls_ori.ini"
) ELSE GOTO neverrun
COPY /Y "RealRepo\DSR\Settings\DarkSouls.ini" "%dst%DarkSouls.ini"

ECHO Copying game specific files...
COPY /Y RealRepo\DSR\BackupSaves.bat RealRepo
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==DSR IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installFF7R
ECHO ***********************************************************
ECHO * R.E.A.L. VR mod for Final Fantasy VII Remake Intergrade *
ECHO ***********************************************************

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%ff7remake_.exe"

ECHO Installing graphics settings preset...
SET sub=My Games\FINAL FANTASY VII REMAKE\
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc1%%sub%" SET "doc=%doc1%"
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc2%%sub%" SET "doc=%doc2%"
SET "dst=%doc%%sub%"
SET haveprofile=no
IF NOT EXIST "%dst%" GOTO checkFF7Rprofile
FOR /F "delims=" %%D IN ('DIR /AD /B "%dst%EOS"') DO (
    COPY /Y "RealRepo\FF7R\Settings\ff7remakedevice.sav" "%dst%EOS\%%D"
    SET haveprofile=yes
)
FOR /F "delims=" %%D IN ('DIR /AD /B "%dst%Steam"') DO (
    COPY /Y "RealRepo\FF7R\Settings\ff7remakedevice.sav" "%dst%Steam\%%D"
    SET haveprofile=yes
)
:checkFF7Rprofile
IF %haveprofile%==no GOTO neverrun
SET "wne=%dst%Saved\Config\WindowsNoEditor\"
MKDIR "%wne%"
IF NOT EXIST "%wne%Engine_ori.ini" COPY /Y "%wne%Engine.ini" "%wne%Engine_ori.ini"
COPY /Y "RealRepo\FF7R\Settings\Engine.ini" "%wne%"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==FF7R IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installGWT
ECHO ****************************************
ECHO * R.E.A.L. VR mod for Ghostwire: Tokyo *
ECHO ****************************************

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%GWT.exe"

ECHO Installing graphics settings preset...
SET sub=\Saved Games\TangoGameworks\
SET "dst=%usrprf%%sub%"
SET haveprofile=no
IF NOT EXIST "%dst%" GOTO neverrun
SET "wne=\Saved\Config\WindowsNoEditor\"
FOR /F "delims=" %%D IN ('DIR /AD /B "%dst%"') DO (
    IF NOT EXIST "%dst%%%D%wne%GameUserSettings_ori.ini" COPY /Y "%dst%%%D%wne%GameUserSettings.ini" "%dst%%%D%wne%GameUserSettings_ori.ini"
    COPY /Y "RealRepo\GWT\Settings\GameUserSettings.ini" "%dst%%%D%wne%"
    SET haveprofile=yes
)
IF %haveprofile%==no GOTO neverrun

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
XCOPY /E /I /Q /Y RealRepo\GWT\reshade .
IF NOT EXIST RealVR.ini COPY /Y RealRepo\GWT\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==GWT IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installSTRAY
ECHO *****************************
ECHO * R.E.A.L. VR mod for Stray *
ECHO *****************************

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%Stray-Win64-Shipping.exe"

ECHO Installing graphics settings preset...
SET sub=\Hk_project\Saved\Config\WindowsNoEditor\
SET "dst=%loc%%sub%"
IF NOT EXIST "%dst%" GOTO neverrun
IF NOT EXIST "%dst%GameUserSettings_ori.ini" COPY /Y "%dst%GameUserSettings.ini" "%dst%GameUserSettings_ori.ini"
COPY /Y "RealRepo\STRAY\Settings\GameUserSettings.ini" "%dst%"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==STRAY IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installGRHEL
ECHO *****************************************************
ECHO * R.E.A.L. VR mod for Ghostrunner: Complete Edition *
ECHO *****************************************************

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%Ghostrunner-Win64-Shipping.exe"

ECHO Installing graphics settings preset...
SET sub=\Ghostrunner\Saved\Config\WindowsNoEditor\
SET "dst=%loc%%sub%"
IF NOT EXIST "%dst%" GOTO neverrun
IF NOT EXIST "%dst%GameUserSettings_ori.ini" COPY /Y "%dst%GameUserSettings.ini" "%dst%GameUserSettings_ori.ini"
COPY /Y "RealRepo\GRHEL\Settings\GameUserSettings.ini" "%dst%"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==GRHEL IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installGROUNDED
ECHO ********************************
ECHO * R.E.A.L. VR mod for Grounded *
ECHO ********************************

ECHO Fixing folder permissions... (note: for the PC Game Pass version, getting an
ECHO Access Denied error that makes processing fail for 1 file is normal here)
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%Maine-Win64-Shipping.exe"
RealRepo\Vdesync "%base%Maine-WinGDK-Shipping.exe"

ECHO Installing graphics settings preset...
SET sub=\Maine\Saved\Config\WindowsNoEditor\
SET subGDK=\Maine\Saved\Config\WinGDK\
SET "dst=%loc%%sub%"
SET "dstGDK=%loc%%subGDK%"
IF NOT EXIST "%dst%" IF NOT EXIST "%dstGDK%" GOTO neverrun
IF EXIST "%dst%" (
    IF NOT EXIST "%dst%GameUserSettings_ori.ini" COPY /Y "%dst%GameUserSettings.ini" "%dst%GameUserSettings_ori.ini"
    COPY /Y "RealRepo\GROUNDED\Settings\GameUserSettings.ini" "%dst%"
)
IF EXIST "%dstGDK%" (
    IF NOT EXIST "%dstGDK%GameUserSettings_ori.ini" COPY /Y "%dstGDK%GameUserSettings.ini" "%dstGDK%GameUserSettings_ori.ini"
    COPY /Y "RealRepo\GROUNDED\Settings\GameUserSettings.ini" "%dstGDK%"
)

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==GROUNDED IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installSPIDEY
ECHO ******************************************************
ECHO * R.E.A.L. VR mod for Marvel's Spider-Man Remastered *
ECHO ******************************************************

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%Spider-Man.exe"

ECHO Installing graphics settings preset...
REG QUERY "HKCU\Software\Insomniac Games\Marvel's Spider-Man Remastered"
IF ERRORLEVEL 1 GOTO neverrun
REG IMPORT RealRepo\SPIDEY\Settings\SPIDEY.txt

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==SPIDEY IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installSPIDEYMM
ECHO **********************************************************
ECHO * R.E.A.L. VR mod for Marvel's Spider-Man: Miles Morales *
ECHO **********************************************************

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%MilesMorales.exe"

ECHO Installing graphics settings preset...
REG QUERY "HKCU\Software\Insomniac Games\Marvel's Spider-Man Miles Morales"
IF ERRORLEVEL 1 GOTO neverrun
REG IMPORT RealRepo\SPIDEY\Settings\SPIDEYMM.txt

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==SPIDEY IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installHOGWARTS
ECHO ***************************************
ECHO * R.E.A.L. VR mod for Hogwarts Legacy *
ECHO ***************************************

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%HogwartsLegacy.exe"

ECHO Installing graphics settings preset...
SET sub=\Hogwarts Legacy\Saved\Config\WindowsNoEditor\
SET "dst=%loc%%sub%"
IF NOT EXIST "%dst%" GOTO neverrun
IF NOT EXIST "%dst%GameUserSettings_ori.ini" COPY /Y "%dst%GameUserSettings.ini" "%dst%GameUserSettings_ori.ini"
COPY /Y "RealRepo\HOGWARTS\Settings\GameUserSettings.ini" "%dst%"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==HOGWARTS IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installHOL
ECHO ************************************
ECHO * R.E.A.L. VR mod for High On Life *
ECHO ************************************

ECHO Fixing folder permissions... (note: for the PC Game Pass version, getting an
ECHO Access Denied error that makes processing fail for 1 file is normal here)
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Removing conflicting VR hook...
DEL /F /Q ..\..\..\Engine\Binaries\ThirdParty\OpenXR\win64\openxr_loader.dll

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%Oregon-Win64-Shipping.exe"
RealRepo\Vdesync "%base%Oregon-WinGDK-Shipping.exe"

ECHO Installing graphics settings preset...
SET sub=\Oregon\Saved\Config\WindowsNoEditor\
SET subGDK=\Oregon\Saved\Config\WinGDK\
SET "dst=%loc%%sub%"
SET "dstGDK=%loc%%subGDK%"
IF NOT EXIST "%dst%" IF NOT EXIST "%dstGDK%" GOTO neverrun
IF EXIST "%dst%" (
    IF NOT EXIST "%dst%GameUserSettings_ori.ini" COPY /Y "%dst%GameUserSettings.ini" "%dst%GameUserSettings_ori.ini"
    COPY /Y "RealRepo\HOL\Settings\GameUserSettings.ini" "%dst%"
)
IF EXIST "%dstGDK%" (
    IF NOT EXIST "%dstGDK%GameUserSettings_ori.ini" COPY /Y "%dstGDK%GameUserSettings.ini" "%dstGDK%GameUserSettings_ori.ini"
    COPY /Y "RealRepo\HOL\Settings\GameUserSettings.ini" "%dstGDK%"
)

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==HOL IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installAH
ECHO ************************************
ECHO * R.E.A.L. VR mod for Atomic Heart *
ECHO ************************************

ECHO Fixing folder permissions... (note: for the PC Game Pass version, getting an
ECHO Access Denied error that makes processing fail for 1 file is normal here)
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%AtomicHeart-Win64-Shipping.exe"
RealRepo\Vdesync "%base%AtomicHeart-WinGDK-Shipping.exe"

ECHO Installing graphics settings preset...
SET sub=\AtomicHeart\Saved\Config\WindowsNoEditor\
SET subGDK=\AtomicHeart\Saved\Config\WinGDK\
SET "dst=%loc%%sub%"
SET "dstGDK=%loc%%subGDK%"
IF NOT EXIST "%dst%" IF NOT EXIST "%dstGDK%" GOTO neverrun
IF EXIST "%dst%" (
    IF NOT EXIST "%dst%GameUserSettings_ori.ini" COPY /Y "%dst%GameUserSettings.ini" "%dst%GameUserSettings_ori.ini"
    COPY /Y "RealRepo\AH\Settings\GameUserSettings.ini" "%dst%"
)
IF EXIST "%dstGDK%" (
    IF NOT EXIST "%dstGDK%GameUserSettings_ori.ini" COPY /Y "%dstGDK%GameUserSettings.ini" "%dstGDK%GameUserSettings_ori.ini"
    COPY /Y "RealRepo\AH\Settings\GameUserSettings.ini" "%dstGDK%"
)

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==AH IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installULOTC
ECHO ***************************************************************
ECHO * R.E.A.L. VR mod for Uncharted: Legacy of Thieves Collection *
ECHO ***************************************************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%u4.exe"
RealRepo\Vdesync "%base%tll.exe"

ECHO Installing graphics settings preset...
SET sub=\Uncharted4_data\
SET "dst=%base%%sub%"
IF EXIST "%dst%screeninfo.cfg" (
    IF NOT EXIST "%dst%screeninfo_ori.cfg" COPY /Y "%dst%screeninfo.cfg" "%dst%screeninfo_ori.cfg"
) ELSE GOTO neverrun
RealRepo\rxrepl --file "%dst%screeninfo.cfg" --alter --no-backup --options "RealRepo\ULOTC\Settings\option.replace"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\ULOTC\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==ULOTC IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installFC6
ECHO *********************************
ECHO * R.E.A.L. VR mod for Far Cry 6 *
ECHO *********************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%FarCry6.exe"

ECHO Installing graphics settings preset...
SET sub=My Games\Far Cry 6\
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc1%%sub%" SET "doc=%doc1%"
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc2%%sub%" SET "doc=%doc2%"
SET "dst=%doc%%sub%"
IF EXIST "%dst%gamerprofile.xml" (
    IF NOT EXIST "%dst%gamerprofile_ori.xml" COPY /Y "%dst%gamerprofile.xml" "%dst%gamerprofile_ori.xml"
) ELSE GOTO neverrun
RealRepo\rxrepl --file "%dst%gamerprofile.xml" --alter --no-backup --options "RealRepo\FC6\Settings\option.replace"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==FC6 IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installFC5
ECHO *********************************
ECHO * R.E.A.L. VR mod for Far Cry 5 *
ECHO *********************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%FarCry5.exe"

ECHO Installing graphics settings preset...
SET sub=My Games\Far Cry 5\
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc1%%sub%" SET "doc=%doc1%"
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc2%%sub%" SET "doc=%doc2%"
SET "dst=%doc%%sub%"
IF EXIST "%dst%gamerprofile.xml" (
    IF NOT EXIST "%dst%gamerprofile_ori.xml" COPY /Y "%dst%gamerprofile.xml" "%dst%gamerprofile_ori.xml"
) ELSE GOTO neverrun
RealRepo\rxrepl --file "%dst%gamerprofile.xml" --alter --no-backup --options "RealRepo\FC5\Settings\option.replace"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==FC5 IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installFC4
ECHO *********************************
ECHO * R.E.A.L. VR mod for Far Cry 4 *
ECHO *********************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%FarCry4.exe"

ECHO Installing graphics settings preset...
SET sub=My Games\Far Cry 4\
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc1%%sub%" SET "doc=%doc1%"
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc2%%sub%" SET "doc=%doc2%"
SET "dst=%doc%%sub%"
SET haveprofile=no
IF NOT EXIST "%dst%" GOTO neverrun
FOR /F "delims=" %%D IN ('DIR /AD /B "%dst%"') DO (
    IF EXIST "%dst%%%D\GamerProfile.xml" (
        IF NOT EXIST "%dst%%%D\GamerProfile_ori.xml" COPY /Y "%dst%%%D\GamerProfile.xml" "%dst%%%D\GamerProfile_ori.xml"
        RealRepo\rxrepl --file "%dst%%%D\GamerProfile.xml" --alter --no-backup --options "RealRepo\FC4\Settings\option.replace"
        SET haveprofile=yes
    )
)
IF %haveprofile%==no GOTO neverrun

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==FC4 IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installFCND
ECHO ****************************************
ECHO * R.E.A.L. VR mod for Far Cry New Dawn *
ECHO ****************************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%FarCryNewDawn.exe"

ECHO Installing graphics settings preset...
SET sub=My Games\Far Cry New Dawn\
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc1%%sub%" SET "doc=%doc1%"
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc2%%sub%" SET "doc=%doc2%"
SET "dst=%doc%%sub%"
IF EXIST "%dst%gamerprofile.xml" (
    IF NOT EXIST "%dst%gamerprofile_ori.xml" COPY /Y "%dst%gamerprofile.xml" "%dst%gamerprofile_ori.xml"
) ELSE GOTO neverrun
RealRepo\rxrepl --file "%dst%gamerprofile.xml" --alter --no-backup --options "RealRepo\FCND\Settings\option.replace"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==FCND IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installFCP
ECHO **************************************
ECHO * R.E.A.L. VR mod for Far Cry Primal *
ECHO **************************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%FCPrimal.exe"

ECHO Installing graphics settings preset...
SET sub=My Games\Far Cry Primal\
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc1%%sub%" SET "doc=%doc1%"
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc2%%sub%" SET "doc=%doc2%"
SET "dst=%doc%%sub%"
IF EXIST "%dst%gamerprofile.xml" (
    IF NOT EXIST "%dst%gamerprofile_ori.xml" COPY /Y "%dst%gamerprofile.xml" "%dst%gamerprofile_ori.xml"
) ELSE GOTO neverrun
RealRepo\rxrepl --file "%dst%gamerprofile.xml" --alter --no-backup --options "RealRepo\FCP\Settings\option.replace"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==FCP IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installHFW
ECHO **********************************************
ECHO * R.E.A.L. VR mod for Horizon Forbidden West *
ECHO **********************************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%HorizonForbiddenWest.exe"

ECHO Installing graphics settings preset...
REG QUERY "HKCU\Software\Guerrilla Games\Horizon Forbidden West Complete Edition"
IF ERRORLEVEL 1 GOTO neverrun
REG IMPORT RealRepo\HFW\Settings\HFW.txt

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==HFW IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installWD1
ECHO **********************************
ECHO * R.E.A.L. VR mod for Watch Dogs *
ECHO **********************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%watch_dogs.exe"

ECHO Installing graphics settings preset...
SET sub=My Games\Watch_Dogs\
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc1%%sub%" SET "doc=%doc1%"
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc2%%sub%" SET "doc=%doc2%"
SET "dst=%doc%%sub%"
SET haveprofile=no
IF NOT EXIST "%dst%" GOTO neverrun
FOR /F "delims=" %%D IN ('DIR /AD /B "%dst%"') DO (
    IF EXIST "%dst%%%D\GamerProfile.xml" (
        IF NOT EXIST "%dst%%%D\GamerProfile_ori.xml" COPY /Y "%dst%%%D\GamerProfile.xml" "%dst%%%D\GamerProfile_ori.xml"
        RealRepo\rxrepl --file "%dst%%%D\GamerProfile.xml" --alter --no-backup --options "RealRepo\WD1\Settings\option.replace"
        SET haveprofile=yes
    )
)
IF %haveprofile%==no GOTO neverrun

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==WD1 IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installGOT
ECHO ********************************************************
ECHO * R.E.A.L. VR mod for Ghost of Tsushima DIRECTOR'S CUT *
ECHO ********************************************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%GhostOfTsushima.exe"

ECHO Installing graphics settings preset...
REG QUERY "HKCU\Software\Sucker Punch Productions\Ghost of Tsushima DIRECTOR'S CUT"
IF ERRORLEVEL 1 GOTO neverrun
REG IMPORT RealRepo\GOT\Settings\GOT.txt

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\GOT\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==GOT IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installWD2
ECHO ************************************
ECHO * R.E.A.L. VR mod for Watch Dogs 2 *
ECHO ************************************
ECHO(
CHOICE /C UVHML /M "Select (U)ltra, (V)ery high, (H)igh, (M)edium or (L)ow config"
IF ERRORLEVEL 6 GOTO abort
IF NOT ERRORLEVEL 1 GOTO abort
IF ERRORLEVEL 1 SET cfg=1ultra
IF ERRORLEVEL 2 SET cfg=2veryhigh
IF ERRORLEVEL 3 SET cfg=3high
IF ERRORLEVEL 4 SET cfg=4medium
IF ERRORLEVEL 5 SET cfg=5low

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync and flickering bugs...
RealRepo\Vdesync "%base%WatchDogs2.exe"

ECHO Installing graphics settings preset...
SET sub=My Games\Watch_Dogs 2\
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc1%%sub%" SET "doc=%doc1%"
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc2%%sub%" SET "doc=%doc2%"
SET "dst=%doc%%sub%"

IF EXIST "%dst%WD2_GamerProfile.xml" (
    IF NOT EXIST "%dst%WD2_GamerProfile_ori.xml" COPY /Y "%dst%WD2_GamerProfile.xml" "%dst%WD2_GamerProfile_ori.xml"
) ELSE GOTO neverrun
RealRepo\rxrepl --file "%dst%WD2_GamerProfile.xml" --alter --no-backup --options "RealRepo\WD2\Settings\option_%cfg%.replace"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==WD2 IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installSWO
ECHO *****************************************
ECHO * R.E.A.L. VR mod for Star Wars Outlaws *
ECHO *****************************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%Outlaws.exe"
RealRepo\Vdesync "%base%Outlaws_Plus.exe"

ECHO Installing graphics settings preset...
SET sub=My Games\Outlaws\
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc1%%sub%" SET "doc=%doc1%"
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc2%%sub%" SET "doc=%doc2%"
SET "dst=%doc%%sub%"
IF EXIST "%dst%graphic settings.cfg" (
    IF NOT EXIST "%dst%graphic settings_ori.cfg" COPY /Y "%dst%graphic settings.cfg" "%dst%graphic settings_ori.cfg"
    IF NOT EXIST "%dst%state_ori.cfg" COPY /Y "%dst%state.cfg" "%dst%state_ori.cfg"
) ELSE GOTO neverrun
RealRepo\rxrepl --file "%dst%graphic settings.cfg" --alter --no-backup --options "RealRepo\SWO\Settings\option.replace"
RealRepo\rxrepl --file "%dst%state.cfg" --alter --no-backup --options "RealRepo\SWO\Settings\state.replace"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==SWO IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installAFOP
ECHO ****************************************************
ECHO * R.E.A.L. VR mod for Avatar: Frontiers of Pandora *
ECHO ****************************************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%afop.exe"
RealRepo\Vdesync "%base%afop_plus.exe"

ECHO Installing graphics settings preset...
SET sub=My Games\AFOP\
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc1%%sub%" SET "doc=%doc1%"
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc2%%sub%" SET "doc=%doc2%"
SET "dst=%doc%%sub%"
IF EXIST "%dst%graphic settings.cfg" (
    IF NOT EXIST "%dst%graphic settings_ori.cfg" COPY /Y "%dst%graphic settings.cfg" "%dst%graphic settings_ori.cfg"
    IF NOT EXIST "%dst%persistent_settings_ori.cfg" COPY /Y "%dst%persistent_settings.cfg" "%dst%persistent_settings_ori.cfg"
    IF NOT EXIST "%dst%state_ori.cfg" COPY /Y "%dst%state.cfg" "%dst%state_ori.cfg"
) ELSE GOTO neverrun
RealRepo\rxrepl --file "%dst%graphic settings.cfg" --alter --no-backup --options "RealRepo\AFOP\Settings\option.replace"
RealRepo\rxrepl --file "%dst%persistent_settings.cfg" --alter --no-backup --options "RealRepo\AFOP\Settings\persistent_settings.replace"
RealRepo\rxrepl --file "%dst%state.cfg" --alter --no-backup --options "RealRepo\AFOP\Settings\state.replace"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==AFOP IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installINDY
ECHO **********************************************************
ECHO * R.E.A.L. VR mod for Indiana Jones and the Great Circle *
ECHO **********************************************************
ECHO(
ECHO Version %modver%
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

REM ECHO Working around NVIDIA V-Sync bug...
REM RealRepo\Vdesync "%base%TheGreatCircle.exe"

REM ECHO Installing graphics settings preset...
SET sub=\Saved Games\MachineGames\TheGreatCircle\base\
SET "dst=%usrprf%%sub%"
IF NOT EXIST "%dst%TheGreatCircleConfig.cfg" (
    ECHO(
    ECHO WARNING: The game appears to never have been run. If you haven't set the
    ECHO          game options as explained in the release post, please abort the
    ECHO          installation and do that now or the mod won't work correctly.
    ECHO(
    CHOICE /C AC /M "Do you want to (A)bort or (C)ontinue?"
    IF ERRORLEVEL 3 GOTO abort
    IF NOT ERRORLEVEL 2 GOTO abort
)

ECHO Copying game specific files...
IF NOT EXIST "%prog%" MKDIR "%prog%"
COPY /Y RealRepo\INDY\* "%prog%"
COPY /Y RealRepo\RealVR64.dll "%prog%"
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Configuring Vulkan layers...
%pshell% "Remove-ItemProperty -Path HKCU:\SOFTWARE\Khronos\Vulkan\ImplicitLayers -Name *RealVR64.json" 2>nul
%pshell% "Remove-ItemProperty -Path HKLM:\SOFTWARE\Khronos\Vulkan\ImplicitLayers -Name *RealVR64.json"
REG ADD HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers /V "%prog%\RealVR64.json" /T REG_DWORD /D 0 /F

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==INDY IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installFF7RB
ECHO *************************************************
ECHO * R.E.A.L. VR mod for Final Fantasy VII Rebirth *
ECHO *************************************************

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%ff7rebirth_.exe"

ECHO Installing graphics settings preset...
SET sub=My Games\FINAL FANTASY VII REBIRTH\
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc1%%sub%" SET "doc=%doc1%"
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc2%%sub%" SET "doc=%doc2%"
SET "dst=%doc%%sub%"
SET haveprofile=no
IF NOT EXIST "%dst%" GOTO checkFF7RBprofile
FOR /F "delims=" %%D IN ('DIR /AD /B "%dst%EOS"') DO (
    COPY /Y "RealRepo\FF7RB\Settings\ff7rebirthdevice.sav" "%dst%EOS\%%D"
    SET haveprofile=yes
)
FOR /F "delims=" %%D IN ('DIR /AD /B "%dst%Steam"') DO (
    COPY /Y "RealRepo\FF7RB\Settings\ff7rebirthdevice.sav" "%dst%Steam\%%D"
    SET haveprofile=yes
)
:checkFF7RBprofile
IF %haveprofile%==no GOTO neverrun

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==FF7RB IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installDS2
ECHO ***************************************************************
ECHO * R.E.A.L. VR mod for Dark Souls II: Scholar of the First Sin *
ECHO ***************************************************************
ECHO(

ECHO WARNING: Online features are available for Dark Souls II: Scholar of the
ECHO          First Sin. From initial testing, it appears that it's possible to
ECHO          play online with the VR mod. However that could be interpreted as
ECHO          cheating and/or violating the EULA, which might lead to your
ECHO          account being temporarily or even permanently banned.
ECHO          Playing in offline mode should be safe.
ECHO(
ECHO Neither the author of the R.E.A.L. VR mod nor Patreon can be held responsible
ECHO if anything happens to your account when playing the modded game. Enter the
ECHO word "Yes" without the quotes if you accept this and take full responsibility
ECHO for your actions, anything else to quit without changing the game installation.

SET /P accept=">: "
IF /I NOT %accept%==Yes GOTO abort

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%DarkSoulsII.exe"

ECHO Installing graphics settings preset...
SET sub=\DarkSoulsII\
SET "dst=%app%%sub%"
IF EXIST "%dst%GraphicsConfig_SOFS.xml" (
    IF NOT EXIST "%dst%GraphicsConfig_SOFS_ori.xml" COPY /Y "%dst%GraphicsConfig_SOFS.xml" "%dst%GraphicsConfig_SOFS_ori.xml"
) ELSE GOTO neverrun
COPY /Y "RealRepo\DS2\Settings\GraphicsConfig_SOFS.xml" "%dst%"

ECHO Copying game specific files...
COPY /Y RealRepo\DS2\BackupSaves.bat RealRepo
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==DS2 IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installKCD2
ECHO ****************************************************
ECHO * R.E.A.L. VR mod for Kingdom Come: Deliverance II *
ECHO ****************************************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%KingdomCome.exe"

ECHO Installing graphics settings preset...
SET sub=\Saved Games\kingdomcome2\profiles\default\
SET "dst=%usrprf%%sub%"
IF EXIST "%dst%attributes.xml" (
    IF NOT EXIST "%dst%attributes_ori.xml" COPY /Y "%dst%attributes.xml" "%dst%attributes_ori.xml"
) ELSE GOTO neverrun
RealRepo\rxrepl --file "%dst%attributes.xml" --alter --no-backup --options "RealRepo\KCD2\Settings\option.replace"
SET "dst=%base%..\.."
IF EXIST "%dst%user.cfg" (
    IF NOT EXIST "%dst%user_ori.cfg" COPY /Y "%dst%user.cfg" "%dst%user_ori.cfg"
)
COPY /Y "RealRepo\KCD2\user.cfg" "%dst%"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==KCD2 IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installDS3
ECHO **************************************
ECHO * R.E.A.L. VR mod for Dark Souls III *
ECHO **************************************
ECHO(

ECHO WARNING: Online features are available for Dark Souls III.
ECHO          From initial testing, it appears that it's possible to play
ECHO          online with the VR mod. However that could be interpreted as
ECHO          cheating and/or violating the EULA, which might lead to your
ECHO          account being temporarily or even permanently banned.
ECHO          Playing in offline mode should be safe.
ECHO(
ECHO Neither the author of the R.E.A.L. VR mod nor Patreon can be held responsible
ECHO if anything happens to your account when playing the modded game. Enter the
ECHO word "Yes" without the quotes if you accept this and take full responsibility
ECHO for your actions, anything else to quit without changing the game installation.

SET /P accept=">: "
IF /I NOT %accept%==Yes GOTO abort

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%DarkSoulsIII.exe"

ECHO Installing graphics settings preset...
SET sub=\DarkSoulsIII\
SET "dst=%app%%sub%"
IF EXIST "%dst%GraphicsConfig.xml" (
    IF NOT EXIST "%dst%GraphicsConfig_ori.xml" COPY /Y "%dst%GraphicsConfig.xml" "%dst%GraphicsConfig_ori.xml"
) ELSE GOTO neverrun
COPY /Y "RealRepo\DS3\Settings\GraphicsConfig.xml" "%dst%"

ECHO Copying game specific files...
COPY /Y RealRepo\DS3\BackupSaves.bat RealRepo
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==DS3 IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installDOOME
ECHO ************************************
ECHO * R.E.A.L. VR mod for DOOM Eternal *
ECHO ************************************
ECHO(
ECHO Version %modver%
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

REM ECHO Working around NVIDIA V-Sync bug...
REM RealRepo\Vdesync "%base%DOOMEternalx64vk.exe"

REM ECHO Installing graphics settings preset...
SET sub=\Saved Games\id Software\DOOMEternal\base\
SET "dst=%usrprf%%sub%"
IF NOT EXIST "%dst%DOOMEternalConfig.cfg" (
    ECHO(
    ECHO WARNING: The game appears to never have been run. If you haven't set the
    ECHO          game options as explained in the release post, please abort the
    ECHO          installation and do that now or the mod won't work correctly.
    ECHO(
    CHOICE /C AC /M "Do you want to (A)bort or (C)ontinue?"
    IF ERRORLEVEL 3 GOTO abort
    IF NOT ERRORLEVEL 2 GOTO abort
)

ECHO Copying game specific files...
IF NOT EXIST "%prog%" MKDIR "%prog%"
COPY /Y RealRepo\DOOME\* "%prog%"
COPY /Y RealRepo\RealVR64.dll "%prog%"
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Configuring Vulkan layers...
%pshell% "Remove-ItemProperty -Path HKCU:\SOFTWARE\Khronos\Vulkan\ImplicitLayers -Name *RealVR64.json" 2>nul
%pshell% "Remove-ItemProperty -Path HKLM:\SOFTWARE\Khronos\Vulkan\ImplicitLayers -Name *RealVR64.json"
REG ADD HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers /V "%prog%\RealVR64.json" /T REG_DWORD /D 0 /F

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==DOOME IF EXIST "%%D" RMDIR /S /Q "%%D"
ECHO Checking for sandbox version...
IF EXIST doomSandBox\DOOMSandBox64vk.exe (
    ECHO Duplicating files to sandbox version...
    COPY /Y cudart64_12.dll doomSandBox
    COPY /Y openvr_api.dll doomSandBox
    COPY /Y RealVR.ini doomSandBox
    XCOPY /E /I /Q /Y RealRepo doomSandBox\RealRepo
)
GOTO finish


:installDOOMTDA
ECHO *******************************************
ECHO * R.E.A.L. VR mod for DOOM: The Dark Ages *
ECHO *******************************************
ECHO(
ECHO Version %modver%
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

REM ECHO Working around NVIDIA V-Sync bug...
REM RealRepo\Vdesync "%base%DOOMTheDarkAges.exe"

REM ECHO Installing graphics settings preset...
SET sub=\Saved Games\id Software\DOOMTheDarkAges\base\
SET "dst=%usrprf%%sub%"
IF NOT EXIST "%dst%DOOMTheDarkAgesConfig.cfg" (
    ECHO(
    ECHO WARNING: The game appears to never have been run. If you haven't set the
    ECHO          game options as explained in the release post, please abort the
    ECHO          installation and do that now or the mod won't work correctly.
    ECHO(
    CHOICE /C AC /M "Do you want to (A)bort or (C)ontinue?"
    IF ERRORLEVEL 3 GOTO abort
    IF NOT ERRORLEVEL 2 GOTO abort
)

ECHO Copying game specific files...
IF NOT EXIST "%prog%" MKDIR "%prog%"
COPY /Y RealRepo\DOOMTDA\* "%prog%"
COPY /Y RealRepo\RealVR64.dll "%prog%"
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Configuring Vulkan layers...
%pshell% "Remove-ItemProperty -Path HKCU:\SOFTWARE\Khronos\Vulkan\ImplicitLayers -Name *RealVR64.json" 2>nul
%pshell% "Remove-ItemProperty -Path HKLM:\SOFTWARE\Khronos\Vulkan\ImplicitLayers -Name *RealVR64.json"
REG ADD HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers /V "%prog%\RealVR64.json" /T REG_DWORD /D 0 /F

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==DOOMTDA IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installDSDC
ECHO ******************************************************
ECHO * R.E.A.L. VR mod for Death Stranding Director's Cut *
ECHO ******************************************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%DeathStranding.exe"
RealRepo\Vdesync "%base%ds.exe"

ECHO Installing graphics settings preset...
IF EXIST "%base%settings.cfg" (
    IF NOT EXIST "%base%settings_ori.cfg" COPY /Y "%base%settings.cfg" "%base%settings_ori.cfg"
) ELSE GOTO neverrun
COPY /Y "RealRepo\DSDC\Settings\settings.cfg" "%base%"

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==DSDC IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installTLOU1
ECHO *********************************************
ECHO * R.E.A.L. VR mod for The Last of Us Part I *
ECHO *********************************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%tlou-i.exe"

ECHO Installing graphics settings preset...
SET sub=\Saved Games\The Last of Us Part I\
SET "dst=%usrprf%%sub%"
SET haveprofile=no
IF NOT EXIST "%dst%users" GOTO neverrun
FOR /F "delims=" %%D IN ('DIR /AD /B "%dst%users"') DO (
    IF NOT EXIST "%dst%users\%%D\screeninfo_ori.cfg" COPY /Y "%dst%users\%%D\screeninfo.cfg" "%dst%users\%%D\screeninfo_ori.cfg"
    RealRepo\rxrepl --file "%dst%users\%%D\screeninfo.cfg" --alter --no-backup --options "RealRepo\TLOU1\Settings\option.replace"
    SET haveprofile=yes
)
IF %haveprofile%==no GOTO neverrun

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==TLOU1 IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installTLOU2
ECHO *********************************************************
ECHO * R.E.A.L. VR mod for The Last of Us Part II Remastered *
ECHO *********************************************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%tlou-ii.exe"

ECHO Installing graphics settings preset...
REG QUERY "HKCU\Software\Naughty Dog\The Last of Us Part II"
IF ERRORLEVEL 1 GOTO neverrun
REG IMPORT RealRepo\TLOU2\Settings\TLOU2.txt

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==TLOU2 IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installWDL
ECHO ******************************************
ECHO * R.E.A.L. VR mod for Watch Dogs: Legion *
ECHO ******************************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%WatchDogsLegion.exe"

ECHO Installing graphics settings preset...
SET sub=My Games\Watch Dogs Legion\
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc1%%sub%" SET "doc=%doc1%"
IF NOT EXIST "%doc%%sub%" IF EXIST "%doc2%%sub%" SET "doc=%doc2%"
SET "dst=%doc%%sub%"
SET haveprofile=no
IF NOT EXIST "%dst%" GOTO neverrun
FOR /F "delims=" %%D IN ('DIR /AD /B "%dst%"') DO (
    IF EXIST "%dst%%%D\WD3_GamerProfile.xml" (
        IF NOT EXIST "%dst%%%D\WD3_GamerProfile_ori.xml" COPY /Y "%dst%%%D\WD3_GamerProfile.xml" "%dst%%%D\WD3_GamerProfile_ori.xml"
        RealRepo\rxrepl --file "%dst%%%D\WD3_GamerProfile.xml" --alter --no-backup --options "RealRepo\WDL\Settings\option.replace"
        SET haveprofile=yes
    )
)
IF %haveprofile%==no GOTO neverrun

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==WDL IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installHZDR
ECHO *****************************************************
ECHO * R.E.A.L. VR mod for Horizon: Zero Dawn Remastered *
ECHO *****************************************************
ECHO(

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%HorizonZeroDawnRemastered.exe"

ECHO Installing graphics settings preset...
REG QUERY "HKCU\Software\Guerrilla Games\Horizon Zero Dawn Remastered"
IF ERRORLEVEL 1 GOTO neverrun
REG IMPORT RealRepo\HZDR\Settings\HZDR.txt

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll
IF NOT EXIST RealVR.ini COPY /Y RealRepo\HZDR\RealVR.ini .

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==HZDR IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:installSPIDEY2
ECHO *********************************************
ECHO * R.E.A.L. VR mod for Marvel's Spider-Man 2 *
ECHO *********************************************

ECHO Fixing folder permissions...
ICACLS . /GRANT *S-1-5-32-545:(OI)(CI)F /T /Q

ECHO Working around NVIDIA V-Sync bug...
RealRepo\Vdesync "%base%Spider-Man2.exe"

ECHO Installing graphics settings preset...
REG QUERY "HKCU\Software\Insomniac Games\Marvel's Spider-Man 2"
IF ERRORLEVEL 1 GOTO neverrun
REG IMPORT RealRepo\SPIDEY2\Settings\SPIDEY2.txt

ECHO Copying game specific files...
COPY /Y RealRepo\RealVR64.dll dxgi.dll

ECHO Cleaning up...
FOR /D /R %%D IN (RealRepo\*) DO IF /I NOT %%~nD==SPIDEY2 IF EXIST "%%D" RMDIR /S /Q "%%D"
GOTO finish


:neverrun
ECHO(
ECHO ERROR: The game appears to never have been run. Run the game at least
ECHO        once to create your user profile, then quit it and try installing
ECHO        the mod again.
ECHO(
:abort
ECHO Aborting RealConfig
GOTO end
:finish
ECHO All done!
:end
PAUSE
:exitnow

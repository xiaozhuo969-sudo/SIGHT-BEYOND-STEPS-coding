param([ValidateSet('Hand','Test')] [string]$Target='Hand')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$installRoot='D:\Espressif'
$env:PYTHONUTF8='1'
$env:IDF_TOOLS_PATH="$installRoot\tools"
$env:IDF_PATH="$installRoot\frameworks\esp-idf-v6.0.3"
$basePython='C:\Users\PC\AppData\Local\Programs\Python\Python311'
$gitBin='C:\Users\PC\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\git\cmd'
$armBin="$installRoot\arm\arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-arm-none-eabi\bin"
$env:PATH="$basePython;$gitBin;$armBin;C:\ANZHUANG\mingw64\mingw64\bin;"+$env:PATH
$testPython="$installRoot\test-python\Scripts\python.exe"
function Check-Result { if($LASTEXITCODE -ne 0){ throw "Command failed: exit $LASTEXITCODE" } }
function Ensure-Junction([string]$Link,[string]$Destination) {
    if(Test-Path -LiteralPath $Link) {
        $item=Get-Item -LiteralPath $Link
        if($item.LinkType -ne 'Junction' -or @($item.Target)[0] -ne $Destination) {
            throw "Existing path does not point to this project: $Link"
        }
    } else { New-Item -ItemType Junction -Path $Link -Target $Destination | Out-Null }
}
New-Item -ItemType Directory -Path "$installRoot\work\air","$installRoot\builds" -Force | Out-Null
Ensure-Junction "$installRoot\work\hand" "$projectRoot\手控制器"
Ensure-Junction "$installRoot\work\shared" "$projectRoot\shared"
if($Target -eq 'Hand') {
    . "$env:IDF_PATH\export.ps1"
    & $testPython "$PSScriptRoot\stage_hand.py" --source-root $projectRoot --destination "$installRoot\sources\feedback"
    Check-Result
    $handBuild="$installRoot\builds\hand-only-release"
    New-Item -ItemType Directory -Path $handBuild -Force | Out-Null
    Copy-Item -LiteralPath "$projectRoot\手控制器\sdkconfig" -Destination "$handBuild\sdkconfig" -Force
    & python "$env:IDF_PATH\tools\idf.py" -C "$installRoot\sources\feedback\hand" -B $handBuild -D "SDKCONFIG=$handBuild\sdkconfig" build
    Check-Result
}
if($Target -eq 'Test') {
    & $testPython "$projectRoot\手控制器\tests\run_native.py"
    Check-Result
}

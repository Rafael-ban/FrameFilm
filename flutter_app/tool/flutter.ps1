# Local Windows build entry: SDKs, caches, output and temporary files live on D:.
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$FlutterArguments
)
$ErrorActionPreference = 'Stop'
$storage = if ($env:FRAMEFILM_DEV_ROOT) { $env:FRAMEFILM_DEV_ROOT } else { 'D:\dev-tool' }
$app = Split-Path $PSScriptRoot -Parent
$flutter = Join-Path $storage 'FrameFilm-toolchains\flutter'
$sdk = Join-Path $storage 'Android\Sdk'
$output = Join-Path $storage 'FrameFilm-build\flutter'
$temporary = Join-Path $storage 'FrameFilm-build\temp'
if (!(Test-Path -LiteralPath "$flutter\bin\flutter.bat") -or !(Test-Path -LiteralPath $sdk)) {
    throw 'Flutter / Android SDK not found. See README for the local D-drive layout.'
}
$localBuild = Join-Path $app 'build'
if (Test-Path -LiteralPath $localBuild) {
    $buildItem = Get-Item -LiteralPath $localBuild
    if ($buildItem.LinkType -ne 'Junction' -or [IO.Path]::GetFullPath($buildItem.Target) -ne [IO.Path]::GetFullPath($output)) {
        throw "Migrate existing build directory to $output first; no files were removed."
    }
} else {
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    New-Item -ItemType Junction -Path $localBuild -Target $output | Out-Null
}
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
$values = @{
    ANDROID_HOME = $sdk
    ANDROID_SDK_ROOT = $sdk
    GRADLE_USER_HOME = (Join-Path $storage 'gradle-home')
    PUB_CACHE = (Join-Path $storage 'pub-cache')
    TEMP = $temporary
    TMP = $temporary
    FLUTTER_SUPPRESS_ANALYTICS = 'true'
}
$saved = @{}
foreach ($key in $values.Keys) {
    $saved[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
    [Environment]::SetEnvironmentVariable($key, $values[$key], 'Process')
}
Push-Location $app
try {
    # local.properties is ignored; preserve other generated Flutter properties.
    $properties = Join-Path $app 'android\local.properties'
    $lines = if (Test-Path -LiteralPath $properties) { @(Get-Content -LiteralPath $properties) } else { @() }
    $lines = @($lines | Where-Object { $_ -notmatch '^(sdk.dir|flutter.sdk)=' })
    $lines += 'sdk.dir=' + $sdk.Replace('\', '/')
    $lines += 'flutter.sdk=' + $flutter.Replace('\', '/')
    Set-Content -LiteralPath $properties -Value $lines -Encoding utf8
    & "$flutter\bin\flutter.bat" @FlutterArguments
    $result = $LASTEXITCODE
} finally {
    Pop-Location
    foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key], 'Process') }
}
exit $result

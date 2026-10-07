$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$binary = Join-Path $env:TEMP ("framefilm-ark-ble-name-{0}.exe" -f [guid]::NewGuid())
try {
    clang -std=c11 -D_CRT_NONSTDC_NO_DEPRECATE -Wall -Wextra `
        -I (Join-Path $here 'stubs') `
        -I (Join-Path $here '../../components/film_service/inc') `
        (Join-Path $here 'test_service_ble_name.c') `
        (Join-Path $here '../../components/film_service/src/service_ble_name.c') `
        -o $binary
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $binary
    exit $LASTEXITCODE
} finally {
    Remove-Item -LiteralPath $binary -ErrorAction SilentlyContinue
}

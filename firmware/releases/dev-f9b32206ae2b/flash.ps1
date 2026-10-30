param(
    [Parameter(Mandatory=$true)][string]$Port,
    [string]$IdfPython = "C:\Users\zengzhi\.espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe"
)
$ErrorActionPreference = 'Stop'
if ($Port -notmatch '^COM[0-9]+$') { throw 'Specify an explicit Windows COM port, for example COM7.' }
if (!(Test-Path -LiteralPath $IdfPython -PathType Leaf)) { throw 'ESP-IDF Python not found; supply -IdfPython.' }
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw | ConvertFrom-Json
foreach ($entry in $manifest.images) {
    $path = Join-Path $PSScriptRoot $entry.file
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "Image checksum mismatch: $($entry.file)"
    }
}
$arguments = @('-m','esptool','--chip','esp32s3','--port',$Port,'--baud','460800',
    '--before','default_reset','--after','hard_reset','write_flash',
    '--flash_mode','dio','--flash_size','16MB','--flash_freq','80m')
foreach ($entry in $manifest.images) {
    $arguments += @($entry.offset, (Join-Path $PSScriptRoot $entry.file))
}
& $IdfPython @arguments
if ($LASTEXITCODE -ne 0) { throw "esptool failed: $LASTEXITCODE" }

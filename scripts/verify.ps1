$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $taskRoot 'dist'
$manifest = Get-Content -LiteralPath (Join-Path $dist 'checksums-wt32-v2-cz.json') -Raw | ConvertFrom-Json
foreach ($entry in $manifest) {
    $path = Join-Path $dist $entry.file
    if ((Get-Item -LiteralPath $path).Length -ne $entry.bytes) { throw "Dimensione errata: $($entry.file)" }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) { throw "SHA256 errato: $($entry.file)" }
    if ((Get-FileHash -LiteralPath $path -Algorithm MD5).Hash -ne $entry.md5) { throw "MD5 errato: $($entry.file)" }
}
$factory = [IO.File]::ReadAllBytes((Join-Path $dist 'HeishaMon-v2.0-WT32-ETH01-CZ-test1-factory.bin'))
$ota = [IO.File]::ReadAllBytes((Join-Path $dist 'HeishaMon-v2.0-WT32-ETH01-CZ-test1-ota.bin'))
if ($factory.Length -ne 4MB) { throw 'Factory non lunga 4 MB' }
foreach ($offset in @(0x1000, 0x10000)) {
    if ($factory[$offset] -ne 0xe9) { throw 'Header ESP errato' }
    if ([BitConverter]::ToUInt16($factory, $offset + 12) -ne 0) { throw 'Chip immagine diverso da ESP32 classico' }
    if ($factory[$offset + 2] -ne 2 -or $factory[$offset + 3] -ne 0x20) { throw 'Flash non DIO 40 MHz 4 MB' }
}
if ($ota.Length -gt 0x1e0000) { throw 'Applicazione troppo grande per slot OTA' }
$embeddedApp = [byte[]]::new($ota.Length)
[Array]::Copy($factory, 0x10000, $embeddedApp, 0, $ota.Length)
$sha = [Security.Cryptography.SHA256]::Create()
try {
    if ([Convert]::ToBase64String($sha.ComputeHash($embeddedApp)) -ne [Convert]::ToBase64String($sha.ComputeHash($ota))) { throw 'Applicazione factory diversa da OTA' }
} finally {
    $sha.Dispose()
}
$expectedPartitions = @{
    app0 = @(0x10000,0x1e0000)
    app1 = @(0x1f0000,0x1e0000)
    spiffs = @(0x3d0000,0x20000)
}
$seenPartitions = @{}
for ($offset = 0x8000; $offset -lt 0x9000; $offset += 32) {
    if ([BitConverter]::ToUInt16($factory,$offset) -ne 0x50aa) { break }
    $label = [Text.Encoding]::ASCII.GetString($factory,$offset + 12,16).TrimEnd([char]0)
    if ($expectedPartitions.ContainsKey($label)) {
        if ([BitConverter]::ToUInt32($factory,$offset + 4) -ne $expectedPartitions[$label][0] -or [BitConverter]::ToUInt32($factory,$offset + 8) -ne $expectedPartitions[$label][1]) { throw "Partizione errata: $label" }
        $seenPartitions[$label] = $true
    }
}
if ($seenPartitions.Count -ne $expectedPartitions.Count) { throw 'Partizioni mancanti' }
# The protocol decoder and topic tables remain v2.0, not the v4.2.3 port.
$original = Join-Path $taskRoot 'vendor/HeishaMon-2.0/HeishaMon'
$source = Join-Path $taskRoot 'firmware/HeishaMon'
$sourceManifest = Get-Content -LiteralPath (Join-Path $taskRoot 'docs/source-sha256.json') -Raw | ConvertFrom-Json
foreach ($entry in $sourceManifest) {
    if ((Get-FileHash -LiteralPath (Join-Path $source $entry.file) -Algorithm SHA256).Hash -ne $entry.sha256) { throw "SHA256 sorgente errato: $($entry.file)" }
}
if (Test-Path -LiteralPath $original) {
    foreach ($name in @('decode.cpp','htmlcode.h','smartcontrol.cpp','smartcontrol.h','dallas.cpp','dallas.h')) {
        if ((Get-FileHash -LiteralPath (Join-Path $original $name) -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath (Join-Path $source $name) -Algorithm SHA256).Hash) { throw "Sorgente v2 modificato inaspettatamente: $name" }
    }
    $originalDecoder = [IO.File]::ReadAllText((Join-Path $original 'decode.h')).Replace('#include <ESP8266WiFi.h>', '#include <WiFi.h>')
    if ($originalDecoder -cne [IO.File]::ReadAllText((Join-Path $source 'decode.h'))) { throw 'Tabelle e dichiarazioni decoder diverse da v2.0' }
}
Write-Output "OK: checksum, target ESP32, flash DIO/40 MHz, partizioni OTA, immagini coerenti e decoder v2.0. App: $($ota.Length) byte."

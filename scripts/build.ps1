param(
    [string]$ArduinoCli = 'arduino-cli',
    [string]$ConfigFile = '',
    [string]$OutputDirectory = 'dist',
    [switch]$InstallDependencies
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$isWindowsHost = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT
$drive = $null
if (-not (Get-Command $ArduinoCli -ErrorAction SilentlyContinue)) {
    $bundledCli = Join-Path $taskRoot '.tools/arduino-cli/arduino-cli.exe'
    if (Test-Path -LiteralPath $bundledCli) { $ArduinoCli = $bundledCli }
    else { throw 'Installare Arduino CLI oppure specificare -ArduinoCli con il suo percorso' }
}
try {
    $shortRoot = $taskRoot
    if ($isWindowsHost) {
        $drive = @('R:', 'S:', 'T:', 'U:', 'V:', 'W:') | Where-Object { -not (Test-Path "$_\") } | Select-Object -First 1
        if (-not $drive) { throw 'Nessuna lettera libera per il percorso breve della toolchain' }
        & subst.exe $drive $taskRoot
        if ($LASTEXITCODE -ne 0) { throw 'Creazione percorso breve fallita' }
        $shortRoot = "$drive\"
    }
    if (-not $ConfigFile) {
        $tools = Join-Path $shortRoot '.tools'
        New-Item -ItemType Directory -Path $tools -Force | Out-Null
        $ConfigFile = Join-Path $tools 'arduino-publish.yaml'
        $config = @{
            board_manager = @{additional_urls = @('https://espressif.github.io/arduino-esp32/package_esp32_index.json')}
            directories = @{
                data = Join-Path $tools 'arduino-data'
                downloads = Join-Path $tools 'arduino-downloads'
                user = Join-Path $tools 'arduino-v2-user'
            }
        }
        [IO.File]::WriteAllText($ConfigFile,($config | ConvertTo-Json -Depth 6))
    }
    $cliArgs = @('--config-file',$ConfigFile)
    if ($InstallDependencies) {
        & $ArduinoCli @cliArgs core update-index
        if ($LASTEXITCODE -ne 0) { throw 'Aggiornamento indice core fallito' }
        & $ArduinoCli @cliArgs core install 'esp32:esp32@3.3.11'
        if ($LASTEXITCODE -ne 0) { throw 'Installazione core ESP32 fallita' }
        & $ArduinoCli @cliArgs lib install 'ArduinoJson@6.21.5' 'WebSockets@2.7.1' 'PubSubClient@2.8' 'DallasTemperature@4.0.6' 'OneWire@2.3.8'
        if ($LASTEXITCODE -ne 0) { throw 'Installazione librerie fallita' }
    }
    $board = 'esp32:esp32:esp32:FlashMode=dio,FlashFreq=40,FlashSize=4M,PartitionScheme=min_spiffs,PSRAM=disabled,UploadSpeed=115200'
    $sketch = Join-Path $shortRoot 'firmware/HeishaMon'
    $output = Join-Path $shortRoot 'build/wt32-cz'
    & $ArduinoCli @cliArgs compile --fqbn $board --build-property 'compiler.cpp.extra_flags=-funsigned-char' --build-path (Join-Path $shortRoot 'build/compile-cache') --output-dir $output --jobs 4 --warnings default $sketch
    if ($LASTEXITCODE -ne 0) { throw 'Compilazione WT32-ETH01 CZ fallita' }
    $properties = & $ArduinoCli @cliArgs compile --fqbn $board --show-properties=expanded $sketch
    if ($LASTEXITCODE -ne 0) { throw 'Lettura percorsi toolchain fallita' }
    $propertyMap = @{}
    foreach ($line in $properties) {
        $parts = $line -split '=',2
        if ($parts.Count -eq 2) { $propertyMap[$parts[0]] = $parts[1] }
    }
    $esptoolName = if ($isWindowsHost) { 'esptool.exe' } else { 'esptool' }
    $esptool = Join-Path $propertyMap['runtime.tools.esptool_py.path'] $esptoolName
    $bootApp = Join-Path $propertyMap['runtime.platform.path'] 'tools/partitions/boot_app0.bin'
    $dist = Join-Path $shortRoot $OutputDirectory
    New-Item -ItemType Directory -Path $dist -Force | Out-Null
    $prefix = 'HeishaMon-v2.0-WT32-ETH01-CZ-test1'
    & $esptool --chip esp32 merge-bin --output (Join-Path $dist "$prefix-factory.bin") --flash-mode dio --flash-freq 40m --flash-size 4MB --pad-to-size 4MB 0x1000 (Join-Path $output 'HeishaMon.ino.bootloader.bin') 0x8000 (Join-Path $output 'HeishaMon.ino.partitions.bin') 0xe000 $bootApp 0x10000 (Join-Path $output 'HeishaMon.ino.bin')
    if ($LASTEXITCODE -ne 0) { throw 'Creazione factory fallita' }
    Copy-Item -LiteralPath (Join-Path $output 'HeishaMon.ino.bin') -Destination (Join-Path $dist "$prefix-ota.bin")
    $manifest = foreach ($suffix in @('factory','ota')) {
        $image = Join-Path $dist "$prefix-$suffix.bin"
        [ordered]@{
            file = Split-Path $image -Leaf
            bytes = (Get-Item -LiteralPath $image).Length
            sha256 = (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.ToLowerInvariant()
            md5 = (Get-FileHash -LiteralPath $image -Algorithm MD5).Hash.ToLowerInvariant()
        }
    }
    $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dist 'checksums-wt32-v2-cz.json')
} finally {
    if ($drive) { & subst.exe $drive /D }
}

param(
    [Parameter(Mandatory=$true)]
    [string]$Address,
    [Parameter(Mandatory=$true)]
    [ValidatePattern('^(?:[0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}$')]
    [string]$ExpectedMac
)
$ErrorActionPreference = 'Stop'
$ExpectedMac = $ExpectedMac.Replace(':','-').ToUpperInvariant()
$image = Join-Path (Split-Path $PSScriptRoot -Parent) 'dist/HeishaMon-v2.0-WT32-ETH01-CZ-test1-ota.bin'
$manifestPath = Join-Path (Split-Path $PSScriptRoot -Parent) 'dist/checksums-wt32-v2-cz.json'
$expected = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json | Where-Object file -eq (Split-Path $image -Leaf)
if (-not $expected -or (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash -ne $expected.sha256) { throw 'Checksum OTA non valido' }
$bytes = [IO.File]::ReadAllBytes($image)
if ($bytes.Length -gt 0x1c0000 -or $bytes[0] -ne 0xe9 -or [BitConverter]::ToUInt16($bytes,12) -ne 0) { throw 'Immagine non valida per ESP32 OTA' }
$ping = [Net.NetworkInformation.Ping]::new()
try { [void]$ping.Send($Address,1500) } finally { $ping.Dispose() }
$neighbor = Get-NetNeighbor -AddressFamily IPv4 -IPAddress $Address | Where-Object { $_.LinkLayerAddress -eq $ExpectedMac } | Select-Object -First 1
if (-not $neighbor) { throw "MAC della scheda $Address diverso da ${ExpectedMac}: caricamento annullato" }
$client = [Net.Sockets.TcpClient]::new()
try {
    $connecting = $client.ConnectAsync($Address,3232)
    if (-not $connecting.Wait(4000)) { throw 'Timeout connessione OTA ESPHome' }
    $client.NoDelay = $true
    $client.SendBufferSize = 65536
    $stream = $client.GetStream()
    $stream.ReadTimeout = 30000
    $stream.WriteTimeout = 30000
    function Expect-Byte([int]$Value,[string]$Stage) {
        $received = $stream.ReadByte()
        if ($received -ne $Value) { throw ('OTA {0}: risposta 0x{1:X2}, attesa 0x{2:X2}' -f $Stage,$received,$Value) }
    }
    $magic = [byte[]]@(0x6c,0x26,0xf7,0x5c,0x45)
    $stream.Write($magic,0,$magic.Length)
    Expect-Byte 0 'handshake'
    $version = $stream.ReadByte()
    if ($version -ne 2) { throw "Versione OTA inattesa: $version" }
    # No compression; advertise SHA256 authentication support, as the official client does.
    $stream.WriteByte(2)
    Expect-Byte 0x40 'features'
    $auth = $stream.ReadByte()
    if ($auth -ne 0x41) { throw 'OTA richiede autenticazione: nessuna scrittura eseguita' }
    Write-Output "Target verificato: $Address / $ExpectedMac. Upload ESPHome OTA v2: $($bytes.Length) byte."
    $size = [byte[]]@(
        (($bytes.Length -shr 24) -band 255),
        (($bytes.Length -shr 16) -band 255),
        (($bytes.Length -shr 8) -band 255),
        ($bytes.Length -band 255)
    )
    $stream.Write($size,0,4)
    Expect-Byte 0x42 'preparazione slot'
    $md5 = [Text.Encoding]::ASCII.GetBytes($expected.md5)
    $stream.Write($md5,0,$md5.Length)
    Expect-Byte 0x43 'checksum'
    $client.NoDelay = $false
    $offset = 0
    $lastPercent = -1
    while ($offset -lt $bytes.Length) {
        $count = [Math]::Min(8192,$bytes.Length-$offset)
        $stream.Write($bytes,$offset,$count)
        Expect-Byte 0x47 'blocco'
        $offset += $count
        $percent = [int][Math]::Floor(100.0*$offset/$bytes.Length)
        if ($percent -ge $lastPercent + 10 -or $offset -eq $bytes.Length) {
            Write-Output "Upload: $percent%"
            $lastPercent = $percent
        }
    }
    $client.NoDelay = $true
    Expect-Byte 0x44 'ricezione completata'
    Expect-Byte 0x45 'validazione immagine'
    $stream.WriteByte(0)
    Write-Output 'OTA completato e immagine validata dalla scheda. Riavvio in HeishaMon.'
} finally {
    $client.Dispose()
}

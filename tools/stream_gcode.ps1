# Useage:PS> .\tools\stream_gcode.ps1 -Port COM3 -File ".\test\test_m905.gcode" > m905_run_log.txt

param(
  [Parameter(Mandatory=$true)] [string] $Port,
  [int] $Baud = 115200,
  [Parameter(Mandatory=$true)] [string] $File
)

if (-not (Test-Path $File)) { Write-Error \"File not found: $File\"; exit 1 }

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 500
$sp.WriteTimeout = 500
try {
  $sp.Open()
  Write-Host \"Connected to $Port @ $Baud\"
  # Optional: flush any startup banner
  Start-Sleep -Milliseconds 500
  while ($sp.BytesToRead) { Write-Host -NoNewline ($sp.ReadExisting()) }

  Get-Content $File | ForEach-Object {
    $line = $_.Trim()
    if ($line -eq '') { return }
    Write-Host \">> $line\"
    $sp.WriteLine($line)
    # Wait for short ack / let the firmware respond
    Start-Sleep -Milliseconds 200

    # Print any incoming data available (non-blocking)
    Start-Sleep -Milliseconds 100
    while ($sp.BytesToRead -gt 0) {
      $r = $sp.ReadExisting()
      Write-Host $r -NoNewline
      Start-Sleep -Milliseconds 50
    }
  }

  Write-Host \"File stream complete.\"
} catch {
  Write-Error \"Serial error: $_\"
} finally {
  if ($sp -and $sp.IsOpen) { $sp.Close(); $sp.Dispose() }
}
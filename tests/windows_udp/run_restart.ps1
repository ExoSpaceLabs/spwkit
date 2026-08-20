param(
    [Parameter(Mandatory = $true)]
    [string]$Peer,
    [Parameter(Mandatory = $true)]
    [int]$BasePort
)

$ErrorActionPreference = "Stop"

$root = Join-Path $env:RUNNER_TEMP "spwkit-winsock-$PID"
New-Item -ItemType Directory -Force -Path $root | Out-Null
$aOut = Join-Path $root "a.out.log"
$aErr = Join-Path $root "a.err.log"
$b1Out = Join-Path $root "b1.out.log"
$b1Err = Join-Path $root "b1.err.log"
$b2Out = Join-Path $root "b2.out.log"
$b2Err = Join-Path $root "b2.err.log"

function Assert-Exit([System.Diagnostics.Process]$Process, [string]$Name) {
    $Process.WaitForExit()
    if ($Process.ExitCode -ne 0) {
        Write-Host "=== $Name stdout ==="
        Get-Content -ErrorAction SilentlyContinue ($Name -eq "A" ? $aOut : ($Name -eq "B1" ? $b1Out : $b2Out))
        Write-Host "=== $Name stderr ==="
        Get-Content -ErrorAction SilentlyContinue ($Name -eq "A" ? $aErr : ($Name -eq "B1" ? $b1Err : $b2Err))
        throw "$Name exited with code $($Process.ExitCode)"
    }
}

try {
    $a = Start-Process -FilePath $Peer `
        -ArgumentList @("A", "$BasePort") `
        -PassThru -NoNewWindow `
        -RedirectStandardOutput $aOut -RedirectStandardError $aErr

    Start-Sleep -Milliseconds 100

    $b1 = Start-Process -FilePath $Peer `
        -ArgumentList @("B", "$BasePort", "1") `
        -PassThru -NoNewWindow `
        -RedirectStandardOutput $b1Out -RedirectStandardError $b1Err
    Assert-Exit $b1 "B1"

    # The surviving peer must cross its configured liveness horizon before the
    # replacement process appears, otherwise restart recovery is not exercised.
    Start-Sleep -Milliseconds 500

    $b2 = Start-Process -FilePath $Peer `
        -ArgumentList @("B", "$BasePort", "2") `
        -PassThru -NoNewWindow `
        -RedirectStandardOutput $b2Out -RedirectStandardError $b2Err
    Assert-Exit $b2 "B2"
    Assert-Exit $a "A"

    $aLog = Get-Content -Raw $aOut
    if ($aLog -notmatch "A observed peer loss" -or
        $aLog -notmatch "A restart recovery complete") {
        throw "survivor log did not prove loss and fresh-session recovery"
    }

    Write-Host "=== survivor ==="
    Write-Host $aLog
    Write-Host "=== first peer ==="
    Get-Content $b1Out
    Write-Host "=== restarted peer ==="
    Get-Content $b2Out
}
finally {
    Get-Process spwkit_windows_udp_peer -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
}

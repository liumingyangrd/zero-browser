# gui_probe.ps1 -- all-in-one GUI verification script.
#
# Why "all-in-one": in this environment a child process started in one pwsh call cannot
# be queried from another (due to session/permission isolation), so starting, waiting,
# screenshotting, clicking, reading the log and terminating must all happen inside the
# same process.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\gui_probe.ps1 `
#       -Url "http://127.0.0.1:8765/video.html" -WaitSeconds 6 `
#       -Out "build\gui-shot.png" [-ClickX 500 -ClickY 625] [-Out2 "build\gui-shot-click.png"]

param(
  [string]$Url = "http://127.0.0.1:8765/video.html",
  [int]$WaitSeconds = 6,
  [string]$Out = "build\gui-shot.png",
  [int]$ClickX = -1,
  [int]$ClickY = -1,
  [string]$Out2 = "",
  [string]$Exe = "build\zero-browser-debug.exe",
  [string]$LogErr = "build\gui_stderr.txt",
  [string]$LogOut = "build\gui_stdout.txt"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

Add-Type -AssemblyName System.Drawing
$src = @"
using System;
using System.Runtime.InteropServices;
public class ZbCap {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
"@
if (-not ("ZbCap" -as [type])) { Add-Type -TypeDefinition $src }

function Save-WindowShot([IntPtr]$hwnd, [string]$path) {
  $r = New-Object ZbCap+RECT
  [ZbCap]::GetWindowRect($hwnd, [ref]$r) | Out-Null
  $w = $r.Right - $r.Left
  $h = $r.Bottom - $r.Top
  if ($w -le 0 -or $h -le 0) { throw "bad window rect ${w}x${h}" }
  $bmp = New-Object System.Drawing.Bitmap $w, $h
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [ZbCap]::PrintWindow($hwnd, $hdc, 2) | Out-Null
  $g.ReleaseHdc($hdc)
  $g.Dispose()
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  Write-Output "captured $path ${w}x${h}"
}

$env:ZB_KEEP_CONSOLE = "1"
$env:ZB_MEDIA_DEBUG = "1"

foreach ($f in @($LogErr, $LogOut)) {
  if (Test-Path $f) { Remove-Item $f -Force }
}

$exePath = (Resolve-Path $Exe).Path
$p = Start-Process -FilePath $exePath -ArgumentList $Url `
     -RedirectStandardError (Join-Path $root $LogErr) `
     -RedirectStandardOutput (Join-Path $root $LogOut) -PassThru

Write-Output "pid=$($p.Id) exe=$exePath url=$Url"

# Wait for the main window to appear and finish navigation / media initialization.
$deadline = (Get-Date).AddSeconds($WaitSeconds + 15)
$hwnd = [IntPtr]::Zero
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Milliseconds 500
  if ($p.HasExited) { throw "process exited early, code=$($p.ExitCode)" }
  $p.Refresh()
  if ($p.MainWindowHandle -ne [IntPtr]::Zero) {
    if ($hwnd -eq [IntPtr]::Zero) { Start-Sleep -Seconds $WaitSeconds }
    $hwnd = $p.MainWindowHandle
    break
  }
}
if ($hwnd -eq [IntPtr]::Zero) { throw "no main window handle" }

Save-WindowShot $hwnd $Out

if ($ClickX -ge 0 -and $ClickY -ge 0) {
  # WM_LBUTTONDOWN=0x0201, WM_LBUTTONUP=0x0202; lParam = y<<16 | x
  $lp = [IntPtr](($ClickY -shl 16) -bor ($ClickX -band 0xFFFF))
  [ZbCap]::PostMessage($hwnd, 0x0201, [IntPtr]1, $lp) | Out-Null
  Start-Sleep -Milliseconds 120
  [ZbCap]::PostMessage($hwnd, 0x0202, [IntPtr]0, $lp) | Out-Null
  Write-Output "clicked at ($ClickX,$ClickY)"
  Start-Sleep -Seconds 2
  if ($Out2 -ne "") { Save-WindowShot $hwnd $Out2 }
}

Write-Output "--- stderr ---"
if (Test-Path $LogErr) { Get-Content $LogErr -Encoding UTF8 }
Write-Output "--- stdout ---"
if (Test-Path $LogOut) { Get-Content $LogOut -Encoding UTF8 }

if (-not $p.HasExited) { $p.Kill(); $p.WaitForExit(5000) | Out-Null }
Write-Output "killed pid=$($p.Id)"

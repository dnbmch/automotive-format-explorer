# Launch the packaged app and pass only once its main window has rendered.
#
# src/main.cpp cloaks the window QML creates and uncloaks it on the first
# swapped frame. A visible, uncloaked Qt window of the launched process titled
# "Automotive Format Explorer" therefore proves that the deployed Qt loaded its
# platform plugin, the QML engine built the window and the scene graph drew it.
#
# Anything else fails: the process exiting first (a missing DLL fails the loader
# before main(); a root object that will not instantiate exits -1), any other
# visible window of the process (Qt's fatal-error box when no platform plugin
# loads), or the timeout. Only the launched process is stopped, on every path.
#
# The app runs on the deployed Windows platform plugin with PATH reduced to the
# system directories and no Qt variables, so a DLL or plugin missing from the
# package cannot be found elsewhere, and with critical-error boxes suppressed,
# so a loader failure exits instead of waiting on the desktop.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke_windows.ps1 [dist-dir] [timeout-seconds]

param(
    [string]$Dist = 'dist',
    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')

$app = Join-Path $Dist 'automotive-format-explorer.exe'
if (-not (Test-Path -LiteralPath $app -PathType Leaf)) {
    Write-Host "smoke: not found: $app"
    exit 1
}
$app = (Resolve-Path -LiteralPath $app).Path
$title = 'Automotive Format Explorer'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class SmokeWindows {
    public sealed class Window {
        public string ClassName;
        public string Title;
        public bool Cloaked;
        public string Text;
    }

    delegate bool EnumProc(IntPtr hwnd, IntPtr parameter);

    [DllImport("user32.dll")]
    static extern bool EnumWindows(EnumProc proc, IntPtr parameter);
    [DllImport("user32.dll")]
    static extern bool EnumChildWindows(IntPtr parent, EnumProc proc, IntPtr parameter);
    [DllImport("user32.dll")]
    static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")]
    static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetClassNameW(IntPtr hwnd, StringBuilder name, int capacity);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetWindowTextW(IntPtr hwnd, StringBuilder text, int capacity);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr SendMessageTimeoutW(IntPtr hwnd, uint message, IntPtr wParam,
                                             StringBuilder lParam, uint flags, uint timeout,
                                             out IntPtr result);
    [DllImport("dwmapi.dll")]
    static extern int DwmGetWindowAttribute(IntPtr hwnd, int attribute, out int value, int size);
    [DllImport("kernel32.dll")]
    public static extern uint SetErrorMode(uint mode);

    const uint WM_GETTEXT = 0x000D;
    const uint SMTO_ABORTIFHUNG = 0x0002;
    const int DWMWA_CLOAKED = 14;

    static string ClassOf(IntPtr hwnd) {
        StringBuilder name = new StringBuilder(256);
        GetClassNameW(hwnd, name, name.Capacity);
        return name.ToString();
    }

    // A control's text in another process needs the message; a top-level
    // window's title does not.
    static string ControlText(IntPtr hwnd) {
        StringBuilder text = new StringBuilder(2048);
        IntPtr result;
        SendMessageTimeoutW(hwnd, WM_GETTEXT, new IntPtr(text.Capacity), text, SMTO_ABORTIFHUNG,
                            1000, out result);
        return text.ToString();
    }

    // The visible top-level windows of one process; a dialog's static texts
    // become its Text.
    public static List<Window> Visible(int processId) {
        List<Window> windows = new List<Window>();
        EnumWindows(delegate(IntPtr hwnd, IntPtr parameter) {
            uint owner;
            GetWindowThreadProcessId(hwnd, out owner);
            if (owner != (uint)processId || !IsWindowVisible(hwnd)) {
                return true;
            }
            Window window = new Window();
            window.ClassName = ClassOf(hwnd);
            StringBuilder title = new StringBuilder(512);
            GetWindowTextW(hwnd, title, title.Capacity);
            window.Title = title.ToString();
            int cloaked;
            window.Cloaked = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, out cloaked, 4) == 0 &&
                             cloaked != 0;
            List<string> texts = new List<string>();
            if (window.ClassName == "#32770") {
                EnumChildWindows(hwnd, delegate(IntPtr child, IntPtr unused) {
                    if (ClassOf(child) == "Static") {
                        string text = ControlText(child).Trim();
                        if (text.Length > 0) {
                            texts.Add(text);
                        }
                    }
                    return true;
                }, IntPtr.Zero);
            }
            window.Text = string.Join(" ", texts.ToArray());
            windows.Add(window);
            return true;
        }, IntPtr.Zero);
        return windows;
    }
}
'@

# SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, inherited by the app.
[void][SmokeWindows]::SetErrorMode(0x0001 -bor 0x8000)

$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = $app
$start.WorkingDirectory = Split-Path -Parent $app
$start.UseShellExecute = $false
foreach ($name in @($start.EnvironmentVariables.Keys)) {
    if ($name -match '^(QT_|QML)') {
        $start.EnvironmentVariables.Remove($name)
    }
}
$start.EnvironmentVariables['PATH'] =
    "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem"

$process = [System.Diagnostics.Process]::Start($start)
$clock = [System.Diagnostics.Stopwatch]::StartNew()
$verdict = $null
$passed = $false
try {
    while ($null -eq $verdict) {
        if ($process.HasExited) {
            $verdict = 'exited before its window, code 0x{0:X8}' -f $process.ExitCode
            break
        }
        foreach ($window in [SmokeWindows]::Visible($process.Id)) {
            $main = $window.ClassName -match '^Qt\d*QWindow' -and $window.Title -eq $title
            if (-not $main) {
                $verdict = 'unexpected window {0} "{1}" {2}' -f $window.ClassName, $window.Title, $window.Text
                break
            }
            if (-not $window.Cloaked) {
                $verdict = 'main window rendered after {0:N1} s' -f $clock.Elapsed.TotalSeconds
                $passed = $true
                break
            }
        }
        if ($null -eq $verdict -and $clock.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
            $verdict = "no rendered main window within $TimeoutSeconds s"
        }
        if ($null -eq $verdict) {
            Start-Sleep -Milliseconds 100
        }
    }
} finally {
    if (-not $process.HasExited) {
        $process.Kill()
        [void]$process.WaitForExit(10000)
    }
}

Write-Host "smoke: pid $($process.Id): $verdict"
if ($passed) {
    exit 0
}
exit 1

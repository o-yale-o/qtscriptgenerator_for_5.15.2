# Batch-run all examples under qs_eval with a timeout; capture stderr for script errors.
# Uses .NET Process + cmd wrapper to dodge the PS5.1 Start-Process NO_PROXY/no_proxy bug.
$ErrorActionPreference = 'Continue'
# Unattended mode: no interactive debugger; event-loop JS exceptions exit(2).
[Environment]::SetEnvironmentVariable('QSEVAL_NO_DEBUGGER','1','Process')
[Environment]::SetEnvironmentVariable('QSEVAL_FAILFAST','1','Process')
$qseval = 'D:\OpenSource\qtscriptgenerator-master\qtbindings\qs_eval\release\qs_eval.exe'
$dir = 'D:\OpenSource\qtscriptgenerator-master\examples'
$scripts = @(
  'TwoWayButton.qs','AnalogClock.js','Wiggly.js','DigitalClock.js','CollidingMice.js',
  'ConcentricCircles.js','_cc_auto.js','AnimatedBox.qs','BasicDrawing.js',
  'CalendarWidget.js','ImageComposition.js','LineEdits.js','PainterPaths.js',
  'RSSListing.js','Screenshot.js','StandardDialogs.js','StreamBookmarks.js',
  'Transformations.js'
)
$report = @()
$i = 0
foreach ($s in $scripts) {
  $i++
  $out = Join-Path $env:TEMP "_cc_out_$i.txt"
  $err = Join-Path $env:TEMP "_cc_err_$i.txt"
  Remove-Item $out,$err -ErrorAction SilentlyContinue
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = 'cmd.exe'
  $psi.Arguments = '/c ""' + $qseval + '" "' + $s + '" > "' + $out + '" 2> "' + $err + '""'
  $psi.WorkingDirectory = $dir
  $psi.UseShellExecute = $false
  $psi.CreateNoWindow = $true
  $p = [System.Diagnostics.Process]::Start($psi)
  if ($null -eq $p) { $report += [pscustomobject]@{Script=$s; Status='LAUNCH-FAILED'; ExitCode=$null; StdErr='p=null'; StdOut=''}; continue }
  $exited = $p.WaitForExit(6000)
  if ($exited) {
    $code = $p.ExitCode
    $status = if ($code -eq 0) { 'EXITED-0' } else { "EXITED-$code" }
  } else {
    cmd /c "taskkill /PID $($p.Id) /T /F" 2>&1 | Out-Null
    $p.WaitForExit(3000) | Out-Null
    $status = 'KILLED@6s (window stayed up)'
    $code = $null
  }
  $errText = (Get-Content $err -Raw -ErrorAction SilentlyContinue)
  $outText = (Get-Content $out -Raw -ErrorAction SilentlyContinue)
  $report += [pscustomobject]@{
    Script = $s; Status = $status; ExitCode = $code
    StdErr = if ($errText) { $errText.Trim() } else { '' }
    StdOut = if ($outText) { $outText.Trim() } else { '' }
  }
}
$report | ConvertTo-Json -Depth 3

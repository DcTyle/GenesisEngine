param(
  [Parameter(Mandatory=$true)][string]$WorkingDirectory,
  [Parameter(Mandatory=$true)][string]$LogPath,
  [Parameter(Mandatory=$true)][string[]]$CommandArgs
)

$ErrorActionPreference = 'Stop'

if ($CommandArgs.Count -eq 0) {
  throw 'CommandArgs must contain at least one executable name.'
}

# cmd.exe can forward array-like arguments as a single literal string:
#   'cmake','--preset','win64-vs2022-clang-vulkan'
# Normalize this into a true string[] so flag-style args remain deterministic.
if ($CommandArgs.Count -eq 1) {
  $single = $CommandArgs[0]
  if (-not [string]::IsNullOrWhiteSpace($single)) {
    $matches = [System.Text.RegularExpressions.Regex]::Matches($single, "'([^']*)'")
    if ($matches.Count -gt 0) {
      $parsed = @()
      foreach ($m in $matches) {
        $parsed += $m.Groups[1].Value
      }
      if ($parsed.Count -gt 0) {
        $CommandArgs = $parsed
      }
    }
  }
}

$logDir = Split-Path -Parent $LogPath
if (-not [string]::IsNullOrWhiteSpace($logDir)) {
  New-Item -ItemType Directory -Force -Path $logDir | Out-Null
}

$timestamp = [DateTimeOffset]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
$header = @(
  ('GENESIS_LOG_STEP utc=' + $timestamp),
  ('WORKING_DIRECTORY ' + $WorkingDirectory),
  ('COMMAND ' + ($CommandArgs -join ' ')),
  ''
)
Set-Content -Path $LogPath -Value $header -Encoding UTF8

Push-Location $WorkingDirectory
try {
  $exe = $CommandArgs[0]
  $exeArgs = @()
  if ($CommandArgs.Count -gt 1) {
    $exeArgs = $CommandArgs[1..($CommandArgs.Count - 1)]
  }

  & $exe @exeArgs 2>&1 | Tee-Object -FilePath $LogPath -Append
  $exitCode = if ($LASTEXITCODE -ne $null) { [int]$LASTEXITCODE } elseif ($?) { 0 } else { 1 }
}
finally {
  Pop-Location
}

exit $exitCode

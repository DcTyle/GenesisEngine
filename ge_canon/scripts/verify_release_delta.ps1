param(
  [Parameter(Mandatory=$true)][string]$OldZip,
  [Parameter(Mandatory=$true)][string]$NewZip,
  [string]$AllowRemoveList = "scripts/allowed_removed_paths.txt"
)

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..")

$py = Join-Path $repoRoot "scripts/verify_release_delta.py"
$allow = Join-Path $repoRoot $AllowRemoveList

python $py --old_zip $OldZip --new_zip $NewZip --allow_remove $allow

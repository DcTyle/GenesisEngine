param(
  [Parameter(Mandatory=$true)][string]$BaseZip,
  [Parameter(Mandatory=$true)][string]$PatchDir,
  [Parameter(Mandatory=$true)][string]$OutZip,
  [string]$AllowRemoveList = "scripts/allowed_removed_paths.txt"
)

$ErrorActionPreference = 'Stop'

function Assert-Path([string]$p) {
  if (-not (Test-Path $p)) { throw "Missing path: $p" }
}

Assert-Path $BaseZip
Assert-Path $PatchDir

$work = Join-Path (Split-Path -Parent $OutZip) ("_release_work_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $work | Out-Null

try {
  Write-Host "[1/6] Expanding base zip: $BaseZip"
  Expand-Archive -Path $BaseZip -DestinationPath $work

  Write-Host "[2/6] Applying patch directory (surgical overwrite): $PatchDir"
  # PatchDir should mirror repo root. Copy into expanded base.
  Copy-Item -Path (Join-Path $PatchDir '*') -Destination $work -Recurse -Force

  Write-Host "[3/6] Generating release manifest (deterministic)"
  $manifest = Join-Path $work "Draft Container/GenesisEngine/release_manifest.json"
  python (Join-Path $work "Draft Container/GenesisEngine/scripts/make_release_manifest.py") --root $work --out $manifest | Out-Null

  Write-Host "[4/6] Verifying delta vs base (no unexpected removals)"
  $verify = Join-Path $work "Draft Container/GenesisEngine/scripts/verify_release_delta.py"
  python $verify --old_zip $BaseZip --new_root $work --allow_remove (Join-Path $work "Draft Container/GenesisEngine/$AllowRemoveList")

  Write-Host "[5/6] Packing output zip: $OutZip"
  if (Test-Path $OutZip) { Remove-Item $OutZip -Force }
  Compress-Archive -Path (Join-Path $work '*') -DestinationPath $OutZip

  Write-Host "[6/6] Done. Output: $OutZip"
}
finally {
  Remove-Item -Recurse -Force $work
}

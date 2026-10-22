# Windows equivalent of build.sh. Uses installed MinGW-w64 and Python only.
# Run: powershell -File native/build.ps1
# Check build without release files (code checks, cloud agents):
#   powershell -File native/build.ps1 -Dev   -> build\dev\CursorPad.exe, public\ untouched
param([string]$OutputDirectory = (Join-Path $PSScriptRoot '..\public'), [switch]$Dev)
$ErrorActionPreference = 'Stop'
if ($Dev) { $OutputDirectory = Join-Path $PSScriptRoot '..\build\dev' }
$compiler = (Get-Command gcc -ErrorAction Stop).Source
$resourceCompiler = (Get-Command windres -ErrorAction Stop).Source
$python = (Get-Command python -ErrorAction Stop).Source
$release = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'version.txt')
$header = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'version.h') -Raw
if ($header -notmatch ('#define APP_VERSION ' + [regex]::Escape($release[0]) + '\b') -or
    $header -notmatch ('#define APP_VERSION_A "' + [regex]::Escape($release[1]) + '"')) {
  throw 'version.h and version.txt disagree'
}
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$releaseDir = (Resolve-Path -LiteralPath $OutputDirectory).Path
$resourceObject = Join-Path $releaseDir 'cursorpad-build.res'
$flags = @('-O2', '-Wall', '-Werror', '-Wno-unknown-pragmas', '-finput-charset=UTF-8')
Push-Location $PSScriptRoot
try {
  # A dev build only warns: an edited native/agent needs a rebuilt pack before a release, not before a code check.
  & $python build_agent_pack.py
  if ($LASTEXITCODE -ne 0) { if ($Dev) { Write-Warning 'Agent pack does not match native/agent: rebuild it before a release' } else { throw 'Agent pack verification failed' } }
  & $python build_addons_pack.py
  if ($LASTEXITCODE -ne 0) { if ($Dev) { Write-Warning 'Tools pack does not match: rebuild it before a release' } else { throw 'Tools pack verification failed' } }
  & $compiler @flags -o CursorPadOcr.exe ocr_module.c -lole32 -lruntimeobject -luser32 -municode -s -static '-Wl,--no-insert-timestamp'
  if ($LASTEXITCODE -ne 0) { throw 'OCR helper compilation failed' }
  & $resourceCompiler cursorpad.rc -O coff -o $resourceObject
  if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed' }
  & $compiler @flags -o (Join-Path $releaseDir 'CursorPad.exe') cursorpad.c $resourceObject `
    -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 -luuid -lcomctl32 -lwinhttp -ladvapi32 -lodbc32 -lcrypt32 -lcomdlg32 -lmsimg32 `
    '-Wl,--subsystem,windows' '-Wl,--no-insert-timestamp' -municode -s -static
  if ($LASTEXITCODE -ne 0) { throw 'CursorPad compilation failed' }
  if ($Dev) {
    Write-Output ('Dev build (not a release): ' + (Join-Path $releaseDir 'CursorPad.exe'))
    return
  }
  & $python package_release.py $releaseDir $PSScriptRoot
  if ($LASTEXITCODE -ne 0) { throw 'Release packaging failed' }
} finally {
  Pop-Location
  if (Test-Path -LiteralPath $resourceObject) { Remove-Item -LiteralPath $resourceObject }
}

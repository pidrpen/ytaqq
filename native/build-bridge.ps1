# Сборка моста cp-soyuz.exe (CursorPad → клиент «Союза») из native/cp-soyuz.cs.
# Нужны: клиент «Союза» на этом ПК (его PLMClientHelperService.exe — ссылка при компиляции)
# и csc .NET Framework (C# 5; у .NET Framework 4.x он всегда есть). Запуск из любого места:
#   powershell -ExecutionPolicy Bypass -File native/build-bridge.ps1 [-PlmDir <папка клиента>] [-Csc <csc.exe>] [-Out <exe>]
# Папка клиента по умолчанию — из реестра (HKLM\SOFTWARE\ProgramSoyuz\PLM\1.0, Path), иначе
# «C:\Program Files (x86)\Програмсоюз\BIS v3». Никаких заводских путей и логинов здесь нет.
# После сборки хеш мостa меняется (legacy csc не детерминирован): запишите его осознанно —
#   python native/check_bridge.py --write
# — иначе build.sh откажет: «cp-soyuz.exe не соответствует записанному хешу».
param(
  [string]$PlmDir = "",
  [string]$Csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe",
  [string]$Out = ""
)
$ErrorActionPreference = "Stop"
if (-not $PlmDir) {
  try { $PlmDir = (Get-ItemProperty -Path "HKLM:\SOFTWARE\ProgramSoyuz\PLM\1.0" -ErrorAction Stop).Path } catch { $PlmDir = "" }
  if (-not $PlmDir) { $PlmDir = "C:\Program Files (x86)\Програмсоюз\BIS v3" }
}
$PlmDir = $PlmDir.TrimEnd('\')
if (-not $Out) { $Out = Join-Path $PSScriptRoot "cp-soyuz.exe" }
$helper = Join-Path $PlmDir "PLMClientHelperService.exe"
if (-not (Test-Path -LiteralPath $helper)) { throw "нет $helper — укажите папку клиента «Союза»: -PlmDir" }
if (-not (Test-Path -LiteralPath $Csc)) { $Csc = "C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe" }
if (-not (Test-Path -LiteralPath $Csc)) { throw "нет csc.exe .NET Framework — укажите: -Csc" }
& $Csc /nologo /codepage:65001 /target:exe /platform:anycpu /optimize+ /out:"$Out" `
  /reference:"$helper" /reference:System.Runtime.Remoting.dll `
  (Join-Path $PSScriptRoot "cp-soyuz.cs")
if ($LASTEXITCODE -ne 0) { throw "csc завершился с кодом $LASTEXITCODE" }
$sha = (Get-FileHash -LiteralPath $Out -Algorithm SHA256).Hash.ToLower()
"собрано: $Out ($((Get-Item -LiteralPath $Out).Length) байт), sha256 $sha"
"записать хеш в native/cp-soyuz.sha256: python native/check_bridge.py --write"

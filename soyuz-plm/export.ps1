# Выгрузка из Союз ПЛМ по запросам из папки queries.
# Запуск: powershell -ExecutionPolicy Bypass -File export.ps1 [-Config путь\config.ini]
param([string]$Config = (Join-Path $PSScriptRoot 'config.ini'))

$ErrorActionPreference = 'Stop'

function Read-Ini([string]$path) {
    $ini = @{}; $section = ''
    foreach ($line in Get-Content -LiteralPath $path -Encoding UTF8) {
        $t = $line.Trim()
        if ($t -eq '' -or $t.StartsWith(';') -or $t.StartsWith('#')) { continue }
        if ($t -match '^\[(.+)\]$') { $section = $Matches[1]; $ini[$section] = @{}; continue }
        if ($t -match '^([^=]+)=(.*)$') { $ini[$section][$Matches[1].Trim()] = $Matches[2].Trim() }
    }
    $ini
}

$cfg = Read-Ini $Config
$db = $cfg['database']; $ex = $cfg['export']
if ($db['Server'] -eq 'ЗАПОЛНИТЬ' -or $db['Database'] -eq 'ЗАПОЛНИТЬ') {
    throw "Заполните Server и Database в $Config"
}

$cs = "Server=$($db['Server']);Database=$($db['Database']);"
if ($db['Auth'] -eq 'Sql') { $cs += "User ID=$($db['User']);Password=$($db['Password']);" }
else { $cs += 'Integrated Security=True;' }

$outDir = Join-Path $ex['OutputDir'] (Get-Date -Format 'yyyy-MM-dd_HH-mm')
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$delim = if ($ex['Delimiter']) { $ex['Delimiter'] } else { ';' }
$timeout = if ($db['Timeout']) { [int]$db['Timeout'] } else { 600 }

$conn = New-Object System.Data.SqlClient.SqlConnection $cs
$conn.Open()
try {
    foreach ($name in ($ex['Queries'] -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
        $sqlPath = Join-Path (Join-Path $PSScriptRoot 'queries') $name
        $cmd = $conn.CreateCommand()
        $cmd.CommandText = Get-Content -LiteralPath $sqlPath -Raw -Encoding UTF8
        $cmd.CommandTimeout = $timeout
        $table = New-Object System.Data.DataTable
        (New-Object System.Data.SqlClient.SqlDataAdapter $cmd).Fill($table) | Out-Null
        $csv = Join-Path $outDir ([IO.Path]::ChangeExtension($name, '.csv'))
        $table | Select-Object -Property ($table.Columns | ForEach-Object ColumnName) |
            Export-Csv -LiteralPath $csv -Delimiter $delim -NoTypeInformation -Encoding UTF8
        Write-Host "$name -> $csv ($($table.Rows.Count) строк)"
    }
} finally { $conn.Close() }

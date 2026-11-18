# Collects every translatable text from the source (T(L"..."), TF(L"..."), N(L"...")
# and kt(L"...") in PatKit's window code) into lang\template.lang, and writes
# lang\lang.rc, which compiles the template and every finished language pack
# (lang\<code>.lang) into the exe. build.cmd runs this before compiling.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sources = @(Get-ChildItem "$root\src\*.cpp" | Where-Object { $_.Name -notin @('SelfTest.cpp', 'OpsTest.cpp') }) + @(Get-ChildItem "$root\src\app\*.inl")
$kit = Join-Path (Split-Path -Parent $root) 'PatKit\ui\KitWindow.cpp'
if (Test-Path $kit) { $sources += Get-Item $kit }
$rx = [regex]'\b(?:T|TF|N|kt|kitN)\(\s*L"((?:[^"\\]|\\.)*)"'
$seen = New-Object 'System.Collections.Generic.HashSet[string]'
$keys = New-Object 'System.Collections.Generic.List[string]'
foreach ($f in $sources) {
    $text = [IO.File]::ReadAllText($f.FullName, [Text.Encoding]::UTF8)
    foreach ($m in $rx.Matches($text)) {
        $k = $m.Groups[1].Value
        if ($seen.Add($k)) { $keys.Add($k) }
    }
}
New-Item -ItemType Directory -Force "$root\lang" | Out-Null
$sb = New-Object Text.StringBuilder
[void]$sb.Append("# PATio Boot Doctor: every text the program shows, in English.`n")
[void]$sb.Append("# Made by tools\extract-strings.ps1 when the program is built. Do not edit: copy it to <code>.lang to translate.`n`n")
[void]$sb.Append("@code template`n@name English (template)`n@rtl no`n`n")
foreach ($k in $keys) { [void]$sb.Append("msgid `"$k`"`nmsgstr `"$k`"`n`n") }
[IO.File]::WriteAllText("$root\lang\template.lang", $sb.ToString(), (New-Object Text.UTF8Encoding($false)))
$rc = New-Object Text.StringBuilder
[void]$rc.Append("// Made by tools\extract-strings.ps1: the language packs compiled into the exe.`r`n")
[void]$rc.Append("LANG_TEMPLATE RCDATA `"lang\\template.lang`"`r`n")
foreach ($p in Get-ChildItem "$root\lang\*.lang") {
    $code = $p.BaseName
    if ($code -eq 'template' -or $code -eq 'en') { continue }
    [void]$rc.Append("LANG_$($code.ToUpper()) RCDATA `"lang\\$($p.Name)`"`r`n")
}
[IO.File]::WriteAllText("$root\lang\lang.rc", $rc.ToString(), (New-Object Text.ASCIIEncoding))
Write-Host "Translatable texts: $($keys.Count)"

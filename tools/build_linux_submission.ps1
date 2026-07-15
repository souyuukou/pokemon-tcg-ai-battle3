param(
    [Parameter(Mandatory = $true)]
    [string]$Zig
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Source = Join-Path $Root "ptcg_engine/ptcgProgram 22/Export.cpp"
$Output = Join-Path $Root "sample_submission/sample_submission/cg/libcg.so"
if (-not (Test-Path -LiteralPath $Zig)) { throw "zig executable not found: $Zig" }

& $Zig c++ -target x86_64-linux-gnu.2.17 -std=c++20 -O3 -DNDEBUG -fPIC -shared `
    $Source -o $Output -pthread
if ($LASTEXITCODE) { throw "Linux cross-build failed ($LASTEXITCODE)" }

# GNU strip is available in the supported WSL build environment.  Stripping is
# packaging-only; it does not alter exported symbols or evaluator arithmetic.
$wslOutput = (wsl.exe wslpath -a $Output.Replace('\', '/')).Trim()
wsl.exe strip --strip-all $wslOutput
if ($LASTEXITCODE) { throw "strip failed ($LASTEXITCODE)" }

[pscustomobject]@{
    Path = $Output
    Bytes = (Get-Item -LiteralPath $Output).Length
    Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Output).Hash.ToLowerInvariant()
}

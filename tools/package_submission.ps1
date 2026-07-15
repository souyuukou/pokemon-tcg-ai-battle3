param(
    [string]$Output = "artifacts/pokemon-tcg-ai-submission.zip"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Source = Join-Path $Root "sample_submission/sample_submission"
$OutputPath = [System.IO.Path]::GetFullPath((Join-Path $Root $Output))
$Staging = Join-Path ([System.IO.Path]::GetTempPath()) ("ptcg-submission-" + [guid]::NewGuid())

try {
    New-Item -ItemType Directory -Force $Staging | Out-Null
    Get-ChildItem -LiteralPath $Source -Recurse -File | Where-Object {
        $_.FullName -notmatch '[\\/](__pycache__|\.pytest_cache)[\\/]' -and
        $_.Extension -notin '.pyc', '.pyo', '.pdb', '.lib', '.exp'
    } | ForEach-Object {
        $relative = [System.IO.Path]::GetRelativePath($Source, $_.FullName)
        $destination = Join-Path $Staging $relative
        New-Item -ItemType Directory -Force (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $destination
    }
    foreach ($required in @('main.py', 'deck.csv', 'exact-evaluator-v3.bin',
                             'cg/cg.dll', 'cg/libcg.so')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Staging $required))) {
            throw "submission is missing $required"
        }
    }
    New-Item -ItemType Directory -Force (Split-Path -Parent $OutputPath) | Out-Null
    Remove-Item -LiteralPath $OutputPath -Force -ErrorAction SilentlyContinue
    Compress-Archive -Path (Join-Path $Staging '*') -DestinationPath $OutputPath -CompressionLevel Optimal
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutputPath).Hash.ToLowerInvariant()
    [pscustomobject]@{ Path = $OutputPath; Bytes = (Get-Item $OutputPath).Length; Sha256 = $hash }
}
finally {
    Remove-Item -LiteralPath $Staging -Recurse -Force -ErrorAction SilentlyContinue
}

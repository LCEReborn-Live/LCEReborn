$ErrorActionPreference = 'Stop'
$setupArgs = $args
try {
    $toolsRoot = [IO.Path]::GetFullPath($PSScriptRoot)
    $runtime = Join-Path $toolsRoot 'python'
    $python = Join-Path $runtime 'python.exe'
    $pin = Get-Content -LiteralPath (Join-Path $toolsRoot 'python-runtime.json') -Raw | ConvertFrom-Json
    $marker = Join-Path $runtime '.runtime-ready'
    if (Test-Path -LiteralPath $marker) {
        if ((Get-Content -LiteralPath $marker -Raw).Trim() -ne $pin.sha256) {
            throw 'The private Python runtime has a different version. Move tools/python aside and retry.'
        }
    }
    if (-not (Test-Path -LiteralPath $python) -or -not (Test-Path -LiteralPath $marker)) {
        if (Test-Path -LiteralPath $runtime) { throw 'The private Python folder is incomplete. Move tools/python aside and retry.' }
        $zip = Join-Path $toolsRoot 'python-embed.zip'
        if (-not (Test-Path -LiteralPath $zip) -or (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256) {
            Write-Host 'Downloading the private server setup runtime (11 MB)...'
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
            $ProgressPreference = 'SilentlyContinue'
            Invoke-WebRequest -Uri $pin.url -OutFile $zip -UseBasicParsing
        }
        if ((Get-Item -LiteralPath $zip).Length -ne $pin.size -or (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256) {
            throw 'Python runtime checksum verification failed. Retry prepare-server.'
        }
        $temporary = Join-Path $toolsRoot ('python.partial-' + [Guid]::NewGuid().ToString('N'))
        $resolvedTemporary = [IO.Path]::GetFullPath($temporary)
        if (-not $resolvedTemporary.StartsWith($toolsRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid runtime staging path.' }
        try {
            Expand-Archive -LiteralPath $zip -DestinationPath $temporary
            $pth = @(Get-ChildItem -LiteralPath $temporary -Filter 'python*._pth')
            if ($pth.Count -ne 1) { throw 'Python runtime path configuration is missing.' }
            Add-Content -LiteralPath $pth[0].FullName -Value '..' -Encoding ASCII
            Set-Content -LiteralPath (Join-Path $temporary '.runtime-ready') -Value $pin.sha256 -Encoding ASCII
            if (Test-Path -LiteralPath $runtime) { throw 'Another setup created the Python runtime. Retry prepare-server.' }
            Move-Item -LiteralPath $temporary -Destination $runtime
        } finally {
            if ((Test-Path -LiteralPath $temporary) -and [IO.Path]::GetFullPath($temporary).StartsWith($toolsRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                Remove-Item -LiteralPath $temporary -Recurse -Force
            }
        }
    }
    & $python (Join-Path $toolsRoot 'prepare_server.py') @setupArgs
    exit $LASTEXITCODE
} catch {
    Write-Host ('Server preparation failed: ' + $_.Exception.Message)
    exit 1
}

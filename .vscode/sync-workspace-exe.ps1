param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("debug", "release")]
    [string]$Configuration,

    [Parameter(Mandatory = $true)]
    [string]$WorkspaceFolder
)

$workspaceBuildDir = Join-Path $WorkspaceFolder "build"
$buildFolderName = if ($Configuration -eq "debug") { "build-debug" } else { "build-release" }
$sourceExe = Join-Path $WorkspaceFolder ".cmake\$buildFolderName\NanquimoriEngine.exe"
$destinationExe = Join-Path $workspaceBuildDir "NanquimoriEngine.exe"

New-Item -ItemType Directory -Path $workspaceBuildDir -Force | Out-Null

if (!(Test-Path $sourceExe)) {
    Write-Error "Executavel nao encontrado: $sourceExe"
    exit 1
}

Copy-Item -Path $sourceExe -Destination $destinationExe -Force
Write-Host "Sincronizado: $destinationExe"

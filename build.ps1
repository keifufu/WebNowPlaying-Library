function Invoke-Environment {
  param
  (
    [Parameter(Mandatory=$true)]
    [string] $Command
  )

  $Command = "`"" + $Command + "`""
  cmd /c "$Command > nul 2>&1 && set" | . { process {
    if ($_ -match '^([^=]+)=(.*)') {
      [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2])
    }
  }}
}

$origEnv = Get-ChildItem Env:*;
$vsInstallationPath = & 'vswhere.exe' -property installationPath

if (-not $vsInstallationPath) {
  Write-Host "No Visual Studio installation found."
  exit 1
}

Remove-Item -Path "$PSScriptRoot\build" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "$PSScriptRoot\dist" -Recurse -Force -ErrorAction SilentlyContinue

New-Item -Path "$PSScriptRoot\build" -ItemType Directory
New-Item -Path "$PSScriptRoot\build\win64" -ItemType Directory
New-Item -Path "$PSScriptRoot\build\win32" -ItemType Directory
New-Item -Path "$PSScriptRoot\dist" -ItemType Directory

Invoke-Environment "$vsInstallationPath\VC\Auxiliary\Build\vcvars32.bat"
cmake -S "$PSScriptRoot" -B "$PSScriptRoot\build\win32" -DCMAKE_INSTALL_PREFIX="$PSScriptRoot\build\win32" -DCMAKE_BUILD_TYPE=Release -G "NMake Makefiles"
cmake --build "$PSScriptRoot\build\win32"
cmake --install "$PSScriptRoot\build\win32"
$origEnv | ForEach-Object { [System.Environment]::SetEnvironmentVariable($_.Name, $_.Value) }

Invoke-Environment "$vsInstallationPath\VC\Auxiliary\Build\vcvars64.bat"
cmake -S "$PSScriptRoot" -B "$PSScriptRoot\build\win64" -DCMAKE_INSTALL_PREFIX="$PSScriptRoot\build\win64" -DCMAKE_BUILD_TYPE=Release -G "NMake Makefiles"
cmake --build "$PSScriptRoot\build\win64"
cmake --install "$PSScriptRoot\build\win64"
$origEnv | ForEach-Object { [System.Environment]::SetEnvironmentVariable($_.Name, $_.Value) }

function package {
  param (
    [string]$platform
  )

  $files = @(
    "$PSScriptRoot\README.md",
    "$PSScriptRoot\LICENSE",
    "$PSScriptRoot\CHANGELOG.md",
    "$PSScriptRoot\VERSION",
    "$PSScriptRoot\build\$platform\lib",
    "$PSScriptRoot\build\$platform\include"
  )
  
  $version = (Get-Content "$PSScriptRoot\VERSION" | Out-String).Trim()
  Compress-Archive -Path $files -DestinationPath "$PSScriptRoot\dist\libwnp-${version}_$platform.zip" -CompressionLevel Optimal
}

package -platform "win32"
package -platform "win64"

$ErrorActionPreference = "Stop"

Set-Location $PSScriptRoot
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

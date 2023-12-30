@echo off

mkdir dist
set /p VERSION=<VERSION

set archive64=dist\libwnp-%VERSION%_win64_msvc.zip
if exist %archive64% del %archive64%
7z a %archive64% build\libwnp_win64.lib include\wnp.h CHANGELOG.md README.md LICENSE VERSION
7z rn %archive64% build\libwnp_win64.lib lib/wnp.lib

set archive64_nodp=dist\libwnp-%VERSION%_win64_msvc_nodp.zip
if exist %archive64_nodp% del %archive64_nodp%
7z a %archive64_nodp% build\libwnp_win64_nodp.lib include\wnp.h CHANGELOG.md README.md LICENSE VERSION
7z rn %archive64_nodp% build\libwnp_win64_nodp.lib lib/wnp.lib

set archive32=dist\libwnp-%VERSION%_win32_msvc.zip
if exist %archive32% del %archive32%
7z a %archive32% build\libwnp_win32.lib include\wnp.h CHANGELOG.md README.md LICENSE VERSION
7z rn %archive32% build\libwnp_win32.lib lib/wnp.lib

set archive32_nodp=dist\libwnp-%VERSION%_win32_msvc_nodp.zip
if exist %archive32_nodp% del %archive32_nodp%
7z a %archive32_nodp% build\libwnp_win32_nodp.lib include\wnp.h CHANGELOG.md README.md LICENSE VERSION
7z rn %archive32_nodp% build\libwnp_win32_nodp.lib lib/wnp.lib

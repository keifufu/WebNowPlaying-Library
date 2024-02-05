@echo off

REM Build the Python bindings for the WNP library
ECHO Generating the Python bindings for the WNP library using SWIG
swig -python -outdir wnp -o ../../src/wnp_wrap.c ../../src/wnp.i

REM Copy Needed Files
ECHO Copying the source files
cd ..\..
xcopy src bindings\python\src\ /y /q
xcopy src bindings\python\wnp\src\ /y /q
xcopy LICENSE bindings\python\ /y /q
xcopy README.md bindings\python\ /y /q
cd bindings\python
ECHO Done 

REM Create Packge
ECHO Creating the Python package
python -m build

REM Clean up
ECHO Cleaning up
del src\* /q
rd /s /q src
del wnp\src\* /q
rd /s /q wnp\src
del LICENSE README.md /q
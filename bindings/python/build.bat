@echo off

REM Build the Python bindings for the WNP library
ECHO Generating the Python bindings for the WNP library using SWIG
swig -python -outdir . -o ../../src/wnp_wrap.c ../../src/wnp.i


REM Run the build command
ECHO Building the Python bindings as a python extension module
python setup.py build_ext

REM Create Packge
ECHO Creating the Python package
python setup.py sdist bdist_wheel

REM Clean up
ECHO Cleaning up
del ..\src\* /q
rd /s /q ..\src
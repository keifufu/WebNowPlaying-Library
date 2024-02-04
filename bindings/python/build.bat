@echo off

REM Build the Python bindings for the WNP library
ECHO Generating the Python bindings for the WNP library using SWIG
swig -python -outdir . -o ../../src/wnp_wrap.c ../../src/wnp.i


REM Run the build command
ECHO Building the Python bindings as a python extension module
python setup.py build
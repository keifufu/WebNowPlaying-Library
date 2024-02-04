from setuptools import setup, Extension

wnp = Extension(
    "_wnp",
    sources=["../../src/wnp_wrap.c", "../../src/wnp.c", "../../src/cws.c", "../../src/dp_windows.cpp"],
    extra_compile_args=[
        "/std:c++17",
        "/D_CRT_SECURE_NO_WARNINGS",
        "/link",
        "/LIBPATH:C:\\Program Files\\Microsoft SDKs\\Windows\\v7.1A\\Lib",
        "Ws2_32.lib",
    ],
    libraries=['Ws2_32', 'gdiplus', 'advapi32', 'windowscodecs', 'ole32', 'oleaut32', 'uuid'],
    define_macros=[
        ('_WIN32_WINNT', '0x0600'),
    ],
)
setup(
    name="wnp",
    version="0.1",
    author="keifufu, YasogaN",
    description="""WebNowPlaying is a browser extension that allows you to control music players from a browser popup. This is a Python wrapper for the WebNowPlaying-Library.""",
    ext_modules=[wnp],
    py_modules=["wnp"],
)
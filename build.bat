@echo off
chcp 65001 >nul
setlocal

rem ================= 找一个可用的 g++ =================
set "GXX="
where g++ >nul 2>nul && set "GXX=g++"
if not defined GXX if exist "C:\tools\mingw64\bin\g++.exe" set "GXX=C:\tools\mingw64\bin\g++.exe"
if not defined GXX if exist "C:\mingw64\bin\g++.exe" set "GXX=C:\mingw64\bin\g++.exe"
if not defined GXX if exist "C:\msys64\mingw64\bin\g++.exe" set "GXX=C:\msys64\mingw64\bin\g++.exe"
if not defined GXX (
    echo 找不到 g++，请先安装 MinGW-w64 并加入 PATH，或把本脚本中的路径改成你的 g++ 位置。
    pause
    exit /b 1
)

echo 使用编译器: %GXX%

rem 中文用户名 + 系统 Temp 目录有写入权限问题，g++ 的临时文件会写失败；
rem 这里把 TMP/TEMP 指到纯 ASCII 路径，保证任何用户都能直接编译。
if not exist "C:\tools\gcc-tmp" mkdir "C:\tools\gcc-tmp"
set "TMP=C:\tools\gcc-tmp"
set "TEMP=C:\tools\gcc-tmp"

echo 正在编译...

"%GXX%" -O2 -std=c++17 -municode main.cpp config.cpp player.cpp -o ValoSoundBoard.exe ^
    -static -luser32 -lmfplat -lmf -lmfuuid -luuid -lole32
if errorlevel 1 (
    echo.
    echo 编译失败，请看上面的报错信息。
    pause
    exit /b 1
)

echo.
echo 编译成功：ValoSoundBoard.exe
echo 现在可以运行它了。
pause

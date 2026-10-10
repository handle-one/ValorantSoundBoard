@echo off
chcp 65001 >nul
rem 需要在 Visual Studio 的“开发者 PowerShell / 开发者命令提示符”里运行
echo 正在用 MSVC 编译...
where cl >nul 2>nul
if errorlevel 1 (
    echo 未找到 cl.exe。请打开“开始菜单 - Visual Studio - 开发者命令提示符”后再运行本脚本。
    pause
    exit /b 1
)
cl /nologo /O2 /std:c++17 /EHsc /utf-8 main.cpp config.cpp player.cpp /Fe:ValoSoundBoard.exe ^
   user32.lib mfplat.lib mf.lib mfuuid.lib uuid.lib ole32.lib
if errorlevel 1 (
    echo 编译失败
    pause
    exit /b 1
)
echo 编译成功：ValoSoundBoard.exe
pause

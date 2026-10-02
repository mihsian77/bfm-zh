@echo off
chcp 65001 >nul
title BFM 中文美化版 - 安装程序
setlocal enabledelayedexpansion

echo ============================================
echo   BFM 中文美化版 v1.2.1-zh.5 安装程序
echo ============================================
echo.

set "SCRIPT_DIR=%~dp0"
set "TARGET_DIR=C:\windows"

if not exist "%TARGET_DIR%\wfm.exe" (
    echo [信息] 未找到 WFM，正在搜索...
    for %%d in (C D E F G H) do (
        if exist "%%d:\windows\wfm.exe" (
            set "TARGET_DIR=%%d:\windows"
            goto :found
        )
    )
    echo [错误] 未找到 WFM
    pause
    exit /b 1
)
:found
echo [信息] 目标目录: %TARGET_DIR%
echo.

echo [0/5] 正在关闭 WFM...
taskkill /f /im wfm.exe >nul 2>&1
set "WAIT_COUNT=0"
:wait_exit
tasklist /fi "imagename eq wfm.exe" 2>nul | findstr /i "wfm.exe" >nul
if errorlevel 1 goto :proc_killed
set /a WAIT_COUNT+=1
if %WAIT_COUNT% geq 10 (
    echo [错误] 无法关闭 WFM，请手动关闭后按任意键继续
    pause >nul
    taskkill /f /im wfm.exe >nul 2>&1
    set "WAIT_COUNT=0"
)
ping -n 2 127.0.0.1 >nul
goto :wait_exit
:proc_killed
echo       WFM 已关闭
echo.

if exist "%TARGET_DIR%\wfm.exe.bak_zh" (
    echo [警告] 已存在备份，重新安装将覆盖
    choice /c YN /m "是否继续？"
    if errorlevel 2 (
        echo 已取消
        pause
        exit /b 0
    )
)

echo [1/5] 正在备份原版...
if not exist "%TARGET_DIR%\wfm.exe.bak_zh" (
    copy /y "%TARGET_DIR%\wfm.exe" "%TARGET_DIR%\wfm.exe.bak_zh" >nul
    echo       wfm.exe 已备份
)
if exist "%TARGET_DIR%\libcdio.dll" (
    if not exist "%TARGET_DIR%\libcdio.dll.bak_zh" (
        copy /y "%TARGET_DIR%\libcdio.dll" "%TARGET_DIR%\libcdio.dll.bak_zh" >nul
        echo       libcdio.dll 已备份
    )
)
echo.

echo [2/5] 正在安装 7-Zip...
set "SEVENZIP_DIR=Z:\opt\apps\7-Zip"
if exist "%SEVENZIP_DIR%\7z.exe" (
    echo       7-Zip 已安装，跳过
) else if exist "%SCRIPT_DIR%7-Zip\7z.exe" (
    echo       正在复制 7-Zip 文件...
    if not exist "%SEVENZIP_DIR%" mkdir "%SEVENZIP_DIR%"
    xcopy /e /i /y "%SCRIPT_DIR%7-Zip\*" "%SEVENZIP_DIR%\" >nul
    if exist "%SEVENZIP_DIR%\7z.exe" (
        echo       7-Zip 安装完成
    ) else (
        echo       [警告] 7-Zip 复制失败
    )
) else (
    echo       [注意] 未找到 7-Zip 目录，压缩功能受限
)
echo.

echo [3/5] 正在安装文件...
for %%A in ("%SCRIPT_DIR%wfm.exe") do set "SRC_SIZE=%%~zA"
copy /y "%SCRIPT_DIR%wfm.exe" "%TARGET_DIR%\wfm.exe" >nul
for %%A in ("%TARGET_DIR%\wfm.exe") do set "DST_SIZE=%%~zA"
if not "%SRC_SIZE%"=="%DST_SIZE%" (
    echo [错误] wfm.exe 复制失败
    pause
    exit /b 1
)
echo       wfm.exe 已复制 (%SRC_SIZE% 字节)

if exist "%SCRIPT_DIR%libcdio.dll" (
    for %%A in ("%SCRIPT_DIR%libcdio.dll") do set "SRC2=%%~zA"
    copy /y "%SCRIPT_DIR%libcdio.dll" "%TARGET_DIR%\libcdio.dll" >nul
    for %%A in ("%TARGET_DIR%\libcdio.dll") do set "DST2=%%~zA"
    if "!SRC2!"=="!DST2!" echo       libcdio.dll 已复制 (!SRC2! 字节)
    if not "!SRC2!"=="!DST2!" echo       [警告] libcdio.dll 复制失败
)
echo.

echo [4/5] 正在启动 WFM...
start "" "%TARGET_DIR%\wfm.exe"
echo.

echo [5/5] 正在验证...
ping -n 3 127.0.0.1 >nul
if exist "%TARGET_DIR%\wfm.exe" (
    echo       验证通过
) else (
    echo       [警告] 未找到 wfm.exe
)
echo.

echo ============================================
echo   安装完成！
echo ============================================
timeout /t 3 /nobreak >nul
exit /b 0

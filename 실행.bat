@echo off
chcp 65001 >nul 2>&1

echo ==========================================
echo   선거 공약 시스템
echo ==========================================
echo.

taskkill /IM server.exe /F >nul 2>&1
taskkill /IM client.exe /F >nul 2>&1

echo 선택하세요:
echo 1. 빌드만 하기
echo 2. 서버 실행
echo 3. 클라이언트 실행
echo 4. 서버 + 클라이언트 함께 실행
echo 5. 모든 프로세스 종료
echo 6. 프로젝트 정리
echo 0. 종료
echo.

set /p choice="선택 (0-6): "

if "%choice%"=="1" goto build
if "%choice%"=="2" goto server
if "%choice%"=="3" goto client
if "%choice%"=="4" goto both
if "%choice%"=="5" goto kill
if "%choice%"=="6" goto clean
if "%choice%"=="0" goto exit
goto invalid

:build
echo.
echo 🔨 빌드 중...
make clean >nul 2>&1
make all
if %errorlevel% neq 0 (
    echo ❌ 빌드 실패!
) else (
    echo ✅ 빌드 성공!
)
goto end

:server
echo.
echo 🚀 서버 실행 (Ctrl+C로 종료)...
make all >nul 2>&1
.\build\server.exe
goto end

:client
echo.
echo 🚀 클라이언트 실행...
make all >nul 2>&1
.\build\client.exe
goto end

:both
echo.
echo 🚀 서버를 백그라운드에서 시작...
make all >nul 2>&1
start /min "선거 공약 서버" .\build\server.exe
timeout /t 2 >nul
echo 🚀 클라이언트 시작...
.\build\client.exe
goto end

:kill
echo.
echo 🛑 모든 프로세스 종료 중...
taskkill /F /IM server.exe 2>nul
taskkill /F /IM client.exe 2>nul
echo ✅ 정리 완료!
goto end

:clean
echo.
echo 🧹 프로젝트 정리 중...
make clean >nul 2>&1
del *.log 2>nul
echo ✅ 정리 완료!
goto end

:invalid
echo ❌ 잘못된 선택입니다.
goto end

:exit
echo 👋 안녕히 가세요!
exit /b 0

:end
echo.
pause 
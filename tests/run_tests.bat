@echo off
rem Проверки корректности. Запуск из папки tests после сборки: ..\src\lab4.exe
set EXE=..\src\lab4.exe
set FAIL=0

for %%N in (5 7) do for %%T in (2 15) do (
  %EXE% --n %%N --think %%T --ops 50 --verbose 0 >nul
  if errorlevel 1 (echo FAIL: asym N=%%N think=%%T & set FAIL=1) else echo OK: asym N=%%N think=%%T
)

rem контрпример: ожидается ПРОВАЛ (обнаружена взаимная блокировка)
%EXE% --n 5 --think 2 --ops 50 --strategy naive --hold 5 --timeout 500 --verbose 0 >nul
if errorlevel 1 (echo OK: naive strategy deadlocks as expected) else (echo FAIL: naive did not deadlock & set FAIL=1)

if %FAIL%==0 (echo ALL TESTS PASSED) else (echo SOME TESTS FAILED & exit /b 1)

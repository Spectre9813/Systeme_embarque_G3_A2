@echo off
rem Lance build.ps1 sans changer la politique d execution de Windows.
rem Exemples : build build ^| build upload ^| build demo ^| build monitor
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*

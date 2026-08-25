@echo off
chcp 65001 >nul
cd /d "%~dp0"

if exist ".venv\Scripts\activate.bat" call ".venv\Scripts\activate.bat"

rem ---- 带参数：原样透传（如 train.bat cleaned.json --model=xgb --seed=7）----
if not "%~1"=="" (
    python train.py %*
    goto :end
)

rem ---- 不带参数：跑 rf_lr 种子稳定性三连 ----
echo.

python train.py cleaned.json fixes.json --model=rf_lr --seed=7

:end
pause
@echo off
chcp 65001 >nul
cd /d "%~dp0"

if exist ".venv\Scripts\activate.bat" (
    call ".venv\Scripts\activate.bat"
) else (
    echo [warn] 未找到 .venv，使用系统 Python
)

echo == 训练 ==
python train.py cleaned.json %*

:end
pause
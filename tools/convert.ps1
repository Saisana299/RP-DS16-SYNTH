$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $scriptDir
python wave_converter.py
Read-Host -Prompt "Press Enter to continue"
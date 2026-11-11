@echo off
REM Upload an SREC file to the C256 Foenix

python ..\C256Mgr\c256mgr.py --upload-srec ldemutos.s28
python --binary emutos-a2560x.rom --address 0x100000
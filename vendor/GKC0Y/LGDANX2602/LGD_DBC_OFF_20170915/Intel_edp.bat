:start
anx_Tcon_EEpromUpdate.exe ANX2602_Intel-EDP_Batch.txt
IF %errorlevel% NEQ 0 GOTO :error
GOTO :end
:error
FailMsg.exe
EXIT 1
:end
SuccessMsg.exe
EXIT 0

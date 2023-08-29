rem File for programming target software using ST-LINK
rem sergey12malyshev 29.08.23

"C:\Program Files (x86)\STMicroelectronics\STM32 ST-LINK Utility\ST-LINK Utility\ST-Link_CLI.exe" -c SWD FREQ=4000 -P "build\ch.hex" -V -Rst

TIMEOUT /T 10

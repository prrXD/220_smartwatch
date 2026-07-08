@echo off
cd /d "C:\Users\38389\AppData\Local\stm32cube\bundles\stlink-upgrader\3.17.10+st.1"
start "ST-Link Upgrade" "C:\Users\38389\AppData\Local\stm32cube\bundles\adoptium-jre\21.0.8+9.st.2\bin\javaw.exe" -jar "STLinkUpgrade.jar"
echo ST-Link Firmware Upgrade tool launched.
echo If a window doesn't appear, check your Java installation.

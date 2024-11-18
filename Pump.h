// Pump.h
#pragma once

void updatePump();
void stopPump();
void startPump();
void onPumpAlarmCommand(bool state, HASwitch* sender);
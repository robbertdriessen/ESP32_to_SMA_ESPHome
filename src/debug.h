#ifndef DEBUG_H_
#define DEBUG_H_

#include "Arduino.h"
#include <WiFi.h>
#include "site_details.h"

void debugMsgLn(String part);
void debugMsg(String part, unsigned int value);
void debugMsgLn(String part, uint8_t value);
void debugMsg(String part);
void debugMsg(String part, unsigned int value);
void debugMsg(String part, uint8_t value);
void debugSetup();

#endif // DEBUG_H_

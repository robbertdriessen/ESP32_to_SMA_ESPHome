
#include "debug.h"
#include "EspMQTTClient.h"

WiFiUDP DebugUdp;
String debugMsgLine;
extern EspMQTTClient client;

void debugSetup()
{
    debugMsgLine.reserve(128);
}

#ifdef DEBUG_HOST
void sendDebugUDP(String msg)
{
    if (client.isConnected())
    {
        DebugUdp.beginPacket(DEBUG_HOST, DEBUG_PORT);
        DebugUdp.print("msg: ");
        DebugUdp.print(msg);
        // for (int i = 0; i < msg.length(); i++)
        // {
        //   DebugUdp.write(msg.charAt(i));
        // }
        DebugUdp.endPacket();
    }
}
#endif // DEBUG_HOST

void debugMsgLn(String part)
{
    debugMsg(part + "\n");
}


void debugMsgLn(String part, unsigned int value) {
    debugMsg(part, value);
    debugMsgLn("");
}


void debugMsgLn(String part, uint8_t value)
{
    debugMsg(part, value);
    debugMsgLn("");
}

void debugMsg(String part)
{
    Serial.print(part);
#ifdef DEBUG_HOST
    debugMsgLine += part;
    if (debugMsgLine.endsWith("\n"))
    {
        sendDebugUDP(debugMsgLine);
        debugMsgLine = "";
    }
#endif // DEBUG_HOST
}

void debugMsg(String part, unsigned int value) {
    Serial.print(part);
    Serial.print(" ");
    Serial.printf("%i", value);
#ifdef DEBUG_HOST
    debugMsgLine += part;
    debugMsgLine += " ";
    debugMsgLine += value;
    if (debugMsgLine.endsWith("\n"))
    {
        sendDebugUDP(debugMsgLine);
        debugMsgLine = "";
    }
#endif // DEBUG_HOST
}

void debugMsg(String part, uint8_t value) {
    debugMsg(part, (unsigned int) value);
}

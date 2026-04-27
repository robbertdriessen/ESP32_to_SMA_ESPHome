#include "ESP32_SMA_Inverter.h"

#include "bluetooth.h"
#include "SMANetArduino.h"
#include "LocalUtil.h"
#include "mainstate.h"

#include <ets-appender.hpp>
#include <udp-appender.hpp>

#include "site_details.h"

// Dump up to `count` bytes from `buf` as space-separated hex with `tag` prefix.
// Used to diagnose responses that fail the L2-frame check (e.g. the DC query
// reply on a SunnyBoy that doesn't return PPP-framed data). Uses log_i (ESP-IDF
// macro) rather than the class-level logI so it can run from a free function;
// ESP32_SMA::setup() hooks ESP-IDF logs into the same UDP appender.
static void logHexDump(const char* tag, const unsigned char* buf, int count)
{
  if (count <= 0) { log_i("%s [0 bytes]:", tag); return; }
  if (count > 120) count = 120; // packet buffer max
  char line[3 * 121 + 1];
  int pos = 0;
  for (int i = 0; i < count; i++) {
    pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", buf[i]);
    if (pos >= (int)sizeof(line) - 4) break;
  }
  if (pos > 0 && line[pos - 1] == ' ') line[pos - 1] = '\0';
  log_i("%s [%d bytes]: %s", tag, count, line);
}

bool ESP32_SMA_Inverter::initialiseSMAConnection()
{
  logD("initialiseSMAConnection(%i) ", innerstate);


  unsigned char netid;
  switch (innerstate)
  {
  case 0:
  logD("AC: send request");
    //Wait for announcement/broadcast message from PV inverter
    if (getPacket(0x0002))
      innerstate++;
    break;

  case 1:
  logD("AC: waiting multi-packet");
    //Extract data from the 0002 packet
    netid = level1packet[4];

    // Now create a response and send it.
    for (int i = 0; i < sizeof(level1packet); i++)
      level1packet[i] = 0x00;

    writePacketHeader(level1packet, 0x02, 0x00, smaBTInverterAddressArray);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packet99, sizeof_smanet2packet99 );
    writeSMANET2SingleByte(level1packet, netid);
    writeSMANET2ArrayFromProgmem(level1packet, fourzeros, sizeof_fourzeros );
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packet0x01000000, sizeof(smanet2packet0x01000000));
    writePacketLength(level1packet);
    sendPacket(level1packet);
    innerstate++;
    break;

  case 2:
  logD("AC: parse response");
    // The SMA inverter will respond with a packet carrying the command '0x000A'.
    // It will return with cmdcode set to 0x000A.
    if (getPacket(0x000a))
      innerstate++;
    break;

  case 3:
    // The SMA inverter will now send two packets, one carrying the '0x000C' command, then the '0x0005' command.
    // Sit in the following loop until you get one of these two packets.
    cmdcode = readLevel1PacketFromBluetoothStream(0);
    if ((cmdcode == 0x000c) || (cmdcode == 0x0005))
      innerstate++;
    else if (cmdcode == 0xFFFF) {
      // transient failure, retry from case 2 next loop
      innerstate = 2;
    }
    break;

  case 4:
    // If the most recent packet was command code = 0x0005 skip this next line, otherwise, wait for 0x0005 packet.
    // Since the first SMA packet after a 0x000A packet will be a 0x000C packet, you'll probably sit here waiting at least once.
    if (cmdcode == 0x0005)
    {
      innerstate++;
    }
    else
    {
      if (getPacket(0x0005))
        innerstate++;
    }
    break;

  case 5:
    //First SMANET2 packet
    writePacketHeader(level1packet, sixff);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xa0, packet_send_counter, 0, 0, 0);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof_smanet2packetx80x00x02x00);
    writeSMANET2SingleByte(level1packet, 0x00);
    writeSMANET2ArrayFromProgmem(level1packet, fourzeros, sizeof_fourzeros);
    writeSMANET2ArrayFromProgmem(level1packet, fourzeros, sizeof_fourzeros);
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);

    if (getPacket(0x0001) && validateChecksum())
    {
      innerstate++;
      packet_send_counter++;
    }
    else if (cmdcode == 0xFFFF) {
      innerstate = 5; // retry sending first packet
    }
    break;

  case 6:
    //Second SMANET2 packet
    writePacketHeader(level1packet, sixff);
    writeSMANET2PlusPacket(level1packet, 0x08, 0xa0, packet_send_counter, 0x00, 0x03, 0x03);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packet2, sizeof_smanet2packet2);

    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);
    packet_send_counter++;

    innerstate++;
    break;

  default:
    return true;
  }

  return false;
}




bool ESP32_SMA_Inverter::logonSMAInverter()
{
  logD("logonSMAInverter(%i)" , innerstate);

  //Third SMANET2 packet
  switch (innerstate)
  {
  case 0:
  logD("DC: send request");
    writePacketHeader(level1packet, sixff);
    writeSMANET2PlusPacket(level1packet, 0x0e, 0xa0, packet_send_counter, 0x00, 0x01, 0x01);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packet_logon, sizeof(smanet2packet_logon));
    writeSMANET2ArrayFromProgmem(level1packet, fourzeros, sizeof_fourzeros);

    //INVERTER PASSWORD
    for (int passcodeloop = 0; passcodeloop < sizeof(SMAInverterPasscode); passcodeloop++)
    {
      unsigned char v = pgm_read_byte(SMAInverterPasscode + passcodeloop);
      writeSMANET2SingleByte(level1packet, (v + 0x88) % 0xff);
    }

    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);

    innerstate++;
    break;

  case 1:
  logD("DC: waiting multi-packet");
    if (getPacket(0x0001) && validateChecksum())
    {
      innerstate++;
      packet_send_counter++;
    }
    else if (cmdcode == 0xFFFF) {
      innerstate = 6; // retry
    }
    break;

  default:
    return true;
  }

  return false;
}



bool ESP32_SMA_Inverter::getDailyYield()
{
  //We expect a multi packet reply to this question...
  //We ask the inverter for its DAILY yield (generation)
  //once this is returned we can extract the current date/time from the inverter and set our internal clock
  logD("getDailyYield(%i)" , innerstate);

  switch (innerstate)
  {
  case 0:
    writePacketHeader(level1packet);
    //writePacketHeader(level1packet,0x01,0x00,smaBTInverterAddressArray);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xa0, packet_send_counter, 0, 0, 0);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof(smanet2packetx80x00x02x00));
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packet6, sizeof(smanet2packet6));
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);

    sendPacket(level1packet);

    innerstate++;
    break;

  case 1:
    if (getPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
        innerstate = 0;
    }
    break;

  case 2:
  logD("DC: parse response");
    //Returns packet looking like this...
    //    7E FF 03 60 65 0D 90 5C AF F0 1D 50 00 00 A0 83
    //    00 1E 6C 5D 7E 00 00 00 00 00 00 03
    //    80 01 02 00
    //    54 01 00 00 00 01 00 00 00 01
    //    22 26  //command code 0x2622 daily yield
    //    00     //unknown
    //    D6 A6 99 4F  //Unix time stamp (backwards!) = 1335469782 = Thu, 26 Apr 2012 19:49:42 GMT
    //    D9 26 00     //Daily generation 9.945 kwh
    //    00
    //    00 00 00 00
    //    18 61    //checksum
    //    7E       //packet trailer

    // Does this packet contain the British Summer time flag?
    //dumpPacket('Y');

    valuetype = level1packet[40 + 1 + 1] + level1packet[40 + 2 + 1] * 256;

    //Serial.println(valuetype,HEX);
    //Make sure this is the right message type
    if (valuetype == 0x2622)
    {
      memcpy(&value64, &level1packet[40 + 8 + 1], 8);
      //0x2622=Day Yield Wh
      // memcpy(&datetime, &level1packet[40 + 4 + 1], 4);
      datetime = LocalUtil::get_long(level1packet + 40 + 1 + 4);
      // debugMsg("Current Time (epoch): ");
      // debugMsgLn(String(datetime));

      //setTime(datetime);
      currentvalue = value64;
      logI("Day Yield: %f" , (double)value64 / 1000);
      _client.publish(MQTT_BASE_TOPIC "generation_today", LocalUtil::uint64ToString(currentvalue), true);


      if (1==0) {
        if (!_client.isConnected() ){
            log_i("_client not connected here ");
        } else {
            log_i("_client is connected here ");
        }
      }

    }
    innerstate++;
    break;

  default:
    return true;
  }

  return false;
}


bool ESP32_SMA_Inverter::getInstantACPower()
{
  logD("getInstantACPower(%i)" , innerstate);

  int32_t thisvalue;
  //Get spot value for instant AC wattage
  // debugMsg("getInstantACPower stage: ");
  // debugMsgLn(String(innerstate));

  switch (innerstate)
  {
  case 0:
    writePacketHeader(level1packet); //writePacketHeader(pcktBuf, 0x01, addr_unknown);
    //writePacketHeader(level1packet,0x01,0x00,smaBTInverterAddressArray);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xA1, packet_send_counter, 0, 0, 0); // writePacket(pcktBuf, 0x09, 0xA0, 0, device->SUSyID, device->Serial);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof(smanet2packetx80x00x02x00));
    writeSMANET2ArrayFromProgmem(level1packet, smanet2acspotvalues, sizeof(smanet2acspotvalues));
    writeSMANET2PlusPacketTrailer(level1packet); //writePacketTrailer(pcktBuf);
    writePacketLength(level1packet); //writePacketLength(pcktBuf);

    sendPacket(level1packet);
    innerstate++;
    break;

  case 1:
    if (waitForMultiPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
        innerstate = 0;
    }
    else {
      // timeout, retry request next loop
      innerstate = 0;
    }
    break;

  case 2:
    // Loop through all value types in the packet
    {
      bool found_ac = false;
      for (int i = 40 + 1; i < packetposition - 3; i += 28) {
        valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
        memcpy(&value, &level1packet[i + 8], 4);
        memcpy(&datetime, &level1packet[i + 4], 4);
        if (valuetype == 0x263f) { // Total AC power
          currentvalue = value;
          logI("AC Pwr Total= %li ", value);
          _client.publish(MQTT_BASE_TOPIC "instant_ac", LocalUtil::uint64ToString(currentvalue), true);
          _client.publish(MQTT_BASE_TOPIC "inv_time", String(datetime), true);
          spotpowerac = currentvalue;
          found_ac = true;

          // Software-tracked daily peak. There is no SMA LRI for "max Pac today",
          // so derive it from the running AC total. Reset at local-midnight rollover.
          time_t now_local = esp32rtc.getEpoch();
          struct tm tm_local; localtime_r(&now_local, &tm_local);
          if (tm_local.tm_yday != maxpowertoday_yday) {
            maxpowertoday = 0;
            maxpowertoday_yday = tm_local.tm_yday;
          }
          if (value > (long)maxpowertoday) {
            maxpowertoday = (unsigned long)value;
            _client.publish(MQTT_BASE_TOPIC "max_power_today", String(maxpowertoday), true);
          }
        } else if (valuetype == 0x4640) { // PAC1
          logI("AC Pwr L1= %li ", value);
          _client.publish(MQTT_BASE_TOPIC "pac1", LocalUtil::uint64ToString(value), true);
        } else if (valuetype == 0x4641) { // PAC2
          logI("AC Pwr L2= %li ", value);
          _client.publish(MQTT_BASE_TOPIC "pac2", LocalUtil::uint64ToString(value), true);
        } else if (valuetype == 0x4642) { // PAC3
          logI("AC Pwr L3= %li ", value);
          _client.publish(MQTT_BASE_TOPIC "pac3", LocalUtil::uint64ToString(value), true);
        }
      }
      if (!found_ac) {
        // Fallback to old method
        datetime = LocalUtil::get_long(level1packet + 40 + 1 + 4);
        thisvalue = LocalUtil::get_long(level1packet + 40 + 1 + 8);
        currentvalue = thisvalue;
        logI("AC Pwr= %li ", thisvalue);
        _client.publish(MQTT_BASE_TOPIC "instant_ac", LocalUtil::uint64ToString(currentvalue), true);
        _client.publish(MQTT_BASE_TOPIC "inv_time", String(datetime), true);
        spotpowerac = thisvalue;
      }
    }
    innerstate++;
    break;

  default:
    return true;
  }

  return false;
}

bool ESP32_SMA_Inverter::getTotalPowerGeneration()
{

  //Gets the total kWh the SMA inverter has generated in its lifetime...
  // debugMsg("getTotalPowerGeneration stage: ");
  // debugMsgLn(String(innerstate));
  logD("getTotalPowerGeneration(%i)" , innerstate);

  switch (innerstate)
  {
  case 0:
    writePacketHeader(level1packet);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xa0, packet_send_counter, 0, 0, 0);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof(smanet2packetx80x00x02x00));
    writeSMANET2ArrayFromProgmem(level1packet, smanet2totalyieldWh, sizeof(smanet2totalyieldWh));
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);

    sendPacket(level1packet);
    innerstate++;
    break;

  case 1:
    if (waitForMultiPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
        innerstate = 0;
    }
    else {
      innerstate = 0;
    }
    break;

  case 2:
    //displaySpotValues(16);
    memcpy(&datetime, &level1packet[40 + 1 + 4], 4);
    memcpy(&value64, &level1packet[40 + 1 + 8], 8);
    //digitalClockDisplay(datetime);
    currentvalue = value64;
    logI("Total Power: %f ", (double)value64 / 1000);
    //reportValue(MQTT_BASE_TOPIC "generation_total", LocalUtil::uint64ToString(currentvalue), true);
    _client.publish(MQTT_BASE_TOPIC "generation_total", LocalUtil::uint64ToString(currentvalue), true);
    innerstate++;
    break;

  default:
    return true;
  }

  return false;
}

bool ESP32_SMA_Inverter::getInstantDCPower()
{
#ifndef FETCH_DC_INSTANT_POWER
  return true;
#else
  logD("getInstantDCPower(%i)", innerstate);

  // Two SBFspot-style queries chained together so we get Pdc and Udc/Idc:
  //   states 0..2 = SpotDCPower    (LRI 0x251E)              -> pdc1/pdc2
  //   states 3..5 = SpotDCVoltage  (LRI 0x451F + 0x4521)     -> udc/idc per string
  // Each phase has a hard timeout: if the inverter doesn't respond we skip
  // forward instead of spinning back to state 0 (previously caused a watchdog
  // reboot on inverters that ignore the DC query).
  static unsigned long phaseStartMs = 0;
  static unsigned long pdc1_acc = 0, pdc2_acc = 0;
  static float vdc1_acc = 0, vdc2_acc = 0, adc1_acc = 0, adc2_acc = 0;
  const unsigned long PHASE_TIMEOUT_MS = 4000;

  switch (innerstate)
  {
  case 0:
    pdc1_acc = pdc2_acc = 0;
    vdc1_acc = vdc2_acc = adc1_acc = adc2_acc = 0;
    phaseStartMs = millis();
    writePacketHeader(level1packet);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xA1, packet_send_counter, 0, 0, 0);
    // smanet2packetdcpower has the {0x80,0x00,0x02,0x80} prefix baked in,
    // so the shared smanet2packetx80x00x02x00 prefix MUST NOT be prepended here.
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetdcpower, sizeof_smanet2packetdcpower);
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);
    innerstate++;
    break;

  case 1:
    if (millis() - phaseStartMs > PHASE_TIMEOUT_MS) {
      logW("DC Power phase timeout - moving on");
      innerstate = 3; // skip parse, try DC voltage phase
      break;
    }
    if (waitForMultiPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
      {
        // SunnyBoy 4000US returns DC responses with a short 7-byte header
        // instead of the usual ~41-byte PPP frame, so containsLevel2Packet()
        // rejects them. Try parsing 28-byte records starting at offset 7
        // before giving up. Records that don't exist won't iterate.
        logW("DC Power: PPP frame check failed, attempting short-frame parse");
        logHexDump("DC Power raw body", level1packet, packetposition);
        for (int i = 7; i + 28 <= (int)packetposition - 3; i += 28)
        {
          valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
          memcpy(&value, &level1packet[i + 8], 4);
          uint8_t cls = level1packet[i + 0];
          logI("DC Pdc short-frame: cls=%u lri=0x%04X value=%lu", cls, valuetype, value);
          if (valuetype == 0x251e) {
            if (cls == 1) pdc1_acc = value;
            else if (cls == 2) pdc2_acc = value;
          }
        }
        innerstate = 3;
      }
    }
    // else: fall through and try again until timeout
    break;

  case 2:
    for (int i = 40 + 1; i + 28 <= packetposition - 3; i += 28)
    {
      valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
      memcpy(&value, &level1packet[i + 8], 4);
      uint8_t cls = level1packet[i + 0];

      logD("DC Pdc record: cls=%u valuetype=0x%04X value=%lu", cls, valuetype, value);

      if (valuetype == 0x251e) {
        if (cls == 1) pdc1_acc = value;
        else if (cls == 2) pdc2_acc = value;
      }
      memcpy(&datetime, &level1packet[i + 4], 4);
    }
    innerstate++;
    break;

  case 3:
    phaseStartMs = millis();
    writePacketHeader(level1packet);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xA1, packet_send_counter, 0, 0, 0);
    // smanet2packetdcvoltage also bakes the {0x80,0x00,0x02,0x80} prefix in.
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetdcvoltage, sizeof_smanet2packetdcvoltage);
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);
    innerstate++;
    break;

  case 4:
    if (millis() - phaseStartMs > PHASE_TIMEOUT_MS) {
      logW("DC Voltage phase timeout - moving on");
      innerstate = 6; // skip to publish
      break;
    }
    if (waitForMultiPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
      {
        // Same short-frame fallback for the DC Voltage phase. SunnyBoy 4000US
        // returns Idc records (LRI 0x4521) at offset 7 with 28-byte stride;
        // Udc records (0x451F) probably arrive in a continuation packet we
        // don't currently read, so they'll typically be absent here.
        logW("DC Voltage: PPP frame check failed, attempting short-frame parse");
        logHexDump("DC Voltage raw body", level1packet, packetposition);
        for (int i = 7; i + 28 <= (int)packetposition - 3; i += 28)
        {
          valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
          memcpy(&value, &level1packet[i + 8], 4);
          uint8_t cls = level1packet[i + 0];
          logI("DC Udc/Idc short-frame: cls=%u lri=0x%04X value=%lu", cls, valuetype, value);
          if (valuetype == 0x451f) {
            if (cls == 1) vdc1_acc = (float)value / 100.0f;
            else if (cls == 2) vdc2_acc = (float)value / 100.0f;
          } else if (valuetype == 0x4521) {
            if (cls == 1) adc1_acc = (float)value / 1000.0f;
            else if (cls == 2) adc2_acc = (float)value / 1000.0f;
          }
        }
        innerstate = 6;
      }
    }
    break;

  case 5:
    for (int i = 40 + 1; i + 28 <= packetposition - 3; i += 28)
    {
      valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
      memcpy(&value, &level1packet[i + 8], 4);
      uint8_t cls = level1packet[i + 0];

      logD("DC Udc/Idc record: cls=%u valuetype=0x%04X value=%lu", cls, valuetype, value);

      if (valuetype == 0x451f) {
        if (cls == 1) vdc1_acc = (float)value / 100.0f;
        else if (cls == 2) vdc2_acc = (float)value / 100.0f;
      } else if (valuetype == 0x4521) {
        if (cls == 1) adc1_acc = (float)value / 1000.0f;
        else if (cls == 2) adc2_acc = (float)value / 1000.0f;
      }
      memcpy(&datetime, &level1packet[i + 4], 4);
    }
    innerstate++;
    break;

  case 6:
    {
      // Aggregate and publish whatever we managed to collect this cycle.
      spotpowerdc = pdc1_acc + pdc2_acc;
      spotvoltdc  = (vdc1_acc > 0 && vdc2_acc > 0) ? (vdc1_acc + vdc2_acc) / 2.0f
                   : (vdc1_acc > 0 ? vdc1_acc : vdc2_acc);
      spotampdc   = adc1_acc + adc2_acc;

      // If the inverter didn't return Pdc records (common with the short-frame
      // DC response on some SunnyBoys) but did return U and I, derive P=U*I.
      // Also guard against implausibly-large reported values.
      if (spotpowerdc == 0 || spotpowerdc > MAX_SPOTDC) {
        if (spotvoltdc > 0 && spotampdc > 0) {
          spotpowerdc = (unsigned long)(spotvoltdc * spotampdc);
        }
      }

      logI("DC Pwr=%lu Volt=%.2f Amp=%.3f", spotpowerdc, spotvoltdc, spotampdc);

      _client.publish(MQTT_BASE_TOPIC "instant_dc",   String(spotpowerdc), true);
      _client.publish(MQTT_BASE_TOPIC "instant_vdc",  String(spotvoltdc),  true);
      _client.publish(MQTT_BASE_TOPIC "instant_adc",  String(spotampdc),   true);
      if (vdc1_acc > 0) _client.publish(MQTT_BASE_TOPIC "instant_vdc1", String(vdc1_acc), true);
      if (vdc2_acc > 0) _client.publish(MQTT_BASE_TOPIC "instant_vdc2", String(vdc2_acc), true);
      if (adc1_acc > 0) _client.publish(MQTT_BASE_TOPIC "instant_adc1", String(adc1_acc), true);
      if (adc2_acc > 0) _client.publish(MQTT_BASE_TOPIC "instant_adc2", String(adc2_acc), true);
      if (pdc1_acc > 0) _client.publish(MQTT_BASE_TOPIC "instant_pdc1", String(pdc1_acc), true);
      if (pdc2_acc > 0) _client.publish(MQTT_BASE_TOPIC "instant_pdc2", String(pdc2_acc), true);
    }
    innerstate++;
    break;

  default:
    return true;
  }

  return false;
#endif
}



/*
//Inverter name
 prog_uchar PROGMEM smanet2packetinvertername[]={   0x80, 0x00, 0x02, 0x00, 0x58, 0x00, 0x1e, 0x82, 0x00, 0xFF, 0x1e, 0x82, 0x00};  
 
 void getInverterName() {
 
 do {
 //INVERTERNAME
 debugMsgln("InvName"));
 writePacketHeader(level1packet,sixff);
 //writePacketHeader(level1packet,0x01,0x00,sixff);
 writeSMANET2PlusPacket(level1packet,0x09, 0xa0, packet_send_counter, 0, 0, 0);
 writeSMANET2ArrayFromProgmem(level1packet,smanet2packetinvertername);
 writeSMANET2PlusPacketTrailer(level1packet);
 writePacketLength(level1packet);
 sendPacket(level1packet);
 
 waitForMultiPacket(0x0001);
 } 
 while (!validateChecksum());
 packet_send_counter++;
 
 valuetype = level1packet[40+1+1]+level1packet[40+2+1]*256;
 
 if (valuetype==0x821e) {
 memcpy(invertername,&level1packet[48+1],14);
 Serial.println(invertername);
 memcpy(&datetime,&level1packet[40+4+1],4);  //Returns date/time unit switched PV off for today (or current time if its still on)
 }
 }
 
 void HistoricData() {
 
 time_t currenttime=now();
 digitalClockDisplay(currenttime);
 
 debugMsgln("Historic data...."));
 tmElements_t tm;
 if( year(currenttime) > 99)
 tm.Year = year(currenttime)- 1970;
 else
 tm.Year = year(currenttime)+ 30;  
 
 tm.Month = month(currenttime);
 tm.Day = day(currenttime);
 tm.Hour = 10;      //Start each day at 5am (might need to change this if you're lucky enough to live somewhere hot and sunny!!
 tm.Minute = 0;
 tm.Second = 0;
 time_t startTime=makeTime(tm);  //Midnight
 
 
 //Read historic data for today (SMA inverter saves 5 minute averaged data)
 //We read 30 minutes at a time to save RAM on Arduino
 
 //for (int hourloop=1;hourloop<24*2;hourloop++)
 while (startTime < now()) 
 {
 //HowMuchMemory();
 
 time_t endTime=startTime+(25*60);  //25 minutes on
 
 //digitalClockDisplay(startTime);
 //digitalClockDisplay(endTime);
 //debugMsgln(" ");
 
 do {
 writePacketHeader(level1packet);
 //writePacketHeader(level1packet,0x01,0x00,smaBTInverterAddressArray);
 writeSMANET2PlusPacket(level1packet,0x09, 0xE0, packet_send_counter, 0, 0, 0);
 
 writeSMANET2SingleByte(level1packet,0x80);
 writeSMANET2SingleByte(level1packet,0x00);
 writeSMANET2SingleByte(level1packet,0x02);
 writeSMANET2SingleByte(level1packet,0x00);
 writeSMANET2SingleByte(level1packet,0x70);
 // convert from an unsigned long int to a 4-byte array
 writeSMANET2Long(level1packet,startTime);
 writeSMANET2Long(level1packet,endTime);
 writeSMANET2PlusPacketTrailer(level1packet);
 writePacketLength(level1packet);
 sendPacket(level1packet);
 
 waitForMultiPacket(0x0001);
 }
 while (!validateChecksum());
 //debugMsg("packetlength=");    Serial.println(packetlength);
 
 packet_send_counter++;
 
 //Loop through values
 for(int x=40+1;x<(packetposition-3);x+=12){
 memcpy(&value,&level1packet[x+4],4);
 
 if (value > currentvalue) {
 memcpy(&datetime,&level1packet[x],4);
 digitalClockDisplay(datetime);
 debugMsg("=");
 Serial.println(value);
 currentvalue=value;         
 
 //uploadValueToSolarStats(currentvalue,datetime);          
 }
 }
 
 startTime=endTime+(5*60);
 delay(750);  //Slow down the requests to the SMA inverter
 }
 }
 */

//-------------------------------------------------------------------------------------------
bool ESP32_SMA_Inverter::checkIfNeedToSetInverterTime()
{
  //digitalClockDisplay(now());Serial.println("");
  //digitalClockDisplay(datetime);Serial.println("");
  logI("checkIfNeedToSetInverterTime()");

  unsigned long timediff;

  timediff = abs((long) (datetime - esp32rtc.getEpoch()));
  logI("Time diff: %lu, datetime: %lu, epoch: %lu " , timediff, datetime, esp32rtc.getEpoch());

  if (timediff > 60)
  {
    //If inverter clock is out by more than 1 minute, set it to the time from NTP, saves filling up the
    //inverters event log with hundred of "change time" lines...
    setInverterTime(); //Set inverter time to now()
  }

  return true;
}



void ESP32_SMA_Inverter::setInverterTime()
{
  //Sets inverter time for those SMA inverters which don't have a realtime clock (Tripower 8000 models for instance)
  logD("setInverterTime(%i) " , innerstate);

  //Payload...

  //** 8C 0A 02 00 F0 00 6D 23 00 00 6D 23 00 00 6D 23 00
  //   9F AE 99 4F   ==Thu, 26 Apr 2012 20:22:55 GMT  (now)
  //   9F AE 99 4F   ==Thu, 26 Apr 2012 20:22:55 GMT  (now)
  //   9F AE 99 4F   ==Thu, 26 Apr 2012 20:22:55 GMT  (now)
  //   01 00         ==Timezone +1 hour for BST ?
  //   00 00
  //   A1 A5 99 4F   ==Thu, 26 Apr 2012 19:44:33 GMT  (strange date!)
  //   01 00 00 00
  //   F3 D9         ==Checksum
  //   7E            ==Trailer

  //Set time to Feb

  //2A 20 63 00 5F 00 B1 00 0B FF B5 01
  //7E 5A 00 24 A3 0B 50 DD 09 00 FF FF FF FF FF FF 01 00
  //7E FF 03 60 65 10 A0 FF FF FF FF FF FF 00 00 78 00 6E 21 96 37 00 00 00 00 00 00 01
  //** 8D 0A 02 00 F0 00 6D 23 00 00 6D 23 00 00 6D 23 00
  //14 02 2B 4F ==Thu, 02 Feb 2012 21:37:24 GMT
  //14 02 2B 4F ==Thu, 02 Feb 2012 21:37:24 GMT
  //14 02 2B 4F  ==Thu, 02 Feb 2012 21:37:24 GMT
  //00 00        ==No time zone/BST not applicable for Feb..
  //00 00
  //AD B1 99 4F  ==Thu, 26 Apr 2012 20:35:57 GMT
  //01 00 00 00
  //F6 87        ==Checksum
  //7E

  //2A 20 63 00 5F 00 B1 00 0B FF B5 01
  //7E 5A 00 24 A3 0B 50 DD 09 00 FF FF FF FF FF FF 01 00
  //7E FF 03 60 65 10 A0 FF FF FF FF FF FF 00 00 78 00 6E 21 96 37 00 00 00 00 00 00 1C
  //** 8D 0A 02 00 F0 00 6D 23 00 00 6D 23 00 00 6D 23 00
  //F5 B3 99 4F
  //F5 B3 99 4F
  //F5 B3 99 4F 01 00 00 00 28 B3 99 4F 01 00 00 00
  //F3 C7 7E

  //2B 20 63 00 5F 00 DD 00 0B FF B5 01
  //7E 5A 00 24 A3 0B 50 DD 09 00 FF FF FF FF FF FF 01 00
  //7E FF 03 60 65 10 A0 FF FF FF FF FF FF 00 00 78 00 6E 21 96 37 00 00 00 00 00 00 08
  //** 80 0A 02 00 F0 00 6D 23 00 00 6D 23 00 00 6D 23 00
  //64 76 99 4F ==Thu, 26 Apr 2012 16:23:00 GMT
  //64 76 99 4F ==Thu, 26 Apr 2012 16:23:00 GMT
  //64 76 99 4F  ==Thu, 26 Apr 2012 16:23:00 GMT
  //58 4D   ==19800 seconds = 5.5 hours
  //00 00
  //62 B5 99 4F
  //01 00 00 00
  //C3 27 7E

  
  time_t currenttime = esp32rtc.getEpoch(); // Returns the ESP32 RTC in number of seconds since the epoch (normally 01/01/1970)
  //digitalClockDisplay(currenttime);
  writePacketHeader(level1packet);
  writeSMANET2PlusPacket(level1packet, 0x09, 0x00, packet_send_counter, 0, 0, 0);
  writeSMANET2ArrayFromProgmem(level1packet, smanet2settime, sizeof(smanet2settime));
  writeSMANET2Long(level1packet, currenttime);
  writeSMANET2Long(level1packet, currenttime);
  writeSMANET2Long(level1packet, currenttime);
  // writeSMANET2Long(level1packet, timeZoneOffset);
  writeSMANET2uint(level1packet, timeZoneOffset);
  writeSMANET2uint(level1packet, 0);
  writeSMANET2Long(level1packet, currenttime); //No idea what this is for...
  writeSMANET2ArrayFromProgmem(level1packet, smanet2packet0x01000000, sizeof(smanet2packet0x01000000));
  writeSMANET2PlusPacketTrailer(level1packet);
  writePacketLength(level1packet);
  sendPacket(level1packet);
  packet_send_counter++;
  //debugMsgln(" done");
}

bool ESP32_SMA_Inverter::getGridFrequency()
{
  logD("getGridFrequency(%i)", innerstate);
  
  switch (innerstate)
  {
  case 0:
    writePacketHeader(level1packet);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xA1, packet_send_counter, 0, 0, 0);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof(smanet2packetx80x00x02x00));
    writeSMANET2ArrayFromProgmem(level1packet, smanet2gridfrequency, sizeof(smanet2gridfrequency));
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);
    innerstate++;
    break;

  case 1:
    if (waitForMultiPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
        innerstate = 0;
    }
    else {
      innerstate = 0;
    }
    break;

  case 2:
    {
      // Extract grid frequency (value type 0x4657)
      bool found_frequency = false;
      for (int i = 40 + 1; i < packetposition - 3; i += 28)
      {
        valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
        memcpy(&value, &level1packet[i + 8], 4);

        logD("Grid Frequency packet: valuetype=0x%04X, value=%ld", valuetype, value);

        if (valuetype == 0x4657) // Grid frequency
        {
          gridfrequency = (float)value / 100.0; // Frequency in Hz
          // Sanity check - grid frequency should be between 45-65 Hz
          if (gridfrequency < 45.0 || gridfrequency > 65.0) {
            logW("Grid frequency out of range: %f Hz (expected 45-65 Hz)", gridfrequency);
            gridfrequency = 50.0; // Default to 50 Hz for European grid
          }
          logI("Grid Freq: %f Hz", gridfrequency);
          _client.publish(MQTT_BASE_TOPIC "grid_frequency", String(gridfrequency), true);
          found_frequency = true;
          break;
        }
      }
      if (!found_frequency) {
        logW("Grid frequency data type 0x4657 not found in packet");
        // Debug: dump all value types found
        logW("Available value types in grid frequency packet:");
        for (int i = 40 + 1; i < packetposition - 3; i += 28) {
          valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
          memcpy(&value, &level1packet[i + 8], 4);
          logW("  0x%04X = %ld", valuetype, value);
        }
      }
      innerstate++;
      break;
    }

  default:
    return true;
  }
  return false;
}

bool ESP32_SMA_Inverter::getGridVoltage()
{
  logD("getGridVoltage(%i)", innerstate);
  
  static unsigned long functionStartTime = 0;
  
  switch (innerstate)
  {
  case 0:
    functionStartTime = millis(); // Start timeout timer
    writePacketHeader(level1packet);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xA1, packet_send_counter, 0, 0, 0);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof(smanet2packetx80x00x02x00));
    writeSMANET2ArrayFromProgmem(level1packet, smanet2gridvoltage, sizeof(smanet2gridvoltage));
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);
    innerstate++;
    break;

  case 1:
    // Check for timeout first
    if (millis() - functionStartTime > 10000) { // 10 second timeout
      logW("getGridVoltage timeout after 10s - skipping");
      functionStartTime = 0;
      return true; // Skip this function and continue
    }
    
    if (waitForMultiPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
        innerstate = 0;
    }
    break;

  case 2:
    {
      // Extract grid voltage (value type 0x4648)
      bool found_voltage = false;
      for (int i = 40 + 1; i < packetposition - 3; i += 28)
      {
        valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
        memcpy(&value, &level1packet[i + 8], 4);

        logD("Grid Voltage packet: valuetype=0x%04X, value=%ld", valuetype, value);

        if (valuetype == 0x4648) // Grid voltage (phase A)
        {
          gridvoltage = (float)value / 100.0; // Voltage in V
          logI("Grid Voltage L1: %f V", gridvoltage);
          _client.publish(MQTT_BASE_TOPIC "grid_voltage", String(gridvoltage), true);
          found_voltage = true;
        } else if (valuetype == 0x4649) {
          float voltage = (float)value / 100.0;
          logI("Grid Voltage L2: %f V", voltage);
          _client.publish(MQTT_BASE_TOPIC "uac2", String(voltage), true);
        } else if (valuetype == 0x464A) {
          float voltage = (float)value / 100.0;
          logI("Grid Voltage L3: %f V", voltage);
          _client.publish(MQTT_BASE_TOPIC "uac3", String(voltage), true);
        }
      }
      if (!found_voltage) {
        logW("Grid voltage data type 0x4648 not found in packet");
        // Debug: dump all value types found
        logW("Available value types in grid voltage packet:");
        for (int i = 40 + 1; i < packetposition - 3; i += 28) {
          valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
          memcpy(&value, &level1packet[i + 8], 4);
          logW("  0x%04X = %ld", valuetype, value);
        }
      }
      innerstate++;
      break;
    }

  default:
    return true;
  }
  return false;
}

bool ESP32_SMA_Inverter::getInverterTemperature()
{
  logD("getInverterTemperature(%i)", innerstate);
  
  switch (innerstate)
  {
  case 0:
    writePacketHeader(level1packet);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xA1, packet_send_counter, 0, 0, 0);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof(smanet2packetx80x00x02x00));
    writeSMANET2ArrayFromProgmem(level1packet, smanet2temperature, sizeof(smanet2temperature));
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);
    innerstate++;
    break;

  case 1:
    if (waitForMultiPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
        innerstate = 0;
    }
    break;

  case 2:
    // Extract inverter temperature (value type 0x2377)
    for (int i = 40 + 1; i < packetposition - 3; i += 28)
    {
      valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
      memcpy(&value, &level1packet[i + 8], 4);
      
      if (valuetype == 0x2377) // Inverter temperature
      {
        invertertemp = (float)value / 100.0; // Temperature in °C
        logI("Inverter Temp: %f °C", invertertemp);
        _client.publish(MQTT_BASE_TOPIC "inverter_temp", String(invertertemp), true);
        break;
      }
    }
    innerstate++;
    break;

  default:
    return true;
  }
  return false;
}

bool ESP32_SMA_Inverter::getMaxPowerToday()
{
  // SMA exposes no LRI for "max Pac today". The previous implementation queried
  // 0x2622 (Daily Yield) and published the Wh value as if it were Watts.
  // The peak is now tracked in software inside getInstantACPower(); this state
  // is kept as a no-op so the main state machine ordering is unchanged.
  return true;
}

// Extended metric functions
bool ESP32_SMA_Inverter::getACVoltage()
{
#ifndef FETCH_EXTENDED_AC_METRICS
  return true;
#else
  logD("getACVoltage(%i)", innerstate);
  
  switch (innerstate)
  {
  case 0:
    writePacketHeader(level1packet);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xA1, packet_send_counter, 0, 0, 0);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof(smanet2packetx80x00x02x00));
    writeSMANET2ArrayFromProgmem(level1packet, smanet2acvoltage, sizeof(smanet2acvoltage));
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);
    innerstate++;
    break;

  case 1:
    if (waitForMultiPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
        innerstate = 0;
    }
    break;

  case 2:
    {
  // Extract AC voltage (value type 0x4648)
      bool found_voltage = false;
      for (int i = 40 + 1; i < packetposition - 3; i += 28)
      {
        valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
        memcpy(&value, &level1packet[i + 8], 4);

  logD("AC Voltage packet: valuetype=0x%04X, value=%ld", valuetype, value);

  if (valuetype == 0x4648) // AC voltage (phase A)
        {
          acvoltage = (float)value / 100.0; // Voltage in V
          logI("AC Voltage L1: %f V", acvoltage);
          _client.publish(MQTT_BASE_TOPIC "ac_voltage", String(acvoltage), true);
          found_voltage = true;
        } else if (valuetype == 0x4649) {
          float voltage = (float)value / 100.0;
          logI("AC Voltage L2: %f V", voltage);
          _client.publish(MQTT_BASE_TOPIC "uac2", String(voltage), true);
        } else if (valuetype == 0x464A) {
          float voltage = (float)value / 100.0;
          logI("AC Voltage L3: %f V", voltage);
          _client.publish(MQTT_BASE_TOPIC "uac3", String(voltage), true);
        }
      }
      if (!found_voltage) {
  logW("AC voltage data type 0x4648 not found in packet");
        // Debug: dump all value types found
        logW("Available value types in AC voltage packet:");
        for (int i = 40 + 1; i < packetposition - 3; i += 28) {
          valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
          memcpy(&value, &level1packet[i + 8], 4);
          logW("  0x%04X = %ld", valuetype, value);
        }
      }
      innerstate++;
      break;
    }

  default:
    return true;
  }
  return false;
#endif
}

bool ESP32_SMA_Inverter::getACCurrent()
{
#ifndef FETCH_EXTENDED_AC_METRICS
  return true;
#else
  logD("getACCurrent(%i)", innerstate);
  
  switch (innerstate)
  {
  case 0:
    writePacketHeader(level1packet);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xA1, packet_send_counter, 0, 0, 0);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof(smanet2packetx80x00x02x00));
    writeSMANET2ArrayFromProgmem(level1packet, smanet2accurrent, sizeof(smanet2accurrent));
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);
    innerstate++;
    break;

  case 1:
    if (waitForMultiPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
        innerstate = 0;
    }
    break;

  case 2:
    {
      // Extract AC current (value type 0x4650)
      bool found_current = false;
      for (int i = 40 + 1; i < packetposition - 3; i += 28)
      {
        valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
        memcpy(&value, &level1packet[i + 8], 4);

        logD("AC Current packet: valuetype=0x%04X, value=%ld", valuetype, value);

        if (valuetype == 0x4650) // AC current
        {
          accurrent = (float)value / 1000.0; // Current in A
          logI("AC Current L1: %f A", accurrent);
          _client.publish(MQTT_BASE_TOPIC "ac_current", String(accurrent), true);
          found_current = true;
        } else if (valuetype == 0x4651) {
          float current = (float)value / 1000.0;
          logI("AC Current L2: %f A", current);
          _client.publish(MQTT_BASE_TOPIC "iac2", String(current), true);
        } else if (valuetype == 0x4652) {
          float current = (float)value / 1000.0;
          logI("AC Current L3: %f A", current);
          _client.publish(MQTT_BASE_TOPIC "iac3", String(current), true);
        }
      }
      if (!found_current) {
        logW("AC current data type 0x4650 not found in packet");
        // Debug: dump all value types found
        logW("Available value types in AC current packet:");
        for (int i = 40 + 1; i < packetposition - 3; i += 28) {
          valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
          memcpy(&value, &level1packet[i + 8], 4);
          logW("  0x%04X = %ld", valuetype, value);
        }
      }
      innerstate++;
      break;
    }

  default:
    return true;
  }
  return false;
#endif
}

bool ESP32_SMA_Inverter::getOperatingTime()
{
#ifndef FETCH_TIME_METRICS
  return true;
#else
  logD("getOperatingTime(%i)", innerstate);
  
  switch (innerstate)
  {
  case 0:
    writePacketHeader(level1packet);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xa0, packet_send_counter, 0, 0, 0);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof(smanet2packetx80x00x02x00));
    writeSMANET2ArrayFromProgmem(level1packet, smanet2operatingtime, sizeof(smanet2operatingtime));
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);
    innerstate++;
    break;

  case 1:
    if (waitForMultiPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
        innerstate = 0;
    }
    break;

  case 2:
    {
      // Extract operating time (value type 0x462E)
      bool found_optime = false;
      for (int i = 40 + 1; i < packetposition - 3; i += 28)
      {
        valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
        memcpy(&value, &level1packet[i + 8], 4);

        logD("Operating Time packet: valuetype=0x%04X, value=%ld", valuetype, value);

        if (valuetype == 0x462E) // Operating time
        {
          operatingtime = value / 3600; // Convert seconds to hours
          logI("Operating Time: %lu hours", operatingtime);
          _client.publish(MQTT_BASE_TOPIC "operating_time", String(operatingtime), true);
          found_optime = true;
          break;
        }
      }
      if (!found_optime) {
        logW("Operating time data type 0x462E not found in packet");
      }
      innerstate++;
      break;
    }

  default:
    return true;
  }
  return false;
#endif
}

bool ESP32_SMA_Inverter::getFeedInTime()
{
#ifndef FETCH_TIME_METRICS
  return true;
#else
  logD("getFeedInTime(%i)", innerstate);
  
  switch (innerstate)
  {
  case 0:
    writePacketHeader(level1packet);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xa0, packet_send_counter, 0, 0, 0);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof(smanet2packetx80x00x02x00));
    writeSMANET2ArrayFromProgmem(level1packet, smanet2feedintime, sizeof(smanet2feedintime));
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);
    innerstate++;
    break;

  case 1:
    if (waitForMultiPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
        innerstate = 0;
    }
    break;

  case 2:
    {
      // Extract feed-in time (value type 0x462F)
      bool found_feedtime = false;
      for (int i = 40 + 1; i < packetposition - 3; i += 28)
      {
        valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
        memcpy(&value, &level1packet[i + 8], 4);

        logD("Feed-in Time packet: valuetype=0x%04X, value=%ld", valuetype, value);

        if (valuetype == 0x462F) // Feed-in time
        {
          feedintime = value / 3600; // Convert seconds to hours
          logI("Feed-in Time: %lu hours", feedintime);
          _client.publish(MQTT_BASE_TOPIC "feedin_time", String(feedintime), true);
          found_feedtime = true;
          break;
        }
      }
      if (!found_feedtime) {
        logW("Feed-in time data type 0x462F not found in packet");
      }
      innerstate++;
      break;
    }

  default:
    return true;
  }
  return false;
#endif
}

bool ESP32_SMA_Inverter::getDeviceStatus()
{
#ifndef FETCH_STATUS_METRICS
  return true;
#else
  logD("getDeviceStatus(%i)", innerstate);
  
  static unsigned long functionStartTime = 0;
  
  switch (innerstate)
  {
  case 0:
    functionStartTime = millis(); // Start timeout timer
    writePacketHeader(level1packet);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xA1, packet_send_counter, 0, 0, 0);
    // smanet2devicestatus already bakes in the {0x80,0x00,0x02,0x80} prefix
    // because its mask byte is 0x80; do NOT prepend smanet2packetx80x00x02x00.
    writeSMANET2ArrayFromProgmem(level1packet, smanet2devicestatus, sizeof(smanet2devicestatus));
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);
    innerstate++;
    break;

  case 1:
    // Check for timeout first
    if (millis() - functionStartTime > 10000) { // 10 second timeout
      logW("getDeviceStatus timeout after 10s - skipping");
      functionStartTime = 0;
      return true; // Skip this function and continue
    }
    
    if (waitForMultiPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
        innerstate = 0;
    }
    break;

  case 2:
    {
      // Status records (OperationHealth 0x2148) are 40 bytes long. Each record's
      // bytes [+8..+36] hold up to 8 candidate status attributes (4 bytes each).
      // The currently-active attribute has its high byte set to 0x01; the low
      // 3 bytes are the status code (307=OK, 455=Warning, 35=Fault, 303=Off).
      // Walk records on a 40-byte stride and decode them with SBFspot's
      // getattribute() rule.
      const int recordsize = 40;
      bool found_status = false;
      for (int i = 40 + 1; i + recordsize <= packetposition - 3; i += recordsize)
      {
        valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
        if (valuetype != 0x2148) continue;

        for (int idx = 8; idx < recordsize; idx += 4)
        {
          uint32_t attribute;
          memcpy(&attribute, &level1packet[i + idx], 4);
          uint32_t tag = attribute & 0x00FFFFFF;
          if (tag == 0x00FFFFFE) break; // end-of-attributes marker
          if ((attribute >> 24) == 0x01) // currently-active flag
          {
            devicestatus = tag;
            String status_text;
            switch (devicestatus) {
              case 35:  status_text = "Fault"; break;
              case 303: status_text = "Off"; break;
              case 307: status_text = "OK"; break;
              case 455: status_text = "Warning"; break;
              default:  status_text = String(devicestatus); break;
            }
            logI("Device Status: %s (%u)", status_text.c_str(), devicestatus);
            _client.publish(MQTT_BASE_TOPIC "device_status", status_text, true);
            _client.publish(MQTT_BASE_TOPIC "device_status_code", String(devicestatus), true);
            found_status = true;
            break;
          }
        }
        if (found_status) break;
      }
      if (!found_status) {
        logW("Device status: no active 0x2148 attribute found (packetlen=%d)", packetposition);
      }
      innerstate++;
      break;
    }

  default:
    return true;
  }
  return false;
#endif
}

// Add closing #endif for functions that need them
// (This will be handled by proper function termination)

// Placeholder functions for remaining metrics - to be implemented similarly
bool ESP32_SMA_Inverter::getPowerFactor() { return true; }
bool ESP32_SMA_Inverter::getReactivePower() { return true; }
bool ESP32_SMA_Inverter::getApparentPower() { return true; }
bool ESP32_SMA_Inverter::getGridErrors() { return true; }


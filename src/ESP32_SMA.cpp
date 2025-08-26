/*
 Talks to SMA Sunny Boy, Model SB 8000US-12
    - Gets AC power (only)
    - Sends the result to a custom LED bargraph

  Based on code found at https://github.com/stuartpittaway/nanodesmapvmonitor
 */

//Need to change SoftwareSerial/NewSoftSerial.h file to set buffer to 128 bytes or you will get buffer overruns!
//Find the line below and change
//#define _NewSS_MAX_RX_BUFF 128 // RX buffer size

#include "Arduino.h"
#include "ESP32_SMA.h"

#include "time.h"
#include "bluetooth.h"
#include "LocalUtil.h"
#include "mainstate.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <Update.h>
#include "site_details.h"

#include "EspMQTTClient.h"

#include <logging.hpp>
#include <ets-appender.hpp>
#include <udp-appender.hpp>
#include "ESP32_SMA_Inverter.h"
#ifdef ARDUINO_ARCH_ESP32
#include "esp_sntp.h"
#endif

// Track if we've aligned ESP32rtc to SNTP time to avoid repeated adjustments
static bool gRtcAligned = false;
static unsigned long gBtEarliestStartMs = 0;


using namespace esp32m;


#ifdef SLEEP_ROUTINE
ESP32Time wakeupTime;
ESP32Time bedTime;
#endif
//#define WAKEUP_TIME 22,30 
//#define BED_TIME 05,45

#ifdef SYSLOG_HOST
#ifdef SYSLOG_PORT
  UDPAppender udpappender(SYSLOG_HOST, SYSLOG_PORT);
#else
  UDPAppender udpappender(SYSLOG_HOST, 514);
#endif
#endif


//BST Start and end dates - this needs moving into some sort of PROGMEM array for the years or calculated based on the BST logic see
//http://www.time.org.uk/bstgmtcodepage1.aspx
//static time_t SummerStart=1435888800;  //Sunday, 31 March 02:00:00 GMT
//static time_t SummerEnd=1425952800;  //Sunday, 27 October 02:00:00 GMT


size_t xArraySize(void);


void ESP32_SMA::blinkLed()
{
  digitalWrite(LED_BUILTIN, blinklaststate);
  blinklaststate = !blinklaststate;
}

void ESP32_SMA::blinkLedOff()
{
  if (blinklaststate)
  {
    blinkLed();
  }
}


ESP32_SMA esp32sma ;

void onConnectionEstablished() {
  esp32sma.onConnectionEstablished();
}

void ESP32_SMA::onConnectionEstablished()
{
  mqttclient.publish(MQTT_BASE_TOPIC "LWT", "online", true);
  logW("WiFi and MQTT connected");
  logW("v1 Build 2w d (%s) t (%s) "  ,__DATE__ , __TIME__) ;

#ifdef PUBLISH_HASS_TOPICS
  // client.publish(MQTT_BASE_TOPIC "LWT", "online", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/signal/config", "{\"name\": \"" FRIENDLY_NAME " Signal Strength\", \"state_topic\": \"" MQTT_BASE_TOPIC "signal\", \"unique_id\": \"" HOST "-signal\", \"unit_of_measurement\": \"dB\", \"device\": {\"identifiers\": [\"" HOST "-device\"], \"name\": \"" FRIENDLY_NAME "\"}}", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/generation_today/config", "{\"name\": \"" FRIENDLY_NAME " Power Generation Today\", \"device_class\": \"energy\", \"state_topic\": \"" MQTT_BASE_TOPIC "generation_today\", \"unique_id\": \"" HOST "-generation_today\", \"unit_of_measurement\": \"Wh\", \"state_class\": \"total_increasing\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/generation_total/config", "{\"name\": \"" FRIENDLY_NAME " Power Generation Total\", \"device_class\": \"energy\", \"state_topic\": \"" MQTT_BASE_TOPIC "generation_total\", \"unique_id\": \"" HOST "-generation_total\", \"unit_of_measurement\": \"Wh\", \"state_class\": \"total_increasing\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/instant_ac/config", "{\"name\": \"" FRIENDLY_NAME " Instantinous AC Power\", \"device_class\": \"energy\", \"state_topic\": \"" MQTT_BASE_TOPIC "instant_ac\", \"unique_id\": \"" HOST "-instant_ac\", \"unit_of_measurement\": \"W\", \"state_class\": \"measurement\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/instant_dc/config", "{\"name\": \"" FRIENDLY_NAME " Instantinous DC Power\", \"device_class\": \"energy\", \"state_topic\": \"" MQTT_BASE_TOPIC "instant_dc\", \"unique_id\": \"" HOST "-instant_dc\", \"unit_of_measurement\": \"W\", \"state_class\": \"measurement\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/instant_vdc/config", "{\"name\": \"" FRIENDLY_NAME " Instantinous DC Voltage\", \"device_class\": \"energy\", \"state_topic\": \"" MQTT_BASE_TOPIC "instant_vdc\", \"unique_id\": \"" HOST "-instant_vdc\", \"unit_of_measurement\": \"V\", \"state_class\": \"measurement\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/instant_adc/config", "{\"name\": \"" FRIENDLY_NAME " Instantinous DC Ampere\", \"device_class\": \"energy\", \"state_topic\": \"" MQTT_BASE_TOPIC "instant_adc\", \"unique_id\": \"" HOST "-instant_adc\", \"unit_of_measurement\": \"A\", \"state_class\": \"measurement\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);
  // Per-string metrics if available
  mqttclient.publish("homeassistant/sensor/" HOST "/instant_vdc1/config", "{\"name\": \"" FRIENDLY_NAME " DC Voltage 1\", \"device_class\": \"voltage\", \"state_topic\": \"" MQTT_BASE_TOPIC "instant_vdc1\", \"unique_id\": \"" HOST "-instant_vdc1\", \"unit_of_measurement\": \"V\", \"state_class\": \"measurement\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/instant_vdc2/config", "{\"name\": \"" FRIENDLY_NAME " DC Voltage 2\", \"device_class\": \"voltage\", \"state_topic\": \"" MQTT_BASE_TOPIC "instant_vdc2\", \"unique_id\": \"" HOST "-instant_vdc2\", \"unit_of_measurement\": \"V\", \"state_class\": \"measurement\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/instant_adc1/config", "{\"name\": \"" FRIENDLY_NAME " DC Current 1\", \"device_class\": \"current\", \"state_topic\": \"" MQTT_BASE_TOPIC "instant_adc1\", \"unique_id\": \"" HOST "-instant_adc1\", \"unit_of_measurement\": \"A\", \"state_class\": \"measurement\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/instant_adc2/config", "{\"name\": \"" FRIENDLY_NAME " DC Current 2\", \"device_class\": \"current\", \"state_topic\": \"" MQTT_BASE_TOPIC "instant_adc2\", \"unique_id\": \"" HOST "-instant_adc2\", \"unit_of_measurement\": \"A\", \"state_class\": \"measurement\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/instant_pdc1/config", "{\"name\": \"" FRIENDLY_NAME " DC Power 1\", \"device_class\": \"power\", \"state_topic\": \"" MQTT_BASE_TOPIC "instant_pdc1\", \"unique_id\": \"" HOST "-instant_pdc1\", \"unit_of_measurement\": \"W\", \"state_class\": \"measurement\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/instant_pdc2/config", "{\"name\": \"" FRIENDLY_NAME " DC Power 2\", \"device_class\": \"power\", \"state_topic\": \"" MQTT_BASE_TOPIC "instant_pdc2\", \"unique_id\": \"" HOST "-instant_pdc2\", \"unit_of_measurement\": \"W\", \"state_class\": \"measurement\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/grid_frequency/config", "{\"name\": \"" FRIENDLY_NAME " Grid Frequency\", \"device_class\": \"frequency\", \"state_topic\": \"" MQTT_BASE_TOPIC "grid_frequency\", \"unique_id\": \"" HOST "-grid_frequency\", \"unit_of_measurement\": \"Hz\", \"state_class\": \"measurement\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/grid_voltage/config", "{\"name\": \"" FRIENDLY_NAME " Grid Voltage\", \"device_class\": \"voltage\", \"state_topic\": \"" MQTT_BASE_TOPIC "grid_voltage\", \"unique_id\": \"" HOST "-grid_voltage\", \"unit_of_measurement\": \"V\", \"state_class\": \"measurement\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/inverter_temp/config", "{\"name\": \"" FRIENDLY_NAME " Inverter Temperature\", \"device_class\": \"temperature\", \"state_topic\": \"" MQTT_BASE_TOPIC "inverter_temp\", \"unique_id\": \"" HOST "-inverter_temp\", \"unit_of_measurement\": \"°C\", \"state_class\": \"measurement\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);
  mqttclient.publish("homeassistant/sensor/" HOST "/max_power_today/config", "{\"name\": \"" FRIENDLY_NAME " Max Power Today\", \"device_class\": \"power\", \"state_topic\": \"" MQTT_BASE_TOPIC "max_power_today\", \"unique_id\": \"" HOST "-max_power_today\", \"unit_of_measurement\": \"W\", \"state_class\": \"measurement\", \"device\": {\"identifiers\": [\"" HOST "-device\"]} }", true);

#endif // PUBLISH_HASS_TOPICS
}

void setup() {
  esp32sma.setup();
}

void ESP32_SMA::setup()
{
  Serial.begin(115200);                      //Serial port for debugging output

  Logging::setLevel(esp32m::Info);
  Logging::addAppender(&ETSAppender::instance());
#ifdef SYSLOG_HOST
  udpappender.setMode(UDPAppender::Format::Syslog);
  Logging::addAppender(&udpappender);
#endif
  Serial.println("added appenders");

  // Start WiFi first with modem sleep enabled (required for WiFi+BT), then start Classic BT

  // Ensure WiFi is in station mode and has a friendly hostname before connecting
  WiFi.mode(WIFI_STA);
#ifdef HOST
  WiFi.setHostname(HOST);
#endif
  // When WiFi and Classic BT are both enabled, WiFi modem sleep MUST be enabled
  // to satisfy coexistence requirements in ESP-IDF.
  WiFi.setSleep(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  {
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
      delay(100);
    }
  }

  // Get the MAC address in reverse order (not sure why, but do it here to make setup easier)
  unsigned char smaSetAddress[6] = {SMA_ADDRESS};
  for (int i = 0; i < 6; i++)
    smaBTInverterAddressArray[i] = smaSetAddress[5 - i];

  pinMode(LED_BUILTIN, OUTPUT);

  ESP32rtc.setTime(30, 24, 15, 17, 1, 2021); // 17th Jan 2021 15:24:30  // Need this to be accurate. Since connecting to the internet anyway, use NTP.

  mqttclient.setMaxPacketSize(512); // must be big enough to send home assistant config
  mqttclient.enableMQTTPersistence();
  mqttclient.enableDebuggingMessages();                                     // Enable debugging messages sent to serial output
  mqttclient.enableHTTPWebUpdater("/update");                               // Enable the web updater. User and password default to values of MQTTUsername and MQTTPassword. These can be overrited with enableHTTPWebUpdater("user", "password").
  mqttclient.enableLastWillMessage(MQTT_BASE_TOPIC "LWT", "offline", true); // You can activate the retain flag by setting the third parameter to true

  logW("Connecting to %s" , WIFI_SSID);
  {
    String ipStr = WiFi.localIP().toString();
    logI("Current IP (if any): %s" , ipStr.c_str());
  }

  // Always set time to GMT timezone
#ifdef ARDUINO_ARCH_ESP32
  // Log DNS for NTP server to spot resolution issues
  {
    IPAddress ip;
    if (WiFi.hostByName(NTP_SERVER, ip)) {
      String sip = ip.toString();
      logI("NTP DNS %s -> %s", NTP_SERVER, sip.c_str());
    } else {
      logW("NTP DNS failed for %s", NTP_SERVER);
    }
  }
  // Notify when SNTP actually synchronizes
  sntp_set_time_sync_notification_cb([](struct timeval *tv){
    time_t now = time(nullptr);
    Serial.printf("SNTP sync event, now=%lu\n", (unsigned long)now);
  });
#endif
  configTime(timeZoneOffset, 0, NTP_SERVER, NTP_SERVER2, NTP_SERVER3);
  if (WiFi.status() == WL_CONNECTED) {
    String ipStr = WiFi.localIP().toString();
    logW("WiFi connected: %s -> %s" , WIFI_SSID, ipStr.c_str());
  } else {
    logW("WiFi not connected yet; MQTT/UDP will start once connected");
  }
  
  Logging::hookUartLogger();
//  setupOTAServer();


  logD("hello world!");

  // Initialize scheduled timers to avoid long initial gaps
  nextSecond = millis() + 1000;
  next5Minute = millis() + 30000; // first 5-min tick after 30s
  nextHour = millis() + 5 * 60 * 1000; // first hour tick after 5 min
  nextDay = millis() + 60 * 60 * 1000; // first day tick after 1 hour
}

void ESP32_SMA::everySecond()
{
  blinkLedOff();
  static uint32_t hb = 0;
  hb++;
  if ((hb % 5) == 0) {
    // Debug heartbeat every 5s
    logI("heartbeat t=%lu rssi=%d", ESP32rtc.getEpoch(), WiFi.RSSI());
    if (mqttclient.isConnected()) {
      time_t now = ESP32rtc.getEpoch();
      struct tm tm_utc;
      gmtime_r(&now, &tm_utc);
      char ts[25];
      strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
      mqttclient.publish(MQTT_BASE_TOPIC "esp_local_utc_time", ts, false);
      mqttclient.publish(MQTT_BASE_TOPIC "heartbeat", "1", false);
    }
  }
}

void ESP32_SMA::every5Minutes()
{
  log_d("every5Minutes()");
  if (mqttclient.isConnected())
  {
    mqttclient.publish(MQTT_BASE_TOPIC "signal", String(WiFi.RSSI()));
  }
}

void ESP32_SMA::everyHour()
{

}

void ESP32_SMA::everyDay()
{

}


void ESP32_SMA::dodelay(unsigned long duration)
{
  sleepuntil = millis() + duration;
}

//uint8_t mainstate = MAINSTATE_INIT;
MainState mainstate = MAINSTATE_INIT;

static long lastRanTime = 0;
static long nowTime = 0;

void loop() {
  esp32sma.loop();
}


void ESP32_SMA::loop()
{
  struct tm timeinfo;
  mqttclient.loop();
  // server.handleClient();
  // If WiFi isn't up yet, don't start/hammer Bluetooth. Give WiFi/MQTT time to connect.
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastLog = 0;
    if (millis() - lastLog > 3000) {
      lastLog = millis();
      logI("Waiting for WiFi... status=%d", (int)WiFi.status());
    }
    dodelay(500);
    return;
  }
  
  if (millis() >= nextSecond)
  {
    nextSecond = millis() + 1000;
    everySecond();
  }

  if (millis() >= next5Minute)
  {
    next5Minute = millis() + 1000 * 60 * 5;
    every5Minutes();
  }

  if (millis() >= nextHour)
  {
    nextHour = millis() + 1000 * 3600;
    everyHour();
  }

  if (millis() >= nextDay)
  {
    nextDay = millis() + 1000 * 3600 * 24 ;
    everyDay();
  }

  // "delay" the main BT loop
  if (millis() < sleepuntil)
    return;

  // if in the main loop, only run at the top of the minute
  // if (mainstate >= MAINSTATE_GET_INSTANT_AC_POWER)
  // {
  //   getLocalTime(&timeinfo);
  //   if (timeinfo.tm_min == thisminute)
  //     return;
  // }

  // If SNTP has set the system clock, align ESP32rtc once
  {
    time_t now = time(nullptr);
    if (!gRtcAligned && now > 1609459200) { // > 2021-01-01
      ESP32rtc.setTime(now);
      gRtcAligned = true;
      logI("RTC aligned to SNTP: %lu", (unsigned long)now);
      // Allow a short grace period before starting Classic BT to avoid coex timing issues
      gBtEarliestStartMs = millis() + 3000;
    }
  }

  // Wait for initial NTP sync before setting up inverter
  // Prefer the SNTP sync status when available; fall back to a time threshold heuristic.
  {
    time_t now = time(nullptr);
#ifdef ARDUINO_ARCH_ESP32
    bool synced = (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) || (now > 1640995200);
#else
    bool synced = (now > 1640995200);
#endif
    if (mainstate == MAINSTATE_INIT && !synced)
  {
    // Avoid re-calling configTime in loop; SNTP is already configured in setup().
    log_d("NTP not yet sync'd, sleeping");
    log_w("NTP not yet sync'd, sleeping 10s");
    dodelay(10000);
    return;
  }
  }

  // Keep BT work within the state machine; avoid extra pre-connection loop that could starve WiFi
  blinkLed();

  // Guard: defer first BT start slightly after SNTP sync to reduce coex failures
  if (mainstate == MAINSTATE_INIT && gBtEarliestStartMs != 0 && millis() < gBtEarliestStartMs) {
    dodelay(1000);
    return;
  }

  log_d("Loop mainstate: %s", mainstate.toString().c_str());

  switch (mainstate)
  {
  case MAINSTATE_INIT:
    if (BTStart())
    {
      mainstate++;
      smaInverter.resetInnerstate();
    }
    else
    {
      // Backoff more before retrying BT bring-up to avoid coex issues
      dodelay(2000);
    }
    break;

  case MAINSTATE_INIT_SMA_CONNECTION:
    if (smaInverter.initialiseSMAConnection())
    {
      mainstate++;
      smaInverter.resetInnerstate();
    }
    break;

  case MAINSTATE_LOGON_SMA_INVERTER:
    if (smaInverter.logonSMAInverter())
    {
      mainstate++;
      smaInverter.resetInnerstate();
    }
    break;

  case MAINSTATE_GET_DAILY_YIELD_3 : //3
    // Doing this to set datetime
    if (smaInverter.getDailyYield())
    {
      mainstate++;
      smaInverter.resetInnerstate();
    }
    break;

  case MAINSTATE_CHECK_SET_INVERTER_TIME: // 4:
    if (smaInverter.checkIfNeedToSetInverterTime())
    {
      mainstate++;
      smaInverter.resetInnerstate();
    }
    break;

    // --------- regular loop -----------------

  case MAINSTATE_GET_INSTANT_AC_POWER:// 5:
    if (smaInverter.getInstantACPower())
    {
      mainstate++;
      smaInverter.resetInnerstate();
    } 
    break;

  case MAINSTATE_GET_INSTANT_DC_POWER: //6:
    if (smaInverter.getInstantDCPower())
    {
      mainstate++;
      smaInverter.resetInnerstate();
    } 
    break;

  case MAINSTATE_GET_DAILY_YIELD_7:// 7:
    if (smaInverter.getDailyYield())
    {
      mainstate++;
      smaInverter.resetInnerstate();
    }
    break;

  case MAINSTATE_TOTAL_POWER_GENERATION: //8:
    if (smaInverter.getTotalPowerGeneration())
    {
      mainstate++;
      smaInverter.resetInnerstate();
    }
    break;

  case MAINSTATE_GET_GRID_FREQUENCY: //9:
    if (smaInverter.getGridFrequency())
    {
      mainstate++;
      smaInverter.resetInnerstate();
    }
    break;

  case MAINSTATE_GET_GRID_VOLTAGE: //10:
    if (smaInverter.getGridVoltage())
    {
      mainstate++;
      smaInverter.resetInnerstate();
    }
    break;

  case MAINSTATE_GET_INVERTER_TEMPERATURE: //11:
    if (smaInverter.getInverterTemperature())
    {
      mainstate++;
      smaInverter.resetInnerstate();
    }
    break;

  case MAINSTATE_GET_MAX_POWER_TODAY: //12:
    if (smaInverter.getMaxPowerToday())
    {
      mainstate++;
      smaInverter.resetInnerstate();
    }
    break;

  default:
    mainstate = MAINSTATE_GET_INSTANT_AC_POWER;
    smaInverter.resetInnerstate();
  dodelay(METRIC_UPDATE_MS); // cadence between metric cycles
  }
  return;

  //getInverterName();
  //HistoricData();
  lastRanTime = millis() - 4000;
  log_i("While...");

  while (1)
  {

    //HowMuchMemory();

    //Populate datetime and spotpowerac variables with latest values
    //getInstantDCPower();
    nowTime = millis();
    if (nowTime > (lastRanTime + 4000))
    {
      lastRanTime = nowTime;
      Serial.print("^");
      smaInverter.getInstantACPower();
    }

    //digitalClockDisplay(now());

    //The inverter always runs in UTC time (and so does this program!), and ignores summer time, so fix that here...
    //add 1 hour to readings if its summer
    // DRH Temp removal of: if ((datetime>=SummerStart) && (datetime<=SummerEnd)) datetime+=60*60;

#ifdef SLEEP_ROUTINE

 //wakeupTime;
 //bedTime;
#endif



#ifdef allowsleep
    if ((ESP32rtc.getEpoch() > (datetime + 3600)) && (spotpowerac == 0))
    {
      //Inverter output hasnt changed for an hour, so put Nanode to sleep till the morning
      //debugMsgln("Bed time");

      //sleeping=true;

      //Get midnight on the day of last solar generation
      // First, create a time structure to hold the answer to "At what time (in time_t seconds) is the upcoming midnight?"
      tmElements_t tm;
      tm.Year = year(datetime) - 1970;
      tm.Month = month(datetime);
      tm.Day = day(datetime);
      tm.Hour = 23;
      tm.Hour = hour(datetime);
      tm.Minute = 59;
      tm.Second = 59;
      time_t midnight = makeTime(tm);

      //Move to midnight
      //debugMsg("Midnight ");digitalClockDisplay( midnight );debugMsgln("");

      if (ESP32rtc.getEpoch() < midnight)
      {
        //Time to calculate SLEEP time, we only do this if its BEFORE midnight
        //on the day of solar generation, otherwise we might wake up and go back to sleep immediately!

        //Workout what time sunrise is and sleep till then...
        //longitude, latitude (london uk=-0.126236,51.500152)
        unsigned int minutespastmidnight = ComputeSun(mylongitude, mylatitude, datetime, true);

        //We want to wake up at least 15 minutes before sunrise, just in case...
        checktime = midnight + minutespastmidnight - 15 * 60;
      }
    }
#endif

    //digitalClockDisplay( checktime );

    //Delay for approx. 4 seconds between instant AC power readings

  } // end of while(1)
} // end of loop()

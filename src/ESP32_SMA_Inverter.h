#ifndef ESP32_SMA_H
#define ESP32_SMA_H


//missing builtin led 
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif


//SMA inverter timezone (note inverter appears ignores summer time saving internally)
//Need to determine what happens when its a NEGATIVE time zone !
//Number of seconds for timezone
//    0=UTC (London)
//19800=5.5hours Chennai, Kolkata
//36000=Brisbane (UTC+10hrs)
#define timeZoneOffset (long)(60 * 60 * TIME_ZONE)


// max spot dc watt value, ignore if higher
#define MAX_SPOTDC 2000000000


class ESP32_SMA {
    public:

    private:
};

#endif

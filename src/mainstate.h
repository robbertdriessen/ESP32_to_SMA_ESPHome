#ifndef MAINSTATE_H
#define MAINSTATE_H

#include "Arduino.h"

enum MainStateEnum : unsigned int {
    MAINSTATE_INIT = 0,
    MAINSTATE_INIT_SMA_CONNECTION = 1,
    MAINSTATE_LOGON_SMA_INVERTER = 2,
    MAINSTATE_GET_DAILY_YIELD_3 = 3,
    MAINSTATE_CHECK_SET_INVERTER_TIME = 4,
    MAINSTATE_GET_INSTANT_AC_POWER = 5,
    MAINSTATE_GET_INSTANT_DC_POWER = 6,
    MAINSTATE_GET_DAILY_YIELD_7 = 7,
    MAINSTATE_TOTAL_POWER_GENERATION = 8,
    MAINSTATE_HIGH_LOOP = 9
};

class MainState  
{
    public: 
        MainState(const MainStateEnum& v) : value(v) {}
        MainState& operator=(const MainStateEnum& v);
        String toString() const ;
        MainState& operator++();//prefix
        MainState& operator++(int v);//postfix
        bool operator==(const MainStateEnum& v) const { return value == v; }
        bool operator>=(const MainStateEnum& v) const { return value >= v; }
        bool operator<=(const MainStateEnum& v) const { return value <= v; }
        bool operator!=(const MainStateEnum& v) const { return value != v; }
        operator MainStateEnum() const { return value;}
//        operator unsigned int() const { return (unsigned int)value;}

    private:
        MainStateEnum value;
        MainState() {}
    
};


#endif
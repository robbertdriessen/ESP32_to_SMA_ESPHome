#include "mainstate.h"

MainState& MainState::operator=(const MainStateEnum& v)  {
    value = v;
    return *this;
}

MainState& MainState::operator++() {  // prefix
    unsigned int v = (unsigned int)value;
    v++;
    value = (MainStateEnum)v;
    return *this; 
}

MainState& MainState::operator++(int intValue) { //postfix
    unsigned int v = (unsigned int)value;
    v++;
    value = (MainStateEnum)v;
    return *this; 
}

String MainState::toString() const {
        switch (value) {
            case MAINSTATE_INIT :
                return "Start";
            break;
            case MAINSTATE_INIT_SMA_CONNECTION :
                return "InitSmaconnection";
            break;
            case MAINSTATE_LOGON_SMA_INVERTER :
                return "LogonSmaInverter";
            break;
            case MAINSTATE_GET_DAILY_YIELD_3 :
                return "DailyYield-3";
            break;
            case MAINSTATE_CHECK_SET_INVERTER_TIME :
                return "CheckSetInverterTime";
            break;
            case MAINSTATE_GET_INSTANT_AC_POWER :
                return "GetInstantACPower";
            break;
            case MAINSTATE_GET_INSTANT_DC_POWER :
                return "GetInstantDCPower";
            break;
            case MAINSTATE_GET_DAILY_YIELD_7 :
                return "DailyYield-7";
            break;
            case MAINSTATE_TOTAL_POWER_GENERATION :
                return "TotalPowerGeneration";
            break;
            case MAINSTATE_GET_GRID_FREQUENCY :
                return "GetGridFrequency";
            break;
            case MAINSTATE_GET_GRID_VOLTAGE :
                return "GetGridVoltage";
            break;
            case MAINSTATE_GET_INVERTER_TEMPERATURE :
                return "GetInverterTemperature";
            break;
            case MAINSTATE_GET_MAX_POWER_TODAY :
                return "GetMaxPowerToday";
            break;
            case MAINSTATE_HIGH_LOOP :
                return "Highloop";
            break;
            

            default :
                return "Unknown:"+value;
        }
}

#pragma once

#include <lwip/sockets.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "logging.hpp"

namespace esp32m
{

    class UDPAppenderEspMQTT : public LogAppender
    {
    public:
        enum Format
        {
            Text,
            Syslog
        };
        UDPAppenderEspMQTT(const char *ipaddr=nullptr, uint16_t port = 514);
        UDPAppenderEspMQTT(const UDPAppenderEspMQTT &) = delete;
        ~UDPAppenderEspMQTT();
        Format format() { return _format; }
        void setMode(Format format) { _format = format; }

    protected:
        virtual bool append(const LogMessage *message);

    private:
        Format _format;

        struct sockaddr_in _addr;
        int _fd;
    };

} // namespace esp32m
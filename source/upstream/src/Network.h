#pragma once

#include <ostream>
#include <string>
#include <logger/LoggerInterface.h>

namespace intuibase {
void getWiFiNetworks(std::ostream &ss);
void getWiFiSSID(std::ostream &ss, ib::logger::LoggerInterface *log);

} // namespace intuibase

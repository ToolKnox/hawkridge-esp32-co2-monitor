#pragma once

#include <logger/LoggerInterface.h>
#include <mqtt/MqttConfig.h>
#include <string>
#include <array>
#include <cJSON.h>

#include <optional>

#define VALVE_COUNT 8

namespace json {

std::string getString(cJSON *root, const char *name);
int getInt(cJSON *root, const char *name);
bool getBool(cJSON *root, const char *name);
float getFloat(cJSON *root, const char *name);

template <typename T>
std::optional<T> getOptInt(cJSON *root, const char *name) {
	auto obj = cJSON_GetObjectItem(root, name);
	if (!obj || obj->type != cJSON_Number) {
		return {};
	}
	return {obj->valueint};
}

}

namespace config {

using valves_t = std::array<uint8_t, VALVE_COUNT>;

valves_t getValvePins();

struct APConfig {
	std::string ssid = "IntuiBaseAirSensor";
	std::string password = "IntuiBase";
	uint8_t channel = 10;
	std::string hostname = "air-sensor";
	std::string ip = "10.10.10.1";
	std::string gateway = "10.10.10.1";
	std::string subnetMask = "255.255.255.0";
	uint32_t listenPort = 80;
	uint32_t ntpUtcOffset;
	uint32_t ntpDaylightUtcOffset;
	std::string timeZone;
};

struct NetworkConfig {
	std::string hostname;
	uint32_t listenPort;
	bool ntpEnabled = false;
	std::string ntpHost;
	uint32_t ntpUtcOffset;
	uint32_t ntpDaylightUtcOffset;
	std::string timeZone;
	bool loggerEnabled = false;
	std::string loggerHost;
	uint32_t loggerPort;
	uint32_t loggerTimeoutMs;
};

struct WiFiConfig {
	std::string ssid;
	std::string password;
};

struct CarbonDioxideSensorConfig {
	int8_t sda_pin = 21;
	int8_t scl_pin = 22;
	int8_t buzzer_pin = -1;
};

CarbonDioxideSensorConfig getCarbonDioxideSensorConfig(ib::logger::LoggerInterface *log);

struct DisplayConfig {
	bool software_spi = false;
	int8_t mosi_pin = 23; //sda
	int8_t sck_pin = 18; //scl
	int8_t miso_pin = 5; //not used
	int8_t cs_pin = 5;
	int8_t dc_pin = 2;
	int8_t reset_pin = -1;
};

DisplayConfig getDisplayConfig(ib::logger::LoggerInterface *log);

WiFiConfig getWiFiConfig(ib::logger::LoggerInterface *log);
APConfig getAPConfig(ib::logger::LoggerInterface *log);
NetworkConfig getNetworkConfig(ib::logger::LoggerInterface *log);
ib::mqtt::MqttConfig getMqttConfig(ib::logger::LoggerInterface *log);

void setDebugOptionsFromJson(ib::logger::LoggerInterface *log, const char *json);
void readDebugOptions(ib::logger::LoggerInterface *log);


}

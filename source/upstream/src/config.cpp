
#include "config.h"
#include <logger/LoggerInterface.h>

#include <SPIFFS.h>
#include <array>

namespace json {
std::string getString(cJSON *root, const char *name) {
	auto obj = cJSON_GetObjectItem(root, name);
	if (!obj || obj->type != cJSON_String) {
		return {};
	}
	return obj->valuestring;
}

int getInt(cJSON *root, const char *name) {
	auto obj = cJSON_GetObjectItem(root, name);
	if (!obj || obj->type != cJSON_Number) {
		return {};
	}
	return obj->valueint;
}

int getInt(cJSON *root, const char *name, int defaultValue) {
	auto obj = cJSON_GetObjectItem(root, name);
	if (!obj || obj->type != cJSON_Number) {
		return defaultValue;
	}
	return obj->valueint;
}

bool getBool(cJSON *root, const char *name) {
	auto obj = cJSON_GetObjectItem(root, name);
	if (!obj || obj->type != cJSON_False && obj->type != cJSON_True) {
		return false;
	}
	return obj->type == cJSON_True;
}

std::optional<bool> getBoolOptional(cJSON *root, const char *name) {
	auto obj = cJSON_GetObjectItem(root, name);
	if (!obj || obj->type != cJSON_False && obj->type != cJSON_True) {
		return {};
	}
	return obj->type == cJSON_True;
}

float getFloat(cJSON *root, const char *name) {
	auto obj = cJSON_GetObjectItem(root, name);
	if (!obj || obj->type != cJSON_Number) {
		return {};
	}
	return obj->valuedouble;
}

}

namespace config {

DisplayConfig getDisplayConfig(ib::logger::LoggerInterface *log) {
	File file = SPIFFS.open("/cfg/cfgpins.json", FILE_READ);
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(file.readString().c_str()), &cJSON_Delete);
	file.close();
	if (!root) {
		DBGLOGD(log, "Error parsing json\n");
		return {};
	}

	DisplayConfig cfg;
	cfg.software_spi = json::getBool(root.get(), "software_spi");
	cfg.sck_pin = json::getInt(root.get(), "sck_pin", DisplayConfig().sck_pin);
	cfg.mosi_pin = json::getInt(root.get(), "mosi_pin", DisplayConfig().mosi_pin);
	cfg.miso_pin = json::getInt(root.get(), "miso_pin", DisplayConfig().miso_pin);
	cfg.reset_pin = json::getInt(root.get(), "reset_pin", DisplayConfig().reset_pin);
	cfg.dc_pin = json::getInt(root.get(), "dc_pin", DisplayConfig().dc_pin);
	cfg.cs_pin = json::getInt(root.get(), "cs_pin", DisplayConfig().cs_pin);

	return cfg;
}

CarbonDioxideSensorConfig getCarbonDioxideSensorConfig(ib::logger::LoggerInterface *log) {
	File file = SPIFFS.open("/cfg/cfgpins.json", FILE_READ);
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(file.readString().c_str()), &cJSON_Delete);
	file.close();
	if (!root) {
		DBGLOGD(log, "Error parsing cfgpins json\n");
		return {};
	}

	CarbonDioxideSensorConfig cfg;
	cfg.sda_pin = json::getInt(root.get(), "sda_pin");
	cfg.scl_pin = json::getInt(root.get(), "scl_pin");
	cfg.buzzer_pin = json::getInt(root.get(), "buzzer_pin", -1);
	return cfg;
}

APConfig getAPConfig(ib::logger::LoggerInterface *log) {
	File file = SPIFFS.open("/cfg/cfgap.json", FILE_READ);
	if (!file) {
		return {};
	}

	auto cfg = file.readString();
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> network(cJSON_Parse(cfg.c_str()), &cJSON_Delete);
	file.close();

	if (!network || network->type != cJSON_Object) {
		DBGLOGD(log, "Error parsing AP config\n'%s'\n", cfg.c_str());
		return {};
	}

	APConfig config;
	config.ssid = json::getString(network.get(), "ssid");
	config.password = json::getString(network.get(), "password");
	config.channel = json::getInt(network.get(), "channel");
	config.hostname = json::getString(network.get(), "hostname");
	config.ip = json::getString(network.get(), "ip");
	config.gateway = json::getString(network.get(), "gateway");
	config.subnetMask = json::getString(network.get(), "subnetMask");
	config.listenPort = json::getInt(network.get(), "listenPort");
	config.ntpUtcOffset = json::getInt(network.get(), "ntpUtcOffset");
	config.ntpDaylightUtcOffset = json::getInt(network.get(), "ntpDaylightUtcOffset");
	config.timeZone = json::getString(network.get(), "timeZone");

	return config;
}

WiFiConfig getWiFiConfig(ib::logger::LoggerInterface *log) {
	File file = SPIFFS.open("/cfg/cfgwifi.json", FILE_READ);
	if (!file) {
		return {};
	}
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> network(cJSON_Parse(file.readString().c_str()), &cJSON_Delete);
	file.close();

	if (!network || network->type != cJSON_Object) {
		DBGLOGD(log, "Error parsing wifi config\n");
		return {};
	}

	WiFiConfig config;
	config.ssid = json::getString(network.get(), "ssid");
	config.password = json::getString(network.get(), "password");
	return config;
}

NetworkConfig getNetworkConfig(ib::logger::LoggerInterface *log) {
	File file = SPIFFS.open("/cfg/cfgnetwork.json", FILE_READ);
	if (!file) {
		return {};
	}

	String cfg = file.readString();
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> network(cJSON_Parse(cfg.c_str()), &cJSON_Delete);
	file.close();

	if (!network || network->type != cJSON_Object) {
		DBGLOGD(log, "Error parsing network config\n'%s'\n", cfg.c_str());
		return {};
	}

//TODO getStringOptional and defaults
	NetworkConfig config;
	config.hostname = json::getString(network.get(), "hostname");
	config.listenPort = json::getInt(network.get(), "listenPort");

	config.ntpEnabled = json::getBool(network.get(), "ntpEnabled");
	config.ntpHost = json::getString(network.get(), "ntpHost");
	config.ntpUtcOffset = json::getInt(network.get(), "ntpUtcOffset");
	config.ntpDaylightUtcOffset = json::getInt(network.get(), "ntpDaylightUtcOffset");
	config.timeZone = json::getString(network.get(), "timeZone");

	config.loggerEnabled = json::getBool(network.get(), "loggerEnabled");
	config.loggerHost = json::getString(network.get(), "loggerHost");
	config.loggerPort = json::getInt(network.get(), "loggerPort");
	config.loggerTimeoutMs = json::getInt(network.get(), "loggerTimeoutMs");

	return config;
}

ib::mqtt::MqttConfig getMqttConfig(ib::logger::LoggerInterface *log) {
	File file = SPIFFS.open("/cfg/cfgnetwork.json", FILE_READ);
	if (!file) {
		return {};
	}

	String cfg = file.readString();
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> network(cJSON_Parse(cfg.c_str()), &cJSON_Delete);
	file.close();

	if (!network || network->type != cJSON_Object) {
		DBGLOGD(log, "Error parsing Mqtt config\n'%s'\n", cfg.c_str());
		return {};
	}

	auto mqtt = cJSON_GetObjectItem(network.get(), "mqtt");
	if (!mqtt || mqtt->type != cJSON_Object) {
		return {};
	}

	ib::mqtt::MqttConfig config;
	config.enabled = json::getBool(mqtt, "enabled");
	config.brokerAddress = json::getString(mqtt, "brokerAddress");
	config.brokerPort = json::getInt(mqtt, "brokerPort");
	config.username = json::getString(mqtt, "username");
	config.password = json::getString(mqtt, "password");
	config.clientId = json::getString(mqtt, "clientId");
	config.base = json::getString(mqtt, "base");
	config.keepAlive = json::getInt(mqtt, "keepAlive");
	config.interval = json::getInt(mqtt, "interval");

	return config;
}

void readDebugOptions(ib::logger::LoggerInterface *log) {
	File file = SPIFFS.open("/cfg/cfgdebug.json", FILE_READ);
	if (!file) {
		DBGLOGD(log, "Debug options not found. Using defaults\n");
		return;
	}
	DBGLOGD(log, "Reading debug options from file\n");
	String cfg = file.readString();
	setDebugOptionsFromJson(log, cfg.c_str());
	file.close();
}

void setDebugOptionsFromJson(ib::logger::LoggerInterface *log, const char *json) {
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(json), &cJSON_Delete);

	auto features = log->getRegisteredFeatures();
	for (auto it = features.begin(); it != features.end(); ++it) {
		auto val =  json::getBoolOptional(root.get(), it->second.c_str());
		if (val.has_value()) {
			log->enableFeature(it->first, *val);
		}
	}
}

}

#include <algorithm>
#include <vector>
#include <set>

#include <Arduino.h>
#include <time.h>
#include <sys/time.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <SPIFFS.h>

#include <time.h>
#include <esp_timer.h>

#include "config.h"
#include <logger/Logger.h>
#include <logger/LoggerSerialSink.h>
#include <logger/LoggerSocketSink.h>
#include <mqtt/MQTT.h>
#include <DeviceDiagnostics.h>
#include "REST.h"
#include "TimeHelpers.h"
#include "Sensor.h"
#include <ESPmDNS.h>
#include "Display.h"
#include <DNSServer.h>


#define ONBOARD_LED 2

namespace intuibase {

std::shared_ptr<ib::logger::LoggerInterface> logger;
std::unique_ptr<REST> rest;
std::shared_ptr<CO2Sensor> sensor;
std::shared_ptr<Display> display;
std::shared_ptr<ib::mqtt::MQTT> mqtt;
int64_t wifiEstabilishedConnection = 0;
bool wifiAPMode = false;
std::unique_ptr<DNSServer> dnsServer;
} // namespace intuibase

void WiFiGotIP(arduino_event_id_t event, arduino_event_info_t info) {
	DBGLOGD(intuibase::logger, "WiFI IP address %s hostname: %s\n", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str(), WiFi.getHostname());

	if (WiFi.isConnected()) {
		if (intuibase::rest) {
			intuibase::rest->restart();
		}

		intuibase::wifiEstabilishedConnection = esp_timer_get_time();

		auto networkConfig = config::getNetworkConfig(intuibase::logger.get());
		if (networkConfig.ntpEnabled) {
			if (!networkConfig.timeZone.empty()) {
				DBGLOGD(intuibase::logger, "Configuring timezone time '%s' server: '%s' ", networkConfig.timeZone.c_str(), networkConfig.ntpHost.c_str());
				configTzTime(networkConfig.timeZone.c_str(), networkConfig.ntpHost.c_str());
			} else {
				configTime(networkConfig.ntpUtcOffset, networkConfig.ntpDaylightUtcOffset, networkConfig.ntpHost.c_str());
			}
		} else {
			DBGLOGD(intuibase::logger, "NTP disabled\n");
		}

		struct tm timeinfo;
		if (!getLocalTime(&timeinfo)) {
			DBGLOGD(intuibase::logger, "Failed to obtain network time.\n");
		} else if (networkConfig.ntpEnabled) {
			DBGLOGD(intuibase::logger, "%s", asctime(&timeinfo));
			DBGLOGD(intuibase::logger, "Obtained network time.\n");
		}

		if (!MDNS.begin(WiFi.getHostname())) { //CONFIG_MDNS_TASK_STACK_SIZE 4096
			DBGLOGD(intuibase::logger, "Error starting mDNS\n");
		} else {
			MDNS.addService(networkConfig.hostname.c_str(), "http", networkConfig.listenPort);
		}
	}

}

void WifiSetUp(config::WiFiConfig const &wifiConfig, config::NetworkConfig const &networkConfig, config::APConfig const &apConfig, bool startAP) {
	if (startAP) {
		DBGLOGD(intuibase::logger, "Starting Access Point\n");

		WiFi.mode(WIFI_AP);
		WiFi.setSleep(WIFI_PS_NONE);

		IPAddress ip, gateway, subnetMask;
		ip.fromString(apConfig.ip.c_str());
		gateway.fromString(apConfig.gateway.c_str());
		subnetMask.fromString(apConfig.subnetMask.c_str());

		bool configResult = WiFi.softAPConfig(ip, gateway, subnetMask);
		if (!configResult) {
			DBGLOGD(intuibase::logger, "Failed to configure Access Point network\n");
			return;
		}

		WiFi.softAPsetHostname(apConfig.hostname.c_str());

		DBGLOGD(intuibase::logger, "Started Access Point. Hostname: '%s'. IP address: %s. config: %d\n", WiFi.softAPgetHostname(), WiFi.softAPIP().toString().c_str(), configResult);

		WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) { DBGLOGD(intuibase::logger, "AccessPoint client IP assigned: '%s'\n", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str()); }, arduino_event_id_t::ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);
		WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) { DBGLOGD(intuibase::logger, "AccessPoint client connected MAC: " MACSTR "\n", MAC2STR(info.wifi_ap_staconnected.mac)); }, arduino_event_id_t::ARDUINO_EVENT_WIFI_AP_STACONNECTED);

		bool softAPResult = WiFi.softAP(apConfig.ssid.c_str(), apConfig.password.c_str(), apConfig.channel);
		intuibase::wifiAPMode = softAPResult;

		DBGLOGD(intuibase::logger, "Soft AP start: %d\n", softAPResult);
		if (!softAPResult) {
			DBGLOGD(intuibase::logger, "Failed to start Access Point\n");
			return;
		} else {
			intuibase::wifiEstabilishedConnection = esp_timer_get_time();

			if (intuibase::rest) {
				DBGLOGD(intuibase::logger, "Restarting REST service\n");
				intuibase::rest->restart();
			}

			if (!MDNS.begin(apConfig.hostname.c_str())) {
				DBGLOGD(intuibase::logger, "Error starting mDNS\n");
			} else {
				MDNS.addService(apConfig.hostname.c_str(), "http", apConfig.listenPort);
			}

			intuibase::dnsServer = std::make_unique<DNSServer>();
			intuibase::dnsServer->start(53, "*", WiFi.softAPIP());
		}
	} else {
		DBGLOGD(intuibase::logger, "networkConfig\n"
		                         "  host: %s\n"
		                         "  ssid: %s\n",
		                         networkConfig.hostname.c_str(), wifiConfig.ssid.c_str());

		intuibase::wifiAPMode = false;

		const char *pass = nullptr;
		if (!wifiConfig.password.empty()) {
			pass = wifiConfig.password.c_str();
		}

		WiFi.setHostname(networkConfig.hostname.c_str());
		WiFi.begin(wifiConfig.ssid.c_str(), pass);

		WiFi.setHostname(networkConfig.hostname.c_str());
		WiFi.setAutoConnect(true);
		WiFi.setAutoReconnect(true);

		WiFi.onEvent(WiFiGotIP, arduino_event_id_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
	}
}

void setup() {
	delay(500); // wait for monitor
	std::vector<std::shared_ptr<ib::logger::LoggerSinkInterface>> sinks = {std::make_shared<ib::logger::LoggerSerialSink>(ib::logger::LoggerInterface::LogLevel::TRACE)};

	intuibase::logger = std::make_shared<ib::logger::Logger>(std::move(sinks));

	DBGLOGD(intuibase::logger, "Free memory %d/%d (minimum was: %d) MaxAlloc: %d STARTUP\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

	pinMode(ONBOARD_LED, OUTPUT);
	digitalWrite(ONBOARD_LED, HIGH);
	digitalWrite(ONBOARD_LED, LOW);

	delay(500);
	Serial.begin(115200);

	const esp_app_desc_t *app = esp_ota_get_app_description();
	const esp_partition_t *partition = esp_ota_get_running_partition();

	DBGLOGD(intuibase::logger, "Project: %s, version: %s\n", app->project_name, app->version);
	DBGLOGD(intuibase::logger, "Build: %s %s\n", app->date, app->time);
	DBGLOGD(intuibase::logger, "IDF: %s\n", app->idf_ver);
	DBGLOGD(intuibase::logger, "Firmware sha256: %s\n", app->app_elf_sha256);
	DBGLOGD(intuibase::logger, "Partition: %s, size: %d, encrypted: %d\n", partition->label, partition->size, partition->encrypted);
	DBGLOGD(intuibase::logger, "-----------------------");
	DBGLOGD(intuibase::logger, "Starting up\n");

	if (!SPIFFS.begin(false)) {
		DBGLOGD(intuibase::logger, "An Error has occurred while mounting SPIFFS\n");
	}

	config::readDebugOptions(intuibase::logger.get());

	DBGLOGD(intuibase::logger, "Free memory %d/%d (minimum was: %d) MaxAlloc: %d SPIFFS\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

	auto networkConfig = config::getNetworkConfig(intuibase::logger.get());

	DBGLOGD(intuibase::logger, "networkConfig\n"
	                         "  host: %s\n"
	                         "  listenPort: %d\n"
	                         "  ntpEnabled: %d\n"
	                         "  ntpHost: %s\n"
	                         "  ntpUtcOffset: %d\n"
	                         "  ntpDaylightUtcOffset: %d\n"
	                         "  timeZone: %s\n"
	                         "  loggerEnabled: %d\n"
	                         "  loggerHost: %s\n"
	                         "  loggerPort: %d\n"
	                         "  loggerTimeoutMs: %d\n",
	                         networkConfig.hostname.c_str(), networkConfig.listenPort, networkConfig.ntpEnabled, networkConfig.ntpHost.c_str(), networkConfig.ntpUtcOffset, networkConfig.ntpDaylightUtcOffset, networkConfig.timeZone.c_str(),
	                         networkConfig.loggerEnabled, networkConfig.loggerHost.c_str(), networkConfig.loggerPort, networkConfig.loggerTimeoutMs);

	if (networkConfig.loggerEnabled) {
		DBGLOGD(intuibase::logger, "Enabling socket logger to %s:%d\n", networkConfig.loggerHost.c_str(), networkConfig.loggerPort);
		auto socketSink = std::make_shared<ib::logger::LoggerSocketSink>(ib::logger::LoggerInterface::LogLevel::DEBUG, networkConfig.loggerHost, networkConfig.loggerPort, networkConfig.loggerTimeoutMs);
		intuibase::logger->attachSink(std::move(socketSink));
	}

	DBGLOGD(intuibase::logger, "Free memory %d/%d (minimum was: %d) MaxAlloc: %d\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
	auto wifiConfig = config::getWiFiConfig(intuibase::logger.get());
	auto apConfig = config::getAPConfig(intuibase::logger.get());

	bool startAP = wifiConfig.ssid.empty() /*|| TODO PUSHBUTTON PRESSED */;

	intuibase::rest = std::make_unique<intuibase::REST>(intuibase::logger, startAP ? apConfig.listenPort : networkConfig.listenPort);
	DBGLOGD(intuibase::logger, "Free memory %d/%d (minimum was: %d) MaxAlloc: %d STUFF\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

	WifiSetUp(wifiConfig, networkConfig, apConfig, startAP);
	DBGLOGD(intuibase::logger, "Free memory %d/%d (minimum was: %d) MaxAlloc: %d WIFI\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

	intuibase::display = std::make_shared<Display>(intuibase::logger, config::getDisplayConfig(intuibase::logger.get()));

	intuibase::sensor = std::make_shared<intuibase::CO2Sensor>(intuibase::logger, config::getCarbonDioxideSensorConfig(intuibase::logger.get()), [](double co2ppm, double temperature, double humidity) {
		intuibase::display->showSensorData(co2ppm, temperature, humidity);
	}, [](double pressure, double temperature) {
		intuibase::display->showBmpSensorData(pressure, temperature);
	});

	intuibase::rest->addStatusReporter("air-sensor", intuibase::sensor);
	intuibase::rest->setCalibrate([]() {
		if (intuibase::sensor) {
			intuibase::sensor->triggerCalibration();
		}
	});

	auto mqttConfig = config::getMqttConfig(intuibase::logger.get());

	ib::mqtt::MQTT::HomeAssistantDeviceInfo deviceInfo;
	deviceInfo.name = "IntuiBase CO2 Sensor";
	deviceInfo.model = "IntuiBase CO2 Sensor";
	deviceInfo.manufacturer = "intuibase";
	deviceInfo.swVersion = "1.0.0";

	std::vector<std::shared_ptr<ib::mqtt::MQTTReporterInterface>> mqttReporters{intuibase::sensor, std::make_shared<intuibase::DeviceDiagnostics>(intuibase::logger)};

	intuibase::mqtt = std::make_shared<ib::mqtt::MQTT>(intuibase::logger, mqttConfig, std::move(deviceInfo), std::move(mqttReporters));

	DBGLOGD(intuibase::logger, "Free memory %d/%d (minimum was: %d) MaxAlloc: %d SETUP DONE\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());	
}

void loop() {
	static ib::PeriodicCounter logCounter_{10000};

	if (logCounter_.durationPassed()) {
		DBGLOGD(intuibase::logger, "Free memory %d/%d (minimum was: %d) MaxAlloc: %d MinPeekStack: %d UpTime: %lds SPIFFS: %zu/%zu\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), uxTaskGetStackHighWaterMark(nullptr), esp_timer_get_time() / 1000000, SPIFFS.usedBytes(), SPIFFS.totalBytes());

		if (!WiFi.isConnected() && !intuibase::wifiAPMode) {
			if (intuibase::wifiEstabilishedConnection == 0 && esp_timer_get_time() > 15000000) {
				DBGLOGD(intuibase::logger, "No connection for 15s, starting AP mode\n");
				auto networkConfig = config::getNetworkConfig(intuibase::logger.get());
				auto apConfig = config::getAPConfig(intuibase::logger.get());
				WifiSetUp({}, networkConfig, apConfig, true);
			} else {
				DBGLOGD(intuibase::logger, "WiFi not connected. Reconnecting.\n");
				WiFi.reconnect();
			}
		} else {
			if (intuibase::wifiAPMode) {
				DBGLOGD(intuibase::logger, "WiFi in AP mode. IP Address: %s\n", WiFi.softAPIP().toString().c_str());
			} else {
				DBGLOGD(intuibase::logger, "WiFi IP Address: %s\n", WiFi.localIP().toString().c_str());
			}
		}
	}

	intuibase::rest->handle();
	intuibase::mqtt->loop();
	if (intuibase::dnsServer) {
		intuibase::dnsServer->processNextRequest();
	}
}

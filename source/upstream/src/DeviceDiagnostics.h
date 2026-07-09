#pragma once



#include <mutex>
#include <functional>

#include <WiFi.h>

#include "config.h"
#include <viewable_stringbuf.h>
#include <mqtt/MQTTReporterInterface.h>
#include <mqtt/MQTTPublishInterface.h>
#include <logger/LoggerInterface.h>
#include <PeriodicCounter.h>
#include <freertos/task.h>
#include <atomic>
#include "TimeHelpers.h"
#include "StatusReportingInterface.h"



#define ADD_ITEM_TO_JSON(name, value)     \
	{                                     \
		if (!first) {                     \
			ss << ",";                    \
		} else {                          \
			first = false;                \
		}                                 \
		ss << "\"" #name "\": " << value; \
	}

namespace intuibase {

using namespace std::string_view_literals;

class DeviceDiagnostics : public ib::StatusReportingInterface, public ib::mqtt::MQTTReporterInterface {
public:
	using reportSensorData_t = std::function<void(double co2ppm, double temperature, double humidity)>;
	using reportBmpSensorData_t = std::function<void(double pressure, double temperature)>;

	DeviceDiagnostics(std::shared_ptr<ib::logger::LoggerInterface> log ) : log_(std::move(log)) {
		logFeature_ = log_->addFeature("DIAGNOSTICS");
	}

	void getStatus(std::ostream &ss) const override {
		bool first = true;
		ss << "{";
		ADD_ITEM_TO_JSON(ipaddress, "\"" << WiFi.localIP().toString().c_str() << "\"" );
		ADD_ITEM_TO_JSON(uptime, esp_timer_get_time() / 1000000);
		ADD_ITEM_TO_JSON(free_heap, ESP.getFreeHeap());
		ADD_ITEM_TO_JSON(max_alloc_heap, ESP.getMaxAllocHeap());
		ADD_ITEM_TO_JSON(rssi, (int)WiFi.RSSI());
		ss << "}";
#undef ADD_ITEM_TO_JSON
	}

	static constexpr std::string_view state_topic_sensor = "co2sensor_diagnostic"sv;

	void publishHADiscovery(ib::mqtt::MQTTPublishInterface &publish) override {
		// stateTopic,  sensorUniqueId,sensorFriendlyName,  jsonValueName,  valueOperation,  unit, std::string_view stateClass, std::string_view devClass, std::string_view entityCategory)>;
		publish.publishAutoDiscoverySensor(state_topic_sensor, "ipaddress"sv, "IP Address"sv, "ipaddress"sv, {}, {}, {}, {}, "diagnostic"sv);
		publish.publishAutoDiscoverySensor(state_topic_sensor, "uptime"sv, "Uptime"sv, "uptime"sv, {}, "s"sv, ""sv, "duration"sv, "diagnostic"sv);
		publish.publishAutoDiscoverySensor(state_topic_sensor, "free_heap"sv, "Free Memory"sv, "free_heap"sv, {}, "B"sv, ""sv, "data_size"sv, "diagnostic"sv);
		publish.publishAutoDiscoverySensor(state_topic_sensor, "max_alloc_heap"sv, "Maxium allocable free memory block"sv, "max_alloc_heap"sv, ""sv, "B"sv, ""sv, "data_size"sv, "diagnostic"sv);
		publish.publishAutoDiscoverySensor(state_topic_sensor, "rssi"sv, "WiFi RSSI"sv, "rssi"sv, {}, "dBm"sv, ""sv, "signal_strength"sv, "diagnostic"sv);

		publish.publishAutoDiscoveryButton("co2sensor_restart", "restart_device"sv, "Restart Device"sv, "restart"sv, "config"sv);
		publish.subscribe("co2sensor_restart", [this](char *topic, uint8_t *payload, unsigned int payloadLen) {
			DBGLOGI(log_, "MQTT payload recieved in '%s', value: '%s'\n", topic, payload); 
			if (std::string_view(reinterpret_cast<const char*>(payload), payloadLen) != "PRESS") {
				DBGLOGW(log_, "Ignoring unknown restart command payload\n");
				return;
			}
			ESP.restart();
		});

	}
	void publishStateTopic(ib::mqtt::MQTTPublishInterface &publishInterface, uint16_t intervalSecs) override {
		lastMqttPublishCounter_.setIntervalMs(intervalSecs * 1000);
		if (!lastMqttPublishCounter_.durationPassed()) {
			DBGLOGFD(log_, logFeature_, "Skipping MQTT publish, interval not passed yet. Time to wait: %ld ms\n", lastMqttPublishCounter_.getTimeToWaitMs());
			return;
		}

		ib::viewable_stringbuf payloadBuf;
		std::ostream ss(&payloadBuf);
		getStatus(ss);

		publishInterface.publishStateTopic(state_topic_sensor, payloadBuf.view(), false);
	}

private:

	std::shared_ptr<ib::logger::LoggerInterface> log_;
	ib::logger::LoggerInterface::LogFeatureType logFeature_;
	ib::PeriodicCounter lastMqttPublishCounter_{10000};
};

} // namespace intuibase
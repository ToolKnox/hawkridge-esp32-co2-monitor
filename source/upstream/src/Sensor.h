#pragma once

#include <Arduino.h>
#include <SparkFun_SCD4x_Arduino_Library.h>
#include <SFE_BMP180.h>
#include <Wire.h>


#include <mutex>
#include <functional>

#include "config.h"
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

static void SensorReadTask(void *pvParameters);

class CO2Sensor : public ib::StatusReportingInterface, public ib::mqtt::MQTTReporterInterface {
public:
	using reportSensorData_t = std::function<void(double co2ppm, double temperature, double humidity)>;
	using reportBmpSensorData_t = std::function<void(double pressure, double temperature)>;

	CO2Sensor(std::shared_ptr<ib::logger::LoggerInterface> log, config::CarbonDioxideSensorConfig config, reportSensorData_t reportSensorData, reportBmpSensorData_t reportBmpSensorData) : log_(std::move(log)), config_(std::move(config)), reportSensorData_(reportSensorData), reportBmpSensorData_(reportBmpSensorData) {
		logFeature_ = log_->addFeature("SENSOR");
		DBGLOGFD(log_, logFeature_, "Starting up. SDA pin: %d, SCL pin: %d\n", config_.sda_pin, config_.scl_pin);

		Wire.begin(config_.sda_pin, config_.scl_pin);
		bmp180_.begin();
		Wire.setClock(100000);
		uint8_t rc = scd4x_.begin(Wire);

		if (rc != 0) {
			DBGLOGFD(log_, logFeature_, "Error initializing SCD4X sensor: %d\n", rc);
		} else {
			DBGLOGFD(log_, logFeature_, "SCD4X sensor initialized successfully\n");
		}

		DBGLOGFD(log_, logFeature_, "Scanning I2C bus...\n");
		int error = false;
		for (auto address = 1; address < 127; address++) {
			Wire.beginTransmission(address);
			error = Wire.endTransmission();

			if (error == 0) {
				DBGLOGFD(log_, logFeature_, "Device found at address 0x%02X\n", address);
			} else if (error == 4) {
				DBGLOGFD(log_, logFeature_, "Error at address 0x%02X\n", address);
			}
		}

		DBGLOGFD(log_, logFeature_, "Temperature time: %d\n", bmp180_.startTemperature());
		DBGLOGFD(log_, logFeature_, "Pressure time: %d\n", bmp180_.startPressure(1));

		// scd4x_.startPeriodicMeasurement();
		xTaskCreate(SensorReadTask, "AirSensor", 2560, this, 1, NULL);
	}

	void getStatus(std::ostream &ss) const override{
		std::lock_guard<std::mutex> lock(mutex_);
		bool first = true;
		ss << std::setprecision(2) << std::fixed;
		ss << "{";
		ADD_ITEM_TO_JSON(co2ppm, co2ppm_);
		if (!isnan(temperature_)) {
			ADD_ITEM_TO_JSON(temperature, temperature_);
		}
		if (!isnan(humidity_)) {
			ADD_ITEM_TO_JSON(humidity, humidity_);
		}
		if (!isnan(bmpTemperature_)) {
			ADD_ITEM_TO_JSON(pressureTemperature, bmpTemperature_);
		}
		if (!isnan(bmpPressure_)) {
			ADD_ITEM_TO_JSON(pressure, bmpPressure_);
		}
		ADD_ITEM_TO_JSON(scd4x_calibrating, (calibrating_ ? "\"on\"" : "\"off\""));
		ss << "}";
#undef ADD_ITEM_TO_JSON
	}

	void triggerCalibration() {
		DBGLOGFD(log_, logFeature_, "Starting SCD4X calibration\n");
		calibrating_ = true;
	}

	static constexpr std::string_view state_topic_sensor = "co2sensor"sv;

	void publishHADiscovery(ib::mqtt::MQTTPublishInterface &publish) override {
		publish.publishAutoDiscoverySensor(state_topic_sensor, "co2"sv, "CO2 level"sv, "co2ppm"sv, ""sv, "ppm"sv, "measurement"sv, "carbon_dioxide"sv, {});
		publish.publishAutoDiscoverySensor(state_topic_sensor, "temperature"sv, "Air temperature"sv, "temperature"sv, ""sv, "°C"sv, "measurement"sv, "temperature"sv, {});
		publish.publishAutoDiscoverySensor(state_topic_sensor, "humidity"sv, "Air humidity"sv, "humidity"sv, ""sv, "%"sv, "measurement"sv, "humidity"sv, {});
		publish.publishAutoDiscoverySensor(state_topic_sensor, "pressure"sv, "Atmospheric pressure"sv, "pressure"sv, ""sv, "mbar"sv, "measurement"sv, "pressure"sv, {});
		publish.publishAutoDiscoverySensor(state_topic_sensor, "temperatureBMP180"sv, "Temperature BMP180"sv, "pressureTemperature"sv, ""sv, "°C"sv, "measurement"sv, "temperature"sv, {});
		publish.publishAutoDiscoveryBinarySensor(state_topic_sensor, "scd4x_calibrating"sv, "SCD4X Calibrating"sv, "scd4x_calibrating"sv, "running"sv, "diagnostic"sv);

		publish.publishAutoDiscoveryButton("co2sensor_calibrate", "calibrate_co2_sensor"sv, "Calibrate CO2 sensor"sv, ""sv, "config"sv);
		publish.subscribe("co2sensor_calibrate", [this](char *topic, uint8_t *payload, unsigned int payloadLen) {
			DBGLOGI(log_, "MQTT payload recieved in '%s', value: '%s'\n", topic, payload);
			if (std::string_view(reinterpret_cast<const char*>(payload), payloadLen) != "PRESS") {
				DBGLOGW(log_, "Ignoring unknown calibrate command payload\n");
				return;
			}
			triggerCalibration();
		});

	}
	void publishStateTopic(ib::mqtt::MQTTPublishInterface &publish, uint16_t intervalSecs) override {
		lastMqttPublishCounter_.setIntervalMs(intervalSecs * 1000);
		if (!lastMqttPublishCounter_.durationPassed()) {
			DBGLOGFD(log_, logFeature_, "Skipping MQTT publish, interval not passed yet. Time to wait: %ld ms\n", lastMqttPublishCounter_.getTimeToWaitMs());
			return;
		}

		{
		std::lock_guard<std::mutex> lock(mutex_);
		if (co2ppm_ == 0) {
			DBGLOGFD(log_, logFeature_, "Skipping MQTT publish, no valid sensor data yet\n");
			return;
		}
		}

		ib::viewable_stringbuf payloadBuf;
		std::ostream ss(&payloadBuf);
		getStatus(ss);

		publish.publishStateTopic(state_topic_sensor, payloadBuf.view(), false);
	}


private:

	void readBMP180() {
		DBGLOGFD(log_, logFeature_, "Starting BMP180 measurements\n");
		int delayMs = bmp180_.startTemperature();
		vTaskDelay(delayMs / portTICK_PERIOD_MS);
		double bmpTemperature = 0.0;
		if (bmp180_.getTemperature(bmpTemperature)) {
			DBGLOGFD(log_, logFeature_, "BMP180 Temperature: %.2f C\n", bmpTemperature);
		} else {
			DBGLOGFD(log_, logFeature_, "Error reading BMP180 temperature\n");
			return;
		}

		delayMs = bmp180_.startPressure(3);
		vTaskDelay(delayMs / portTICK_PERIOD_MS);

		double pressure = 0.0;
		if (bmp180_.getPressure(pressure, bmpTemperature)) {
			DBGLOGFD(log_, logFeature_, "BMP180 Pressure: %.2f mbar\n", pressure);
		} else {
			DBGLOGFD(log_, logFeature_, "Error reading BMP180 pressure\n");
			return;
		}

		{
			std::lock_guard<std::mutex> lock(mutex_);
			bmpTemperature_ = bmpTemperature;
			bmpPressure_ = pressure;
		}
		if (reportBmpSensorData_) {
			reportBmpSensorData_(pressure, bmpTemperature);
		}
	}

	void readSCD4X() {
		while (!scd4x_.getDataReadyStatus()) {
			vTaskDelay(500 / portTICK_PERIOD_MS);
			DBGLOGFD(log_, logFeature_, "SCD4X Data not ready yet\n");
		}

		if (!scd4x_.readMeasurement()) {
			DBGLOGFD(log_, logFeature_, "Error reading SCD4X measurement\n");
		} else {
			uint16_t co2ppm = scd4x_.getCO2();
			double temperature = scd4x_.getTemperature();
			double humidity = scd4x_.getHumidity();
			{
				std::lock_guard<std::mutex> lock(mutex_);
				co2ppm_ = co2ppm;
				temperature_ = temperature;
				humidity_ = humidity;
			}

			DBGLOGFD(log_, logFeature_, "CO2: %d ppm, Temperature: %.2f C, Humidity: %.2f %%\n", co2ppm, temperature, humidity);
			if (reportSensorData_) {
				reportSensorData_(co2ppm, temperature, humidity);
			}
			handleAlarm(co2ppm);
		}
	}

	void performSCD4XCalibration() {
		uint16_t co2ppm;
		{
		std::lock_guard<std::mutex> lock(mutex_);
		co2ppm = co2ppm_;
		}

		DBGLOGFD(log_, logFeature_, "Current CO2 ppm for calibration: %d. Stopping periodic measurement\n", co2ppm);
		if (scd4x_.stopPeriodicMeasurement()) {
			DBGLOGFD(log_, logFeature_, "SCD4X periodic measurement stopped successfully\n");
		} else {
			DBGLOGFD(log_, logFeature_, "Error stopping SCD4X periodic measurement\n");
			return;
		}
		vTaskDelay(500 / portTICK_PERIOD_MS); 

		float correction = scd4x_.performForcedRecalibration(co2ppm);
		DBGLOGFD(log_, logFeature_, "SCD4X calibration correction: %.2f\n", correction);
		vTaskDelay(500 / portTICK_PERIOD_MS);

		bool result = scd4x_.startPeriodicMeasurement();
		DBGLOGFD(log_, logFeature_, "SCD4X periodic measurement restarted: %s\n", result ? "success" : "failure");
	}

	void handleAlarm(uint16_t co2ppm) {
		if (config_.buzzer_pin < 0) {
			return;
		}

		if (co2ppm < 1300) {
			return;
		}

		if (co2ppm >= 1300 && co2ppm < 1600) {
			lastAlarm_.setIntervalMs(60000);
		} else if (co2ppm >= 1600 && co2ppm < 3000) {
			lastAlarm_.setIntervalMs(30000);
		} else if (co2ppm >= 3000) {
			lastAlarm_.setIntervalMs(10000);
		}

		if (!lastAlarm_.durationPassed()) {
			return;
		}

		lastAlarm_.notifyNow();
		tone(config_.buzzer_pin, 400, 200);

		DBGLOGFD(log_, logFeature_, "SCD4X Alarm triggered! CO2 ppm: %d interval: %ud ms wait for: %d \n", co2ppm, lastAlarm_.getIntervalMs(), lastAlarm_.getTimeToWaitMs());
		return;

	}

private:

	std::shared_ptr<ib::logger::LoggerInterface> log_;
	ib::logger::LoggerInterface::LogFeatureType logFeature_;
	config::CarbonDioxideSensorConfig config_;
	SCD4x scd4x_;
	SFE_BMP180 bmp180_;
	reportSensorData_t reportSensorData_;
	reportBmpSensorData_t reportBmpSensorData_;

	mutable std::mutex mutex_;
	uint16_t co2ppm_ = 0;
	double temperature_ = 0.0;
	double humidity_ = 0.0;
	double bmpTemperature_ = 0.0;
	double bmpPressure_ = 0.0;
	std::atomic_bool calibrating_ = false;
	ib::PeriodicCounter lastMqttPublishCounter_{10000};
	ib::PeriodicCounter lastAlarm_{60000};

	friend void SensorReadTask(void *pvParameters);
};

static void SensorReadTask(void *pvParameters) {
	CO2Sensor *obj = static_cast<CO2Sensor *>(pvParameters);
	unsigned long lastLogMillis = 0;
	unsigned long lastSaveMillis = 0;

	while (true) {
		if (obj->calibrating_) {
			obj->performSCD4XCalibration();
			obj->calibrating_ = false;
		}

		obj->readBMP180();
		obj->readSCD4X();

		vTaskDelay(5000 / portTICK_PERIOD_MS);
	}
	vTaskDelete(nullptr);
}

} // namespace intuibase
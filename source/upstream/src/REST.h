#pragma once

#include <esp_ota_ops.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include <cJSON.h>

#include "viewable_stringbuf.h"
#include <logger/LoggerInterface.h>

#include "Network.h"
#include <string_view>
#include <iostream>
#include <iomanip>
#include <memory>
#include "StatusReportingInterface.h"
#include <map>

namespace intuibase {

using namespace std::string_view_literals;
using namespace std::string_literals;

class WebServerStringView : public WebServer {
public:
	WebServerStringView(uint16_t listenPort) : WebServer(listenPort) {

	}

	void sendView(int code, std::string_view content_type, std::string_view content) {
		String header;
		if (content.length() == 0) {
			log_w("content length is zero");
		}

		_prepareHeader(header, code, content_type.data(), content.length());
		_currentClientWrite(header.c_str(), header.length());

		if(content.length()) {
			sendContent(content.data(), content.length());
		}
	}

	void sendBytes(const char *bytes, size_t size) { _currentClientWrite(bytes, size); }
	void endResponse() { _finalizeResponse(); }

	HTTPUpload *getUpload() { return _currentUpload.get(); }
};

class REST {
public:
	REST(std::shared_ptr<ib::logger::LoggerInterface> log, uint16_t listenPort) : log_(std::move(log)), server_(listenPort) {
		logFeature_ = log_->addFeature("REST");
		server_.enableCORS(true);
		server_.enableCrossOrigin(true);

		server_.on("/status/wifi", [this]() {
			DBGLOGFD(log_, logFeature_, "REST:wifiNetworks\n");

			ib::viewable_stringbuf payloadBuf;
			std::ostream payload(&payloadBuf);

			getWiFiNetworks(payload);
			server_.sendView(200, "application/json"sv, payloadBuf.view());
		});

		server_.on("/config/calibrate", HTTP_POST, [this]() { calibrate(); });

		server_.on("/status", [this]() { status(); });
		server_.on("/status/version", [this]() { version(); });

		server_.on("/config/wifi", [this]() { configWiFi(); });
		server_.on("/config/device", [this]() { configDevice(); });
		server_.on("/config/hardware", [this]() { configHardware(); });
		server_.on("/config/debug", [this]() { configDebug(); });

		server_.on("/config/reboot", HTTP_GET, [this]() { configReboot(); });

		server_.on("/ota", HTTP_POST, [this]() { handleOTAResponse(); }, [this]() { handleOTAUpdate(); });
		server_.on("/otafs", HTTP_POST, [this]() { handleOTAResponse(); }, [this]() { handleOTAFFSUpdate(); });

		server_.on("/", HTTP_GET, [this]() { index(); });
		server_.on("/index.html", HTTP_GET, [this]() { index(); });

		server_.on("/gen_204", [this]() { gen204(); });
		server_.on("/generate_204", [this]() { gen204(); });
		server_.on("/captive.apple.com", [this]() { gen204(); });
		server_.on("/hotspot-detect.html", [this]() { gen204(); });
		server_.on("/success.txt", [this]() { gen204(); });
		server_.on("/ncsi.txt", [this]() { gen204(); });
		server_.on("/connecttest.txt", [this]() { gen204(); });
		server_.on("/connecttest.html", [this]() { gen204(); });

		server_.onNotFound([this]() {
			serveFile(server_.uri().c_str());
		});
	}

	void setCalibrate(std::function<void()> calibrate) { calibrate_ = calibrate; }

	void restart() {
		server_.stop();
		server_.begin();
	}

	void handle() {
		server_.handleClient();
	}

	void addStatusReporter(std::string name, std::shared_ptr<ib::StatusReportingInterface> reporter) { reporters_.emplace(name, std::move(reporter)); }

private:
	void calibrate() {
		DBGLOGFD(log_, logFeature_, "Calibrate\n");
		if (calibrate_) {
			calibrate_();
		}
		server_.send(200);
	}

	void configReboot() {
		DBGLOGFD(log_, logFeature_, "configReboot\n");
		server_.send(200);
		server_.stop();
		ESP.restart();
	}

	void status() {
		ib::viewable_stringbuf payloadBuf;
		std::ostream payload(&payloadBuf);
		bool first = true;
		payload << '{';
		for (auto const &reporter : reporters_) {
			if (first) {
				first = false;
			} else {
				payload << ',';
			}
			payload << "\"" << reporter.first << "\": "sv;
			reporter.second->getStatus(payload);
		}
		payload << "}";

		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void version() {
		DBGLOGFD(log_, logFeature_, "version\n");

		const esp_app_desc_t *app = esp_ota_get_app_description();

		ib::viewable_stringbuf payloadBuf;
		std::ostream payload(&payloadBuf);
		payload << '{';
		payload << "\"project\": \""sv << app->project_name << "\","sv;
		payload << "\"version\": \""sv << app->version << "\","sv;
		payload << "\"date\": \""sv << app->date << "\","sv;
		payload << "\"time\": \""sv << app->time << "\","sv;
		payload << "\"idf\": \""sv << app->idf_ver << "\","sv;
		payload << "\"sha256\": \""sv;
		for (size_t i = 0; i < sizeof(app->app_elf_sha256); ++i) {
			payload << std::hex << std::setw(2) << std::setfill('0') << (int)app->app_elf_sha256[i];
		}
		payload << "\"}";

		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void configWiFi() {
		DBGLOGFD(log_, logFeature_, "configWiFi METHOD %d\n", server_.method());

		switch (server_.method()) {
			default:
			case HTTP_GET: {
				ib::viewable_stringbuf payloadBuf;
				std::ostream payload(&payloadBuf);
				getWiFiSSID(payload, log_.get());
				server_.sendView(200, "application/json"sv, payloadBuf.view());
				break;
			}
			case HTTP_POST: {
				if (!server_.hasArg("plain")) {
					server_.send(204);
					break;
				}
				auto body = server_.arg("plain");
				if (body.isEmpty()) {
					server_.send(204);
					break;
				}

				File file = SPIFFS.open("/cfg/cfgwifi.json", FILE_WRITE);
				if (!file) {
					server_.send(500, "text/plain", "Internal server error. Can't save wifi settings.");
					break;
				}
				file.write((uint8_t *)body.c_str(), body.length());
				file.close();
				server_.send(201);
				break;
			}
		}

	}

	void configDevice() {
		DBGLOGFD(log_, logFeature_, "configDevice METHOD %d\n", server_.method());

		switch (server_.method()) {
			default:
			case HTTP_GET: {
				File file = SPIFFS.open("/cfg/cfgnetwork.json", FILE_READ);
				if (!file) {
					server_.send(404, "text/plain", "FileNotFound");
					return;
				}
				server_.streamFile(file, "application/json");
				file.close();
				break;
			}
			case HTTP_POST: {
				if (!server_.hasArg("plain")) {
					server_.send(204);
					break;
				}
				auto body = server_.arg("plain");
				if (body.isEmpty()) {
					server_.send(204);
					break;
				}

				File file = SPIFFS.open("/cfg/cfgnetwork.json", FILE_WRITE);
				if (!file) {
					DBGLOGFD(log_, logFeature_, "configDevice. Can't open config file for write.\n");
					server_.send(500, "text/plain", "Internal server error. Can't save device settings.");
					break;
				}
				file.write((uint8_t *)body.c_str(), body.length());
				file.close();
				server_.send(201);
				break;
			}
		}
	}

	void configHardware() {
		DBGLOGFD(log_, logFeature_, "configHardware METHOD %d\n", server_.method());

		switch (server_.method()) {
			default:
			case HTTP_GET: {
				File file = SPIFFS.open("/cfg/cfgpins.json", FILE_READ);
				if (!file) {
					server_.send(404, "text/plain", "FileNotFound");
					return;
				}
				server_.streamFile(file, "application/json");
				file.close();
				break;
			}
			case HTTP_POST: {
				if (!server_.hasArg("plain")) {
					server_.send(204);
					break;
				}
				auto body = server_.arg("plain");
				if (body.isEmpty()) {
					server_.send(204);
					break;
				}

				File file = SPIFFS.open("/cfg/cfgpins.json", FILE_WRITE);
				if (!file) {
					DBGLOGFD(log_, logFeature_, "configHardware. Can't open config file for write.\n");
					server_.send(500, "text/plain", "Internal server error. Can't save hardware settings.");
					break;
				}
				file.write((uint8_t *)body.c_str(), body.length());
				file.close();
				server_.send(201);
				break;
			}
		}
	}

	void configCalibrate() {
		server_.send(204);
	}

	void configDebug() {
		DBGLOGFD(log_, logFeature_, "configDebug METHOD %d\n", server_.method());

		switch (server_.method()) {
			default:
			case HTTP_GET: {
				ib::viewable_stringbuf payloadBuf;
				std::ostream ss(&payloadBuf);
				ss << "{";
				auto features = log_->getRegisteredFeatures();
				for (auto it = features.begin(); it != features.end(); ++it) {
					ss << "\"" << it->second << "\": " << (log_->isFeatureEnabled(it->first) ? "true" : "false");
					auto nextIt = it;
					++nextIt;
					if (nextIt != features.end()) {
						ss << ",";
					}
				}
				ss << "}";
				server_.sendView(200, "application/json"sv, payloadBuf.view());
				break;
			}
			case HTTP_POST: {
				if (!server_.hasArg("plain")) {
					server_.send(204);
					return;
				}
				auto body = server_.arg("plain");
				if (body.isEmpty()) {
					server_.send(204);
					return;
				}

				config::setDebugOptionsFromJson(log_.get(), body.c_str());
				DBGLOGFD(log_, logFeature_, "configDebug. Flags set.\n");

				File file = SPIFFS.open("/cfg/cfgdebug.json", FILE_WRITE);
				if (!file) {
					DBGLOGFD(log_, logFeature_, "configDebug. Can't open config file for write.\n");
					server_.send(500, "text/plain", "Internal server error. Can't save hardware settings.");
					break;
				}
				file.write((uint8_t *)body.c_str(), body.length());
				file.close();

				DBGLOGFD(log_, logFeature_, "configDebug. Flags stored.\n");
				server_.send(201);
				break;
			}

		}
	}

	void index() {
		serveFile("/index.html");
	}

	void gen204() {
		DBGLOGFD(log_, logFeature_, "Caprive portal redirect from gen204 '%s' to 'http://%s'\n", server_.uri().c_str(), server_.client().localIP().toString().c_str());
		server_.sendHeader("Location", String("http://") + server_.client().localIP().toString(), true);
		server_.send(302, "text/plain", "redirecting");
		server_.client().stop();
	}

	void serveFile(const char *serverPath) {
		DBGLOGFD(log_, logFeature_, "Request for: '%s'\n", serverPath);

		String path = "/html";
		path += serverPath;

		if (!SPIFFS.exists(path)) {
			path += ".gz";
		}

		if (strcmp(serverPath, "/favicon.ico") == 0) {
			path = "/html/img/co2_h32.bmp";
			DBGLOGFD(log_, logFeature_, "Favicon path '%s'\n", path.c_str());
		}

		if (!SPIFFS.exists(path)) {
			DBGLOGFD(log_, logFeature_, "Path not found '%s'\n", path.c_str());
			// gen204();
			server_.send(404, "text/plain", "FileNotFound");
		}

		File file = SPIFFS.open(path, FILE_READ);
		if (!file) {
			server_.send(500, "text/plain", "Internal Server Error. Can't open file.");
		}

		auto content = getContentType(path);

		if (content.first) {
			server_.sendHeader("Cache-Control", "max-age=3600");
			server_.sendHeader("Cache-Control", "private");
		}

		server_.streamFile(file, content.second);
		file.close();
	}

	std::pair<bool, const char *>getContentType(String path) {
		if (path.endsWith("css")) {
			return {false, "text/css"};
		} else if (path.endsWith("css.gz")) {
			return {false, "text/css"};
		} else if (path.endsWith("js")) {
			return {false, "text/javascript"};
		} else if (path.endsWith("js.gz")) {
			return {false, "text/javascript"};
		} else if (path.endsWith("png")) {
			return {true, "image/png"};
		} else if (path.endsWith("jpg")) {
			return {true, "image/jpeg"};
		} else if (path.endsWith("html.gz")) {
			return {false, "text/html"};
		} else if (path.endsWith("html")) {
			return {false, "text/html"};
		} else {
			return {true, "text/plain"};
		}
	}

	void handleOTAUpdate() {
		HTTPUpload &upload = server_.upload();

		if (upload.status == UPLOAD_FILE_START) {
			auto size = server_.arg("size");
			long fileSize = atol(size.c_str());

			DBGLOGFD(log_, logFeature_, "handleOTA START '%s', totalSize: '%zu'\n", upload.filename.c_str(), fileSize);

			ota_ = OTAUpload{};

			ota_.partition = esp_ota_get_next_update_partition(NULL);
			if (!ota_.partition) {
				DBGLOGFD(log_, logFeature_, "OTA partition not found\n");
				ota_.errorMessage = "OTA partition not found"sv;
				ota_.error = -1;
				return;
			}

			if (fileSize > 0 && fileSize > ota_.partition->size) {
				DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate Partition size %zu smaller than binary file %zu!\n", ota_.partition->size, fileSize);
				ota_.errorMessage = "Partition smaller than file"sv;
				ota_.error = -1;
				return;
			}


			DBGLOGFD(log_, logFeature_, "handleOTA Found partition '%s', size: %d, encrypted: %d\n", ota_.partition->label, ota_.partition->size, ota_.partition->encrypted);

			DBGLOGFD(log_, logFeature_, "Beginning OTA\n");

			if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
				DBGLOGFD(log_, logFeature_, "Beginning OTA: esp_ota_mark_app_valid_cancel_rollback failed\n");
			}

			ota_.error = esp_ota_begin(ota_.partition, OTA_SIZE_UNKNOWN, &ota_.handle);
			if (ota_.error != ESP_OK) {
				DBGLOGFD(log_, logFeature_, "Beginning OTA failed!\n");
				return;
			}
			ota_.started = true;

			DBGLOGFD(log_, logFeature_, "Beginning OTA handle: %d\n", ota_.handle);
		} else if (upload.status == UPLOAD_FILE_WRITE) {
			if (!ota_.started || ota_.error != ESP_OK) {
				if (!ota_.writeErrorReported) {
					DBGLOGFD(log_, logFeature_, "handleOTA writing skipped, OTA error: %d\n", ota_.error);
					ota_.writeErrorReported = true;
				}
				return;
			}

			DBGLOGFD(log_, logFeature_, "handleOTA writing to OTA handle %d, size: %zu\n", ota_.handle, upload.currentSize);

			ota_.error = esp_ota_write(ota_.handle, upload.buf, upload.currentSize);
			if (ota_.error != ESP_OK) {
				DBGLOGFD(log_, logFeature_, "handleOTA writing to OTA handle %d, size: %zu FAILED, error: %d\n", ota_.handle, upload.currentSize, ota_.error);
				esp_ota_abort(ota_.handle);
				return;
			}
		} else if (upload.status == UPLOAD_FILE_END) {
			if (!ota_.started || ota_.error != ESP_OK) {
				DBGLOGFD(log_, logFeature_, "handleOTA upload end, error: %d\n", ota_.error);
				return;
			}

			DBGLOGFD(log_, logFeature_, "handleOTA ending OTA\n");

			ota_.error = esp_ota_end(ota_.handle);
			if (ota_.error != ESP_OK) {
				DBGLOGFD(log_, logFeature_, "handleOTA finalizing OTA handle %d, FAILED, error: %d\n", ota_.handle, ota_.error);
				return;
			}

			DBGLOGFD(log_, logFeature_, "handleOTA setting boot partition\n");

			ota_.error = esp_ota_set_boot_partition(ota_.partition);
			if (ota_.error != ESP_OK) {
				DBGLOGFD(log_, logFeature_, "handleOTA setting boot partition FAILED, error: %d\n", ota_.error);
				return;
			}
			ota_.success = true;

		} else if (upload.status == UPLOAD_FILE_ABORTED) {
			if (!ota_.started) {
				DBGLOGFD(log_, logFeature_, "handleOTA upload aborted, OTA not started. Error: %d\n", ota_.error);
				return;
			}
			DBGLOGFD(log_, logFeature_, "handleOTA ABORTED\n");
			esp_ota_abort(ota_.handle);
		}
	}

	void handleOTAResponse() {
		auto ctype = "text/plain"sv;
		if (ota_.success) {
			server_.sendView(200, ctype, "success"sv);
		} else {
			ib::viewable_stringbuf payloadBuf;
			std::ostream ss(&payloadBuf);
			ss << "Failure: ";
			ss << ota_.errorMessage << " (" << ota_.error << ')';
			server_.sendView(500, ctype, payloadBuf.view());
		}
	}

	void handleOTAFFSUpdate() {
		HTTPUpload &upload = server_.upload();

		if (upload.status == UPLOAD_FILE_START) {
			auto size = server_.arg("size");
			long fileSize = atol(size.c_str());

			DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate START '%s', totalSize: '%zu'\n", upload.filename.c_str(), fileSize);

			ota_ = OTAUpload{};
			ota_.partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
			if (!ota_.partition) {
				DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate partition not found\n");
				ota_.errorMessage = "FS partition not found"sv;
				ota_.error = -1;
				return;
			}

			DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate Found partition '%s', size: %d, encrypted: %d\n", ota_.partition->label, ota_.partition->size, ota_.partition->encrypted);

			if (fileSize > 0 && fileSize > ota_.partition->size) {
				DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate Partition size %zu smaller than binary file %zu!\n", ota_.partition->size, fileSize);
				ota_.errorMessage = "FS partition smaller than file"sv;
				ota_.error = -1;
				return;
			}

			SPIFFS.end();

			DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate Erasing partition\n");
			ota_.error = esp_partition_erase_range(ota_.partition, 0, ota_.partition->size);
			if (ota_.error != ESP_OK) {
				ota_.errorMessage = "FS partition erase failure"sv;
				DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate Failed to erase SPIFFS partition!\n");
				return;
			}

			ota_.started = true;
		} else if (upload.status == UPLOAD_FILE_WRITE) {
			if (!ota_.started || ota_.error != ESP_OK) {
				if (!ota_.writeErrorReported) {
					DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate writing skipped, OTA error: %d\n", ota_.error);
					ota_.writeErrorReported = true;
				}
				return;
			}
			DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate writing SPIFFS offset: %zu, size: %zu\n", ota_.offset, upload.currentSize);

			ota_.error = esp_partition_write(ota_.partition, ota_.offset, upload.buf, upload.currentSize);
			if (ota_.error != ESP_OK) {
				ota_.errorMessage = "FS partition write error"sv;
				DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate Failed to write SPIFFS partition: %d\n", ota_.error);
				return;
			}
			ota_.offset += upload.currentSize;
		} else if (upload.status == UPLOAD_FILE_END) {
			if (!ota_.started || ota_.error != ESP_OK) {
				DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate upload end, error: %d\n", ota_.error);
				return;
			}
			DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate finished\n");
			ota_.success = true;
		} else if (upload.status == UPLOAD_FILE_ABORTED) {
			if (!ota_.started) {
				DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate upload aborted. Not started. Error: %d\n", ota_.error);
				return;
			}
			DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate ABORTED\n");
		}
	}

	struct OTAUpload {
		bool started = false;
		bool success = false;
		const esp_partition_t *partition = nullptr;
		size_t offset = 0;
		esp_ota_handle_t handle = 0;
		esp_err_t error = ESP_OK;
		std::string errorMessage;
		bool writeErrorReported = false;
	} ota_;


	std::shared_ptr<ib::logger::LoggerInterface> log_;
	ib::logger::LoggerInterface::LogFeatureType logFeature_;
	WebServerStringView server_;
	std::map<std::string, std::shared_ptr<ib::StatusReportingInterface>> reporters_;
	std::function<void()> calibrate_;
};

} // namespace intuibase

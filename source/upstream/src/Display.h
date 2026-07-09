// Display class for showing sensor data on a small TFT screen
#pragma once

#include "config.h"
#include <logger/LoggerInterface.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <Fonts/Org_01.h>
#include <memory>

#include "PeriodicCounter.h"

class Display {
public:
	struct TextPosition {
		int16_t x;
		int16_t y;
		uint16_t w;
		uint16_t h;
	};

	Display(std::shared_ptr<ib::logger::LoggerInterface> log, config::DisplayConfig const &config) : log_(std::move(log)) {
		logFeature_ = log_->addFeature("DISPLAY");
		DBGLOGFD(log_, logFeature_, "Display initialized with CS pin: %d, DC pin: %d, MOSI pin: %d, SCK pin: %d RST pin: %d\n", config.cs_pin, config.dc_pin, config.mosi_pin, config.sck_pin, config.reset_pin);
		if (config.software_spi) {
			DBGLOGFD(log_, logFeature_, "Using SPI pins MOSI: %d, SCK: %d\n", config.mosi_pin, config.sck_pin);
			tft_ = std::make_unique<Adafruit_GC9A01A>(config.cs_pin, config.dc_pin, config.mosi_pin, config.sck_pin, config.reset_pin);
		} else {
			SPI.begin(config.sck_pin, config.miso_pin, config.mosi_pin, config.cs_pin);
			tft_ = std::make_unique<Adafruit_GC9A01A>(&SPI, config.dc_pin, config.cs_pin, config.reset_pin);
		}

		logFeature_ = log_->addFeature("DISPLAY");
		begin();
	}

    constexpr static int16_t tempYpos_ = 130;
    constexpr static int16_t pressureYPos_ = 149;

	void begin() {
		tft_->begin();
		tft_->setRotation(0);
		tft_->fillScreen(GC9A01A_BLACK);

		drawBmp("/html/img/co2_h32.bmp", (tft_->width() - 47) / 2, 35);

		tft_->setFont(&Org_01);
		printText("Warming up", -1, 100, 3, GC9A01A_WHITE, nullptr);
	}



	void showSensorData(uint16_t co2ppm, double temperature, double humidity) {
		tft_->setFont(&Org_01);

		if (!iconDrawn_) {
			tft_->fillScreen(GC9A01A_BLACK);
			drawBmp("/html/img/co2_h32.bmp", (tft_->width() - 47) / 2, 35);
			drawBmp("/html/img/temp_8x16.bmp", 40, tempYpos_);
			drawBmp("/html/img/humidity_12x16.bmp", 155, tempYpos_);
			drawBmp("/html/img/pressure_16x16.bmp", 55, pressureYPos_);
			iconDrawn_ = true;
		}


		char buf[16];
		snprintf(buf, sizeof(buf), "%d", co2ppm);

		printText(buf, -1, 100, 7, GC9A01A_WHITE, &co2TextPos_);

        printText("ppm", -1, 120, 1, GC9A01A_WHITE, nullptr);

        snprintf(buf, sizeof(buf), "%.1f'C", temperature);
		printText(buf, 55, tempYpos_ + 11, 2, GC9A01A_WHITE, &temperatureTextPos_);

        snprintf(buf, sizeof(buf), "%2.0f%%", humidity);
		printText(buf, 170, tempYpos_ + 11, 2, GC9A01A_WHITE, &humidityTextPos_);

		int16_t color;
		if (co2ppm < co2range[0]) {
			color = co2rangeColor[0];
		} else if (co2ppm < co2range[1]) {
			color = co2rangeColor[1];
		} else if (co2ppm < co2range[2]) {
			color = co2rangeColor[2];
		} else if (co2ppm < co2range[3]) {
			color = co2rangeColor[3];
		} else {
			color = co2rangeColor[4];
		}

		drawStatusCircle(120, 120, 120, -15, color);
        showDateTime();
	}

    void showBmpSensorData(double pressure, double temperature) {
        tft_->setFont(&Org_01);

        int16_t color;
        if (pressure < 1000.0) {
            color = GC9A01A_BLUE;
        } else if (pressure < 1020.0) {
            color = GC9A01A_GREEN;
        } else if (pressure < 1040.0) {
            color = GC9A01A_YELLOW;
        } else {
            color = GC9A01A_RED;
        }
        tft_->setTextColor(color);

        char buf[16];
        snprintf(buf, sizeof(buf), "%.1fhPa", pressure);
        printText(buf, -1, pressureYPos_ + 11, 2, GC9A01A_WHITE, &bmpPressureTextPos_);
    }

private:
    void showDateTime() {
        // Get current time
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        int day = timeinfo.tm_mday;
        int month = timeinfo.tm_mon + 1;
        int year = timeinfo.tm_year + 1900;
        int hour = timeinfo.tm_hour;
        int minute = timeinfo.tm_min;
        int second = timeinfo.tm_sec;

        char buf[32];
        snprintf(buf, sizeof(buf), "%02d-%02d-%04d", day, month, year);
        printText(buf, -1, 185, 2, GC9A01A_WHITE, &dateTextPos_);
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour, minute, second);
        printText(buf, -1, 200, 2, GC9A01A_WHITE, &timeTextPos_);

    }


	void drawStatusCircle(int16_t centerX, int16_t centerY, float radius, int8_t offset, int16_t color) {
		for (int16_t angle = 0; angle < 360; angle += 3) {
			float rad = angle * DEG_TO_RAD;
			int16_t x = centerX + radius * cosf(rad);
			int16_t y = centerY + radius * sinf(rad);

			int16_t x2 = centerX + (radius + offset) * cosf(rad);
			int16_t y2 = centerY + (radius + offset) * sinf(rad);

			tft_->drawLine(x, y, x2, y2, color);
		}
	}

	void printText(const char *text, int16_t textx, int16_t texty, uint8_t fontSize, int16_t color = GC9A01A_WHITE, TextPosition *previousText = nullptr) {
		tft_->setTextColor(color, GC9A01A_BLACK);
		tft_->setTextSize(fontSize);
		// tft_->setFont(&Org_01);

		if (previousText) {
			// Clear previous text
			tft_->fillRect(previousText->x, previousText->y - previousText->h + fontSize, previousText->w, previousText->h, GC9A01A_BLACK);
		}

		int16_t x, y;
		uint16_t w, h;
		tft_->getTextBounds(text, 0, 0, &x, &y, &w, &h);

		if (textx < 0) {
			textx = (tft_->width() - (int16_t)w) / 2 - x; // Center horizontally
		}
		if (texty < 0) {
			texty = (tft_->height() - (int16_t)h) / 2 - y; // Center vertically
		}

		if (previousText) {
			// Update previous text position
			previousText->x = textx;
			previousText->y = texty;
			previousText->w = w;
			previousText->h = h;
		}

		tft_->setCursor(textx, texty);
		tft_->print(text);
	}

	//TODO add support for 16bit BMP images
	// Bodmer's BMP image rendering function
	void drawBmp(const char *filename, int16_t x, int16_t y) {
		fs::File bmpFS;
		bmpFS = SPIFFS.open(filename, "r");

		if (!bmpFS) {
			DBGLOGFD(log_, logFeature_, "BMP file not found: %s\n", filename);
			return;
		}

        DBGLOGFD(log_, logFeature_, "Loading BMP: %s\n", filename);
		uint32_t seekOffset;
		uint16_t w, h, row, col;
		uint8_t r, g, b;

		uint32_t startTime = millis();

		if (read16(bmpFS) == 0x4D42) {
			read32(bmpFS);
			read32(bmpFS);
			seekOffset = read32(bmpFS);
			read32(bmpFS);
			w = read32(bmpFS);
			h = read32(bmpFS);

			if ((read16(bmpFS) == 1) && (read16(bmpFS) == 24) && (read32(bmpFS) == 0)) {
				y += h - 1;

				bmpFS.seek(seekOffset);

				uint16_t padding = (4 - ((w * 3) & 3)) & 3;
				uint8_t lineBuffer[w * 3 + padding];

				for (row = 0; row < h; row++) {

					bmpFS.read(lineBuffer, sizeof(lineBuffer));
					uint8_t *bptr = lineBuffer;
					uint16_t *tptr = (uint16_t *)lineBuffer;
					// Convert 24 to 16-bit colours
					for (uint16_t col = 0; col < w; col++) {
						b = *bptr++;
						g = *bptr++;
						r = *bptr++;
						*tptr++ = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
					}

					// Push the pixel row to screen, pushImage will crop the line if needed
					// y is decremented as the BMP image is drawn bottom up
					// tft.pushImage(x, y--, w, 1, (uint16_t*)lineBuffer);
					tft_->drawRGBBitmap(x, y--, (uint16_t *)lineBuffer, w, 1);
				}
				//   tft.setSwapBytes(oldSwapBytes);
				DBGLOGFD(log_, logFeature_, "Loaded in %d ms\n", millis() - startTime);
			} else
				DBGLOGFD(log_, logFeature_, "BMP format not recognized.\n");
		}
		bmpFS.close();
	}

	// These read 16- and 32-bit types from the SD card file.
	// BMP data is stored little-endian, Arduino is little-endian too.
	// May need to reverse subscript order if porting elsewhere.

	uint16_t read16(fs::File &f) {
		uint16_t result;
		((uint8_t *)&result)[0] = f.read(); // LSB
		((uint8_t *)&result)[1] = f.read(); // MSB
		return result;
	}

	uint32_t read32(fs::File &f) {
		uint32_t result;
		((uint8_t *)&result)[0] = f.read(); // LSB
		((uint8_t *)&result)[1] = f.read();
		((uint8_t *)&result)[2] = f.read();
		((uint8_t *)&result)[3] = f.read(); // MSB
		return result;
	}

	std::shared_ptr<ib::logger::LoggerInterface> log_;
	ib::logger::LoggerInterface::LogFeatureType logFeature_;
	std::unique_ptr<Adafruit_GC9A01A> tft_;

	uint16_t co2range[5] = {600, 1000, 1200, 1600, 2000};
	uint16_t co2rangeColor[5] = {GC9A01A_GREEN, GC9A01A_GREENYELLOW, GC9A01A_YELLOW, tft_->color565(230, 138, 0), GC9A01A_RED};

	TextPosition co2TextPos_ = {0, 0, 0, 0};
	TextPosition temperatureTextPos_ = {0, 0, 0, 0};
	TextPosition humidityTextPos_ = {0, 0, 0, 0};

    TextPosition bmpPressureTextPos_ = {0, 0, 0, 0};
    TextPosition bmpTemperatureTextPos_ = {0, 0, 0, 0};

    TextPosition dateTextPos_ = {0, 0, 0, 0};
    TextPosition timeTextPos_ = {0, 0, 0, 0};

	bool iconDrawn_ = false;
};

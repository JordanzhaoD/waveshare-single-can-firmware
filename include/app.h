#pragma once

#include <memory>
#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "can_helpers.h"
#include "handlers.h"

#ifndef NATIVE_BUILD
#ifdef ESP_PLATFORM
#include "platform/espidf_runtime.h"
#else
#include <Arduino.h>
#endif
#endif
#if defined(DASH_RGB_STATUS_LED) && !defined(NATIVE_BUILD) && !defined(ESP_PLATFORM)
#include <esp32-hal-rgb-led.h>
#endif
#if defined(DASH_RGB_STATUS_LED) && defined(ESP_PLATFORM)
#include <led_strip.h>
#endif

#ifndef PIN_LED
#define PIN_LED 2
#endif

#if defined(ESP32_DASHBOARD)
#if DASH_DEFAULT_HW == 0
using SelectedHandler = LegacyHandler;
#elif DASH_DEFAULT_HW == 2
using SelectedHandler = HW4Handler;
#else
using SelectedHandler = HW3Handler;
#endif
#elif defined(NAG_KILLER)
using SelectedHandler = NagHandler;
#elif defined(HW4)
using SelectedHandler = HW4Handler;
#elif defined(HW3)
using SelectedHandler = HW3Handler;
#elif defined(LEGACY)
using SelectedHandler = LegacyHandler;
#else
#error "Define HW4, HW3, LEGACY, or NAG_KILLER in build_flags"
#endif

static std::unique_ptr<CanDriver> appDriver;
static std::unique_ptr<CarManagerBase> appHandler;
static CarManagerBase *appActiveHandler = nullptr;

#if defined(ESP_PLATFORM) && defined(DRIVER_TWAI)
static volatile bool appCanTaskDedicated = false;
static volatile uint32_t appCanTaskLoops = 0;
static volatile uint32_t appCanTaskIdleLoops = 0;
#endif

static volatile bool frameReady = true;
static void canISR() { frameReady = true; }

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_RGB_STATUS_LED)
static void appRefreshStatusLed(bool force = false);
static void appWriteStatusLed(uint8_t red, uint8_t green, uint8_t blue);
#endif
#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_INJECTION_TOGGLE_PIN)
static void appPollInjectionToggleButton();
#endif

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)
#include "web/mcp2515_dashboard.h"
#endif

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_RGB_STATUS_LED)
static void appWriteStatusLed(uint8_t red, uint8_t green, uint8_t blue)
{
#ifdef ESP_PLATFORM
    static led_strip_handle_t strip = nullptr;
    if (!strip)
    {
        led_strip_config_t scfg = {};
        scfg.strip_gpio_num = PIN_LED;
        scfg.max_leds = 1;
        scfg.led_model = LED_MODEL_WS2812;
        scfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
        led_strip_rmt_config_t rcfg = {};
        rcfg.resolution_hz = 10 * 1000 * 1000;
        if (led_strip_new_rmt_device(&scfg, &rcfg, &strip) != ESP_OK)
            return;
    }
    led_strip_set_pixel(strip, 0, red, green, blue);
    led_strip_refresh(strip);
#elif defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    rgbLedWrite(PIN_LED, red, green, blue);
#else
    neopixelWrite(PIN_LED, red, green, blue);
#endif
}

static void appRefreshStatusLed(bool force)
{
    static bool known = false;
    static bool lastInjecting = false;
    static bool lastConnected = false;
    static bool lastOta = false;
    static uint8_t lastLevel = 0;
    static bool lastEmittedOn = true;

    bool injecting = canActive;
    bool connected = (WiFi.softAPgetStationNum() > 0) || (WiFi.status() == WL_CONNECTED);
    bool ota = Update.isRunning();
    uint8_t level = dashLedBrightness;

    // Solid when connected (or OTA), 1 Hz blink otherwise.
    bool blinkOn = (millis() % 1000UL) < 500UL;
    bool emittedOn = (connected || ota) ? true : blinkOn;

    if (!force && known && lastInjecting == injecting && lastConnected == connected && lastOta == ota && lastLevel == level && lastEmittedOn == emittedOn)
        return;

    uint8_t r = 0, g = 0, b = 0;
    if (emittedOn)
    {
        if (ota)
            b = level; // OTA: solid blue
        else if (injecting)
            g = level;
        else
            r = level;
    }
    appWriteStatusLed(r, g, b);

    lastInjecting = injecting;
    lastConnected = connected;
    lastOta = ota;
    lastLevel = level;
    lastEmittedOn = emittedOn;
    known = true;
}
#endif

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_INJECTION_TOGGLE_PIN)
static void appPollInjectionToggleButton()
{
    static bool rawState = HIGH;
    static bool stableState = HIGH;
    static unsigned long lastChangeMs = 0;

    bool sample = digitalRead(DASH_INJECTION_TOGGLE_PIN);
    unsigned long now = millis();

    if (sample != rawState)
    {
        rawState = sample;
        lastChangeMs = now;
    }

    if ((now - lastChangeMs) < 35 || sample == stableState)
        return;

    stableState = sample;
    if (stableState == LOW)
        dashToggleCanActive("GPIO41");
}
#endif

template <typename Driver>
static void appSetup(std::unique_ptr<Driver> drv, const char *readyMsg)
{
    appHandler = std::make_unique<SelectedHandler>();
    appActiveHandler = appHandler.get();
    delay(1500);
    Serial.begin(115200);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 1000)
    {
    }

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_INJECTION_TOGGLE_PIN)
    pinMode(DASH_INJECTION_TOGGLE_PIN, INPUT_PULLUP);
#endif

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_RGB_STATUS_LED)
    appRefreshStatusLed(true);
#else
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);
#endif

    appDriver = std::move(drv);
    if (!appDriver->init())
    {
        Serial.println("CAN init failed");
    }

    appDriver->setFilters(appHandler->filterIds(), appHandler->filterIdCount());
    if constexpr (Driver::kSupportsISR)
    {
        appDriver->enableInterrupt(canISR);
    }

    Serial.println(readyMsg);

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)
    delay(2000);
#endif
}

template <typename Driver>
static bool appLoop()
{
#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_RGB_STATUS_LED)
    appRefreshStatusLed(false);
#endif
#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)
    if (Update.isRunning())
    {
        delay(1);
        return false;
    }

#if defined(DASH_INJECTION_TOGGLE_PIN)
    appPollInjectionToggleButton();
#endif
#endif

    if constexpr (Driver::kSupportsISR)
    {
        if (!frameReady)
            return false;
        frameReady = false;
    }

    CanFrame frame;
    CarManagerBase *h = appActiveHandler ? appActiveHandler : appHandler.get();
    uint8_t framesThisLoop = 0;
    bool processedFrame = false;
    while (appDriver->read(frame))
    {
        processedFrame = true;
        if (frame.bus == CAN_BUS_ANY)
            frame.bus = CAN_BUS_DEFAULT;
#if !(defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_RGB_STATUS_LED))
        digitalWrite(PIN_LED, LOW);
#endif
        h->frameCount++;
#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)
        CanFrame original = frame;
#endif
        h->handleMessage(frame, *appDriver);
#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_PLUGIN_ENGINE)
        DashPluginApplyResult pluginApplied = dashPluginEngine.applyToFrame(original, dashPluginContext(), *appDriver);
        if (pluginApplied.sent)
            lastInjectMs = millis();
#endif
#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)
        dashPostProcessFrame(original, *appDriver);
#endif
#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)
        if (++framesThisLoop >= 32)
        {
            yield();
            break;
        }
#endif
    }
#if !(defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_RGB_STATUS_LED))
    digitalWrite(PIN_LED, HIGH);
#endif
    h->tick(dashDiagNowMs(), *appDriver);
#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD) && defined(DASH_PLUGIN_ENGINE)
    dashPluginEngine.tickPeriodic(millis(), dashPluginContext(), *appDriver);
#endif
    return processedFrame;
}

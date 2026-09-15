#pragma once

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiServer.h>

#include "ConfigStore.h"
#include "RtcManager.h"

enum class WebPortalRequest : uint8_t {
    NONE,
    FORCE_OPEN,
    FORCE_CLOSE,
    NAP,
};

// SoftAP + self-contained config web page. Instantiated for a bounded
// duration on reset, setup, unknown, and WiFi-only wakes. Open/Close
// requests arm a deferred action wake and therefore stop being reachable when
// the portal is torn down.
class WebPortal {
public:
    WebPortal(ConfigStore& store, RtcManager& rtc);

    // Starts the AP, serves the page for durationMs, then tears the AP down
    // and returns the requested action. The caller owns all sleep and door
    // execution decisions.
    WebPortalRequest run(unsigned long durationMs);

private:
    void setupRoutes();
    void handleRoot();
    void handleSaveConfig();
    void handleSetTime();
    void handleForceOpen();
    void handleForceClose();
    void handleSleepNow();
    void handleNapNow();
    void handlePing();
    void redirectToRoot();

    String buildIndexHtml();

    ConfigStore& store_;
    RtcManager& rtc_;
    WebServer server_;
    DNSServer dnsServer_;
    WiFiServer httpsStub_;
    Config cfg_;
    String statusMessage_;
    WebPortalRequest request_ = WebPortalRequest::NONE;
    bool stopRequested_ = false;
};

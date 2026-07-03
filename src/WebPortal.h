#pragma once

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiServer.h>

#include "ConfigStore.h"
#include "DoorController.h"
#include "RtcManager.h"

// SoftAP + self-contained config web page. Only ever instantiated and run
// once, for a bounded duration, right after a power-on/reset boot. Force
// Open/Close exist only as routes on this server, so they structurally stop
// being reachable the moment the portal is torn down.
class WebPortal {
public:
    WebPortal(ConfigStore& store, RtcManager& rtc, DoorController& door);

    // Starts the AP, serves the page for durationMs, then tears the AP down
    // and returns. Blocking.
    void run(unsigned long durationMs);

private:
    void setupRoutes();
    void handleRoot();
    void handleSaveConfig();
    void handleSetTime();
    void handleForceOpen();
    void handleForceClose();
    void redirectToRoot();

    String buildIndexHtml();

    ConfigStore& store_;
    RtcManager& rtc_;
    DoorController& door_;
    WebServer server_;
    DNSServer dnsServer_;
    WiFiServer httpsStub_;
    Config cfg_;
    String statusMessage_;
};

#pragma once

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiServer.h>

#include "Config.h"
#include "SleepManager.h"

enum class WebPortalRequest : uint8_t {
    NONE,
    FORCE_OPEN,
    FORCE_CLOSE,
    NAP,
};

class WebPortal {
public:
    WebPortal(Config& config, SleepManager& sleep_manager);

    // Serve the portal for a bounded time.
    WebPortalRequest run(unsigned long duration_ms);

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

    Config& config_;
    SleepManager& sleep_manager_;
    WebServer server_;
    DNSServer dnsServer_;
    WiFiServer httpsStub_;
    String statusMessage_;
    WebPortalRequest request_ = WebPortalRequest::NONE;
    bool stopRequested_ = false;
};
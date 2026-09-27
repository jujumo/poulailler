#pragma once

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiServer.h>

#include "Config.h"
#include "SleepManager.h"
#include "Scheduler.h"



class WebPortal {
public:
    WebPortal( Config& config, 
               SleepManager& sleep_manager,
               Scheduler& scheduler
            );

    // Serve the portal for a bounded time.
    void run(unsigned long duration_ms);

private:
    void setupRoutes();
    void handleRoot();
    void handleResetScheduler();
    void handleUpdateScheduler();
    void handleSaveConfig();
    void handleSetTime();
    void handleForceOpen();
    void handleForceClose();
    void handleSleepNow();
    void handleNapNow();
    void handlePing();
    void redirectToRoot();

    String buildIndexHtml();
    
    // may act directly on config_, if config is changed by user.
    Config& config_;
    SleepManager& sleep_manager_;
    // may act directly on scheduler_ e.g. if config is changed.
    Scheduler& scheduler_;

    WebServer server_;
    DNSServer dnsServer_;
    WiFiServer httpsStub_;
    String statusMessage_;
    bool stopRequested_ = false;
};
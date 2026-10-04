// Wi-Fi + JMRI connection for the throttles, without any UI: joins Wi-Fi,
// finds the WiThrottle server (a manual host from the setup portal, else
// mDNS), connects, and climbs the reconnect ladder when the link drops.
//
// Non-blocking apart from the mDNS query (about 3 s). Call tick() every loop
// and show statusText() while state() is not Online.

#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiThrottleProtocol.h>

#include "CommonConfig.h"
#include "Provision.h"

class Link {
  public:
    enum class State : uint8_t {
        Idle,
        WifiConnecting,
        Discovering,
        Connecting,
        Online,
        Backoff,
        NeedsSetup,   // Wi-Fi never came up: run the setup portal
    };

    Link(WiThrottleProtocol &wit, WiThrottleProtocolDelegate &delegate)
        : wit_(wit), delegate_(delegate) {}

    // deviceName is sent to JMRI as the throttle's name and ID.
    void begin(const Provision::Stored &cfg, const String &deviceName);
    void tick();

    State state() const { return state_; }
    bool online() const { return state_ == State::Online; }
    const String &statusText() const { return status_; }
    const String &serverLabel() const { return serverLabel_; }
    uint16_t serverPort() const { return serverPort_; }

    // True once each time the link comes (back) online, so the app can
    // re-acquire its loco.
    bool takeOnline() {
        const bool r = justOnline_;
        justOnline_ = false;
        return r;
    }

  private:
    void startWifi();
    bool resolveManualHost();
    bool discover();
    bool connectServer();
    void dropped();

    WiThrottleProtocol         &wit_;
    WiThrottleProtocolDelegate &delegate_;
    WiFiClient                  tcp_;
    Provision::Stored           cfg_;
    String                      deviceName_;
    State                       state_ = State::Idle;
    String                      status_;
    String                      serverLabel_;
    IPAddress                   serverIp_;
    uint16_t                    serverPort_ = DEFAULT_WITHROTTLE_PORT;
    uint32_t                    stateAt_ = 0;
    uint32_t                    lastAttempt_ = 0;
    size_t                      backoffStep_ = 0;
    bool                        justOnline_ = false;
    bool                        mdnsStarted_ = false;
};

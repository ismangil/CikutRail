#include "Link.h"

#include <ESPmDNS.h>

void Link::begin(const Provision::Stored &cfg, const String &deviceName) {
    cfg_ = cfg;
    deviceName_ = deviceName;
    startWifi();
}

void Link::startWifi() {
    state_ = State::WifiConnecting;
    stateAt_ = millis();
    status_ = "Wi-Fi: " + cfg_.ssid;
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg_.ssid.c_str(), cfg_.password.c_str());
}

bool Link::resolveManualHost() {
    serverLabel_ = cfg_.host;
    serverPort_ = cfg_.port ? cfg_.port : DEFAULT_WITHROTTLE_PORT;
    if (serverIp_.fromString(cfg_.host)) return true;
    IPAddress resolved;
    if (WiFi.hostByName(cfg_.host.c_str(), resolved)) {
        serverIp_ = resolved;
        return true;
    }
    return false;
}

bool Link::discover() {
    if (!mdnsStarted_) {
        mdnsStarted_ = MDNS.begin(deviceName_.c_str());
        if (!mdnsStarted_) return false;
    }
    const int n = MDNS.queryService(MDNS_SERVICE_NAME, MDNS_SERVICE_PROTO);
    if (n <= 0) return false;
    // First hit wins; a manual host in the portal overrides this.
#if ESP_IDF_VERSION_MAJOR < 5
    serverIp_ = MDNS.IP(0);       // Arduino-ESP32 2.x
#else
    serverIp_ = MDNS.address(0);  // Arduino-ESP32 3.x
#endif
    serverPort_ = MDNS.port(0);
    serverLabel_ = MDNS.hostname(0);
    return true;
}

bool Link::connectServer() {
    if (!tcp_.connect(serverIp_, serverPort_)) return false;
    wit_.setDelegate(&delegate_);
    wit_.connect(&tcp_);
    wit_.setDeviceName(deviceName_);
    wit_.setDeviceID(deviceName_);
    return true;
}

void Link::dropped() {
    state_ = State::Backoff;
    backoffStep_ = 0;
    lastAttempt_ = 0;
    status_ = "Link lost";
}

void Link::tick() {
    const uint32_t now = millis();
    switch (state_) {
        case State::Idle:
        case State::NeedsSetup:
            break;

        case State::WifiConnecting:
            if (WiFi.status() == WL_CONNECTED) {
                if (cfg_.host.length()) {
                    if (resolveManualHost()) {
                        state_ = State::Connecting;
                        status_ = serverLabel_;
                    } else {
                        status_ = "DNS failed: " + cfg_.host;
                        state_ = State::Discovering;  // fall back to mDNS
                    }
                } else {
                    state_ = State::Discovering;
                    status_ = "Looking for JMRI";
                    lastAttempt_ = 0;
                }
            } else if (now - stateAt_ > WIFI_CONNECT_TIMEOUT_MS) {
                state_ = State::NeedsSetup;
                status_ = "Wi-Fi failed";
            }
            break;

        case State::Discovering:
            if (lastAttempt_ == 0 || now - lastAttempt_ > 2500) {
                lastAttempt_ = now;
                if (discover()) {
                    state_ = State::Connecting;
                    status_ = serverLabel_;
                } else {
                    status_ = "No JMRI found";
                }
            }
            break;

        case State::Connecting:
            if (connectServer()) {
                state_ = State::Online;
                backoffStep_ = 0;
                justOnline_ = true;
                status_ = "";
            } else {
                dropped();
            }
            break;

        case State::Online:
            if (!tcp_.connected()) dropped();
            break;

        case State::Backoff: {
            const uint32_t wait = RECONNECT_BACKOFF_MS[
                backoffStep_ < RECONNECT_BACKOFF_COUNT
                    ? backoffStep_ : RECONNECT_BACKOFF_COUNT - 1];
            if (lastAttempt_ != 0 && now - lastAttempt_ < wait) break;
            lastAttempt_ = now;
            status_ = "Reconnecting";
            if (WiFi.status() != WL_CONNECTED) {
                WiFi.reconnect();
            } else if (serverIp_ != IPAddress((uint32_t)0) && connectServer()) {
                state_ = State::Online;
                backoffStep_ = 0;
                justOnline_ = true;
                status_ = "";
                break;
            }
            if (backoffStep_ + 1 < RECONNECT_BACKOFF_COUNT) backoffStep_++;
            break;
        }
    }
}

#ifndef MQTT_CLIENT_TEST_ENVIRONMENT_H
#define MQTT_CLIENT_TEST_ENVIRONMENT_H

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace mqtt_test {

inline std::string envOr(const char* name, const char* fallback) {
    if (const char* value = std::getenv(name); value && *value) {
        return value;
    }
    return fallback;
}

inline int envPort(const char* name, const int fallback) {
    const std::string value = envOr(name, "");
    if (value.empty()) {
        return fallback;
    }
    const int port = std::stoi(value);
    if (port <= 0 || port > 65535) {
        throw std::out_of_range(std::string(name) + " must be in range 1..65535");
    }
    return port;
}

inline bool envBool(const char* name, const bool fallback) {
    const std::string value = envOr(name, "");
    if (value.empty()) {
        return fallback;
    }
    if (value == "1" || value == "true" || value == "TRUE" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "FALSE" || value == "off") {
        return false;
    }
    throw std::invalid_argument(std::string(name) + " must be a boolean value");
}

} // namespace mqtt_test

#endif // MQTT_CLIENT_TEST_ENVIRONMENT_H

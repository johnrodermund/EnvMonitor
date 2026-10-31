#pragma once

#include <cstring>

inline bool validWifiCredentials(const char *ssid, const char *password) {
  size_t passwordLength = strlen(password);
  bool passwordOk = passwordLength == 0 || (passwordLength >= 8 && passwordLength <= 63);
  return strlen(ssid) > 0 && strlen(ssid) <= 32 && passwordOk;
}

inline bool setupPortalFinished(bool saved, bool hasSavedNetwork, unsigned long elapsed, unsigned long timeout) {
  return saved || (hasSavedNetwork && elapsed > timeout);
}

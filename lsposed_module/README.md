# COPG-VD LSPosed Module

Hooks Android system framework (`android`) to spoof device identity at the Java layer.

## Scope
- `android` (system framework / system_server)

## Hooks
- `Build.*` — MODEL, MANUFACTURER, FINGERPRINT, etc.
- `TelephonyManager.*` — SIM country, operator, MCC/MNC
- `WifiInfo.getSSID()` — WiFi SSID
- `MediaDrm.*` — Widevine ID + security level

## Build
```bash
./gradlew assembleRelease
```

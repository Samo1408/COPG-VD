# COPG-VD LSPosed Module

Hooks **system_server** to spoof device identity for ALL apps — no hooks in app processes.

## Architecture

```
┌──────────────────────────────────────────────────┐
│  App (any app)                                   │
│  • Reads Build.MODEL → gets spoofed value        │
│  • Reads TelephonyManager → gets spoofed SIM     │
│  • Reads WifiInfo.getSSID() → gets spoofed SSID  │
│                                                  │
│  ❌ No hooks in this process — zero footprint    │
└──────────────┬───────────────────────────────────┘
               │ Binder IPC (normal Android IPC)
┌──────────────▼───────────────────────────────────┐
│  system_server (android package)                 │
│  • COPGVDXposed hooks run here                   │
│  • Build static fields set here                  │
│  • TelephonyManager hooks here                   │
│  • WifiInfo hooks here                           │
│                                                  │
│  ✅ LSPosed scope: System Framework (android)    │
└──────────────────────────────────────────────────┘
```

## How it works

Same approach as vpnhide:
- Module is activated in LSPosed with scope: **System Framework** only
- Hooks run inside `system_server` (the Android system process)
- When apps query device info via normal Binder IPC, they receive spoofed data
- No Xposed hooks are injected into any app's process — anti-tamper SDKs cannot detect anything

## Scope

**System Framework** (`android` package) — nothing else.

## Hooks

| Service | What's hooked | Effect |
|---------|--------------|--------|
| `android.os.Build` | Static fields (MODEL, MANUFACTURER, etc.) | All apps see spoofed device profile |
| `android.telephony.TelephonyManager` | `getNetworkCountryIso()` | All apps see spoofed SIM country |
| `android.net.wifi.WifiInfo` | `getSSID()` | All apps see spoofed WiFi SSID |

## Build

```bash
./gradlew assembleRelease
```

## Install & Activate

1. Build and install APK
2. Open LSPosed Manager → Modules → **COPG-VD Spoof**
3. Enable module, scope: **System Framework** (android)
4. Reboot
5. Verify: `adb logcat | grep COPG-VD`

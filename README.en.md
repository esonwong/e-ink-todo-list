# E-Ink Todo List

[![PlatformIO CI](https://github.com/esonwong/e-ink-todo-list/actions/workflows/tag.yml/badge.svg)](https://github.com/esonwong/e-ink-todo-list/actions/workflows/tag.yml)

Use the API provided by [einktodo.com](https://einktodo.com) to display todo items on an e-ink screen.

[中文](README.md)

## Tutorial

<https://blog.esonwong.com/e-ink-screen-todo-list-creation-turial>

## Button gestures

- Single click: closes the config portal if open; otherwise forces an immediate refresh on a connected device.
- Double click: notifies the server with `X-Device-Event: double-click`. The actual action (defaults to cycling display mode: todo / agent / image) is decided server-side and can be changed without reflashing.
- Long press for ~6 seconds: clears configuration + restarts into config mode.

## TODO

- [ ] Support ESP8266 battery voltage detection
- [ ] Configuration interface style
- [ ] QR code for configuring AP connection
- [x] CI/CD automatic firmware release
- [x] Automatic firmware update
- [x] Automatic update of SSL/TLS root certificates
- [x] Startup screen
- [x] Record the last update check time
- [x] Support ESP8266
- [x] Support Waveshare first generation 7.5-inch e-ink screen
- [x] Cache todo items

## Production

```sh
pio run --target erase --target upload --target monitor --environment production
```

## Network Fallback

The device still prefers the primary API domain, but it can automatically retry against one or more compile-time HTTPS fallback endpoints when the primary entry is unreachable.

Current behavior:

- The primary domain keeps using CertStore CA validation
- Fallback IP endpoints use certificate fingerprint pinning
- The firmware remembers the last successful endpoint and tries it first on the next request
- Display fetches, asset updates, and OTA updates all share the same fallback flow

`platformio.ini` includes an example of the build flags:

```ini
; -D FALLBACK_BASE_URL_1="https://107.173.82.8:16845"
; -D FALLBACK_TLS_FINGERPRINT_1="AA BB CC DD EE FF 00 11 22 33 44 55 66 77 88 99 AA BB CC DD"
```

For real testing, override these values in a local environment file such as `local.ini`.

Verified result:

- After the primary entry failed, the device retried against the fallback HTTPS IP
- The fallback endpoint returned 200 and successfully served `certs.ar`
- This behavior was verified on real hardware
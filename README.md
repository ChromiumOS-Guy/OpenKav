# OpenKav

**OpenKav** is a reverse-engineered client for the RavKavOnline system. Its purpose is to proxy NFC APDU commands between official RavKavOnline servers and the physical smartcard.

This project was created to provide support for platforms where the official client is unavailable, such as mobile Linux distributions (e.g., Ubuntu Touch, PostmarketOS) and other `aarch64` or `armhf` based hardware.

The project is licensed under GPLv3.

---

## How to Use

OpenKav acts as a direct bridge between the RavKavOnline infrastructure and the NFC-enabled smartcard. Because it functions as a drop-in replacement for the official client, you simply need to capture the WebSocket handshake from the web interface to initiate the proxy session.

### Execution

Run the application by passing the captured `ravkav:` URI scheme directly as an argument:

```bash
./openkav ravkav:wss://proxy.ravkavonline.co.il/api/proxy/xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx

```

---

## Logging

OpenKav logs are sanitized by default. If you need to troubleshoot connection or APDU issues, you can enable verbose logging:

```bash
export QT_LOGGING_RULES="*.debug=true"
```

> **WARNING:** Debug logs contain sensitive data, including card identifiers and raw transaction bytes. **Always sanitize sensitive information** before sharing these logs online. I strongly recommend to use an anonymous RavKav card if you intend to share logs for debugging, anonymous RavKav cards cost 5 NIS.

---

## Build Instructions

### Dependencies

Before building, ensure you have the necessary Qt6 development libraries installed on your system:

<details><summary>Debian</summary>

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-connectivity-dev qt6-websockets-dev

```

</details>


### Building for Native Architecture

```bash
mkdir build && cd build
cmake ..
make

```

---

## Architecture

The application acts as a middleman, handling the WebSocket handshake with the RavKavOnline servers and serializing/deserializing the ISO 7816 APDU commands directly to the NFC smartcard reader.






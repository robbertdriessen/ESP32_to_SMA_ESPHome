ESP Logger (UDP syslog-ish) with Web UI

Overview
- Receives UDP log lines from your ESP32 (e.g., via UDPAppender) on port 1514/udp
- Serves a simple web UI to view logs and a live stream via Server-Sent Events
- No database; in-memory ring buffer (configurable)

Quick start
1) Build and run
   docker compose up -d

2) Open the UI
   http://localhost:8080/

3) Point the ESP to the logger
   - Set SYSLOG_HOST to the machine running Docker
   - Set SYSLOG_PORT to 1514 (or adjust docker-compose env/ports)

Config
- HTTP_PORT: HTTP listen port (default 8080)
- UDP_PORT: UDP listen port for logs (default 1514)
- LOG_BUFFER: number of lines kept in memory (default 10000)

API
- GET /          -> Web UI
- GET /logs?limit=N  -> last N lines (text/plain)
- GET /stream    -> Server-sent events live stream
- GET /healthz   -> health status JSON

Notes
- For remote clients, ensure your firewall allows UDP 1514 and TCP 8080.
- If you already run a syslog server, you can have the ESP send UDP to that and tail forward to this app instead.

import logging
import os
import queue
import socket
import threading
from datetime import datetime
from collections import deque

from flask import Flask, request, Response, jsonify
from waitress import serve

HTTP_PORT = int(os.getenv("HTTP_PORT", 8080))
UDP_PORT = int(os.getenv("UDP_PORT", 1514))
LOG_BUFFER = int(os.getenv("LOG_BUFFER", 10000))
LOG_DIR = os.getenv("LOG_DIR", "/logs")

app = Flask(__name__)

# Ring buffer of log lines
log_lines = deque(maxlen=LOG_BUFFER)
log_lock = threading.Lock()

# Optional: in-memory subscriber queues for SSE
subscribers = set()
subs_lock = threading.Lock()

# Persistent file writer state (rotates daily into LOG_DIR/YYYY/MM/DD.log)
file_lock = threading.Lock()
current_file = None
current_file_date = None


def write_to_file(line: str, now: datetime) -> None:
    global current_file, current_file_date
    date_key = (now.year, now.month, now.day)
    with file_lock:
        if date_key != current_file_date:
            if current_file is not None:
                try:
                    current_file.close()
                except Exception:
                    pass
            day_dir = os.path.join(LOG_DIR, f"{now.year:04d}", f"{now.month:02d}")
            os.makedirs(day_dir, exist_ok=True)
            path = os.path.join(day_dir, f"{now.day:02d}.log")
            current_file = open(path, "a", encoding="utf-8")
            current_file_date = date_key
        try:
            current_file.write(line + "\n")
            current_file.flush()
        except Exception as e:
            print(f"[file] write error: {e}")


def format_line(data: bytes, addr, now: datetime) -> str:
    try:
        text = data.decode("utf-8", errors="replace").strip()
    except Exception:
        text = repr(data)
    ts = now.strftime("%Y-%m-%dT%H:%M:%S.%fZ")
    return f"{ts} {addr[0]}:{addr[1]} {text}"


def udp_listener():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", UDP_PORT))
    print(f"[udp] listening on 0.0.0.0:{UDP_PORT}")
    print(f"[file] writing daily logs under {LOG_DIR}/YYYY/MM/DD.log")
    while True:
        try:
            data, addr = sock.recvfrom(65535)
            now = datetime.utcnow()
            line = format_line(data, addr, now)
            write_to_file(line, now)
            with log_lock:
                log_lines.append(line)
            # fan out to subscribers
            with subs_lock:
                dead = []
                for q in subscribers:
                    try:
                        q.put_nowait(line)
                    except queue.Full:
                        dead.append(q)
                for q in dead:
                    subscribers.discard(q)
        except Exception as e:
            print(f"[udp] error: {e}")


@app.get("/")
def index():
    return (
        """
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <title>ESP Log Viewer</title>
  <style>
    body { font-family: monospace; background: #111; color: #ddd; margin:0; }
    #toolbar { position:fixed; top:0; left:0; right:0; background:#222; padding:8px; }
    #content { margin-top:48px; padding:8px; white-space: pre; overflow-wrap: anywhere; }
    .btn { padding:6px 10px; background:#444; color:#eee; border:none; cursor:pointer; margin-right:6px; }
  </style>
</head>
<body>
    <div id="toolbar">
    <button class="btn" onclick="refresh()">Refresh</button>
    <button class="btn" onclick="toggleLive()" id="livebtn">Start Live</button>
    <span id="status"></span>
  </div>
  <div id="content"></div>
<script>
var es = null;
window.refresh = function() {
  fetch('/logs?limit=500')
    .then(r => r.text())
    .then(t => { document.getElementById('content').textContent = t; window.scrollTo(0, document.body.scrollHeight);})
}
window.toggleLive = function(){
  if (es) { es.close(); es = null; document.getElementById('livebtn').innerText='Start Live'; return; }
    es = new EventSource('/stream');
    es.onmessage = (e)=>{
        const c = document.getElementById('content');
        c.textContent += (c.textContent ? '\\n' : '') + e.data;
        window.scrollTo(0, document.body.scrollHeight);
    };
  es.onerror = ()=>{ es && es.close(); es=null; document.getElementById('livebtn').innerText='Start Live'; };
  document.getElementById('livebtn').innerText='Stop Live';
}
refresh();
</script>
</body>
</html>
""",
        200,
        {"Content-Type": "text/html; charset=utf-8"},
    )


@app.get("/logs")
def get_logs():
    limit = int(request.args.get("limit", 1000))
    with log_lock:
        data = list(log_lines)[-limit:]
    return Response("\n".join(data) + ("\n" if data else ""), mimetype="text/plain")


@app.get("/healthz")
def health():
    return jsonify({"status": "ok", "lines": len(log_lines)})


@app.get("/stream")
def stream():
    q = queue.Queue(maxsize=1000)
    with subs_lock:
        subscribers.add(q)

    def gen():
        try:
            # push last 100 lines as initial burst
            with log_lock:
                tail = list(log_lines)[-100:]
            for line in tail:
                yield f"data: {line}\n\n"
            while True:
                line = q.get()
                yield f"data: {line}\n\n"
        except GeneratorExit:
            pass
        finally:
            with subs_lock:
                subscribers.discard(q)

    return Response(gen(), mimetype="text/event-stream")


@app.get("/favicon.ico")
def favicon():
    return ("", 204)


def main():
    t = threading.Thread(target=udp_listener, daemon=True)
    t.start()
    print(f"[http] starting on 0.0.0.0:{HTTP_PORT}")
    serve(app, host="0.0.0.0", port=HTTP_PORT)


if __name__ == "__main__":
    main()

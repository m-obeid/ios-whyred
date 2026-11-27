#!/usr/bin/env python3
"""
Remote control page for the phone: the laptop webcam pointed at the phone, a touch pad and buttons.
Taps and swipes go to kern.whyred_touch (0980), buttons to kern.whyred_key (1037), over SSH.
Run on the laptop: whyred/tools/remote/remote.py [--port 8765] [--bind 0.0.0.0]
Open http://<laptop tailscale IP>:8765 from another device on the tailnet.
The page has no password: bind it to the Tailscale address or localhost only.
"""
import argparse
import glob
import io
import http.server
import json
import os
import subprocess
import threading
import time
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
PHONE = os.environ.get("PHONE", "root@192.168.7.2")
CTL = "/tmp/whyred-remote-ssh-%r@%h"
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "-o", "ControlMaster=auto",
       "-o", "ControlPath=" + CTL, "-o", "ControlPersist=600", "-x", PHONE]
PANEL_W, PANEL_H = 1080, 2160
KEYS = {"power": 0, "volup": 1, "voldown": 2, "home": 3}

frame_lock = threading.Condition()
frame = {"jpg": b"", "n": 0, "t": 0.0}
cam = {"dev": "/dev/video0", "exp": 400, "gain": 6, "fps": 10, "proc": None}


def phone(cmd, timeout=10):
    try:
        r = subprocess.run(SSH + [cmd], capture_output=True, text=True, timeout=timeout)
        return r.returncode, (r.stdout + r.stderr).strip()
    except subprocess.TimeoutExpired:
        return -1, "timeout"


def touch(x1, y1, x2, y2, ms):
    clamp = lambda v, hi: max(0, min(int(v), hi - 1))
    x1, x2 = clamp(x1, PANEL_W), clamp(x2, PANEL_W)
    y1, y2 = clamp(y1, PANEL_H), clamp(y2, PANEL_H)
    ms = max(16, min(int(ms), 5000))
    v = x1 | y1 << 12 | x2 << 24 | y2 << 36 | ms << 48
    return phone("/usr/sbin/sysctl -n kern.whyred_touch=%d >/dev/null" % v)


def key(name, ms):
    k = KEYS.get(name)
    if k is None:
        return 1, "no such key"
    ms = max(20, min(int(ms), 5000))
    return phone("/usr/sbin/sysctl -n kern.whyred_key=%d >/dev/null" % (k | ms << 8))


def cam_controls():
    subprocess.run(["v4l2-ctl", "-d", cam["dev"], "-c", "auto_exposure=1", "-c",
                    "exposure_time_absolute=%d" % cam["exp"], "-c", "gain=%d" % cam["gain"],
                    "-c", "backlight_compensation=0"], capture_output=True)


def cam_thread():
    """ffmpeg reads MJPEG from the webcam and copies the frames out unchanged; the latest one is kept."""
    while True:
        cam_controls()
        p = subprocess.Popen(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "v4l2",
                              "-input_format", "mjpeg", "-video_size", "1920x1080", "-framerate", "30",
                              "-i", cam["dev"], "-vf", "fps=%d" % cam["fps"], "-q:v", "5", "-f", "mjpeg", "-"],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        cam["proc"] = p
        buf = b""
        while True:
            chunk = p.stdout.read(65536)
            if not chunk:
                break
            buf += chunk
            while True:
                s = buf.find(b"\xff\xd8")
                e = buf.find(b"\xff\xd9", s + 2)
                if s < 0 or e < 0:
                    break
                jpg, buf = buf[s:e + 2], buf[e + 2:]
                with frame_lock:
                    frame["jpg"], frame["n"], frame["t"] = jpg, frame["n"] + 1, time.time()
                    frame_lock.notify_all()
            if len(buf) > 8 << 20:
                buf = b""
        p.wait()
        time.sleep(2)


def shrink(jpg, q):
    """Crop (c=x,y,w,h in camera pixels) and scale (s) a frame, re-encoded at quality q: less to send."""
    try:
        from PIL import Image
    except ImportError:
        return jpg
    try:
        im = Image.open(io.BytesIO(jpg))
        if "c" in q:
            x, y, w, h = (int(float(v)) for v in q["c"].split(","))
            im = im.crop((max(0, x), max(0, y), min(im.width, x + w), min(im.height, y + h)))
        s = min(1.0, max(0.05, float(q.get("s", 1))))
        if s < 1:
            im = im.resize((max(1, int(im.width * s)), max(1, int(im.height * s))), Image.BILINEAR)
        out = io.BytesIO()
        im.save(out, "JPEG", quality=max(20, min(int(q.get("q", 60)), 95)))
        return out.getvalue()
    except (ValueError, OSError):
        return jpg


def log_tail(n):
    files = sorted(glob.glob("/tmp/claude-1000/*/*/scratchpad/b*.log") + glob.glob(os.path.join(HERE, "*.log")),
                   key=os.path.getmtime)
    if not files:
        return "(no kernel log file)"
    with open(files[-1], "rb") as f:
        f.seek(0, 2)
        f.seek(max(0, f.tell() - 200000))
        lines = f.read().decode("utf-8", "replace").splitlines()
    lines = [l for l in lines if not l.startswith("SSDROP")]
    return os.path.basename(files[-1]) + "\n" + "\n".join(lines[-n:])


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def send(self, code, body, ctype="text/plain; charset=utf-8"):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}
        if u.path == "/":
            with open(os.path.join(HERE, "index.html"), "rb") as f:
                return self.send(200, f.read(), "text/html; charset=utf-8")
        if u.path == "/snap.jpg":
            with frame_lock:
                jpg = frame["jpg"]
            if "c" in q or "s" in q:
                jpg = shrink(jpg, q)
            return self.send(200, jpg, "image/jpeg")
        if u.path == "/stream":
            self.send_response(200)
            self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=f")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            last = 0
            try:
                while True:
                    with frame_lock:
                        frame_lock.wait_for(lambda: frame["n"] != last, timeout=5)
                        jpg, last = frame["jpg"], frame["n"]
                    if jpg:
                        self.wfile.write(b"--f\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n" % len(jpg))
                        self.wfile.write(jpg + b"\r\n")
            except (BrokenPipeError, ConnectionResetError):
                return
        if u.path == "/log":
            return self.send(200, log_tail(int(q.get("n", 60))))
        if u.path == "/state":
            rc, out = phone("echo up", timeout=6)
            return self.send(200, json.dumps({"phone": rc == 0, "frame_age": round(time.time() - frame["t"], 1),
                                              "exp": cam["exp"], "gain": cam["gain"]}), "application/json")
        return self.send(404, "not found")

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}
        try:
            if u.path == "/touch":
                rc, out = touch(float(q["x1"]), float(q["y1"]), float(q.get("x2", q["x1"])),
                                float(q.get("y2", q["y1"])), float(q.get("ms", 60)))
            elif u.path == "/key":
                rc, out = key(q["k"], float(q.get("ms", 80)))
            elif u.path == "/camera":
                cam["exp"] = max(1, min(int(q.get("exp", cam["exp"])), 2000))
                cam["gain"] = max(0, min(int(q.get("gain", cam["gain"])), 100))
                cam_controls()
                rc, out = 0, "exposure %d gain %d" % (cam["exp"], cam["gain"])
            else:
                return self.send(404, "not found")
        except (KeyError, ValueError) as e:
            return self.send(400, "bad request: %s" % e)
        return self.send(200 if rc == 0 else 502, out or "ok")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--bind", default="127.0.0.1", help="address to listen on, e.g. the Tailscale IP")
    ap.add_argument("--camera", default="/dev/video0")
    a = ap.parse_args()
    cam["dev"] = a.camera
    threading.Thread(target=cam_thread, daemon=True).start()
    srv = http.server.ThreadingHTTPServer((a.bind, a.port), Handler)
    srv.daemon_threads = True
    print("remote: http://%s:%d" % (a.bind, a.port), flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()

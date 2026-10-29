#!/bin/sh
# tools/twister-dashboard/serve.sh [path/to/twister-out]
# Serves the dashboard on a free localhost port and exposes every
# twister-out and twister-out.N next to the given base (default: workspace root).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(west topdir 2>/dev/null || pwd)"
BASE="${1:-$ROOT/twister-out}"
case "$BASE" in /*) ;; *) BASE="$PWD/$BASE" ;; esac
BASE="${BASE%/}"

exec python3 - "$HERE" "$BASE" <<'EOF'
import sys, os, re, json, functools, threading, webbrowser, urllib.parse
import http.server

here, base = sys.argv[1:3]
parent, stem = os.path.dirname(base), os.path.basename(base)
pattern = re.compile("^" + re.escape(stem) + r"(\.\d+)?$")

def runs():
    found = []
    try:
        names = os.listdir(parent)
    except OSError:
        names = []
    for name in names:
        if not pattern.match(name):
            continue
        path = os.path.join(parent, name, "twister.json")
        if os.path.isfile(path):
            st = os.stat(path)
            found.append({"name": name, "url": "api/runs/" + urllib.parse.quote(name),
                          "mtime": int(st.st_mtime * 1000), "size": st.st_size})
    return sorted(found, key=lambda r: r["mtime"])

class Handler(http.server.SimpleHTTPRequestHandler):
    def send_json(self, body):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_report(self, name):
        for r in runs():                      # only names we discovered, no path tricks
            if r["name"] == name:
                with open(os.path.join(parent, name, "twister.json"), "rb") as f:
                    return self.send_json(f.read())
        self.send_error(404)

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        if path == "/api/runs":
            return self.send_json(json.dumps(runs()).encode())
        if path.startswith("/api/runs/"):
            return self.send_report(urllib.parse.unquote(path[len("/api/runs/"):]))
        if path == "/twister.json":            # older pages: latest run
            found = runs()
            if found:
                return self.send_report(found[-1]["name"])
            return self.send_error(404)
        return super().do_GET()

    def log_message(self, *args):
        pass

server = http.server.ThreadingHTTPServer(
    ("127.0.0.1", 0), functools.partial(Handler, directory=here))
url = "http://127.0.0.1:%d/" % server.server_address[1]
found = runs()
if found:
    print("Found %d run%s: %s" % (len(found), "" if len(found) == 1 else "s",
                                  ", ".join(r["name"] for r in found)))
else:
    print("No %s*/twister.json found, opening the dashboard empty." % os.path.join(parent, stem))
print("Twister dashboard at " + url + "  (Ctrl+C to stop)")
threading.Timer(0.3, lambda: webbrowser.open(url)).start()
try:
    server.serve_forever()
except KeyboardInterrupt:
    pass
EOF
"""ESP32 GIẢ cho P2 và nhóm C: chạy khi chưa có board.

Làm giống firmware thật ở phía MQTT (hợp đồng v6.1 mục 7):
  - telemetry 1 bản/giây (seq, boot, replay, run_id, res_float_cut... đúng tên trường)
  - status online (retain) + LWT offline JSON (QoS 1, retain), online gửi lại mỗi 30 s
  - event STATE_CHANGE khi mực sông đổi mức, RUN_START/RUN_STOP
  - nhận cmd / scenario / config, trả ack đúng id (cùng luật kiểm như netcmd.h)
Mực sông là sóng sin giả. Mặc định source = "sim" để KHÔNG lẫn vào số liệu nghiệm thu.

Chạy:  pip install paho-mqtt
       python tools/p2_net/fake_esp32.py --host 192.168.1.10
Thêm --offline-at 60 --offline-for 300 để giả mất mạng 5 phút (thử gửi bù #8 phía backend).
"""
import argparse
import json
import math
import re
import time

from mqttcompat import make_client

TOKEN = re.compile(r"^[A-Za-z0-9_-]+$")
RIV = ["NORMAL", "WATCH", "WARNING", "DANGER"]


def token(s, n):
    return isinstance(s, str) and 1 <= len(s) <= n and TOKEN.match(s) is not None


def num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


class Fake:
    def __init__(self, a):
        self.a = a
        self.dev = a.device
        self.p = f"flood/{self.dev}/"
        self.boot = a.boot
        self.seq = 0
        self.mode = "reactive"
        self.run_id = ""
        self.run_t0 = 0
        self.run_p = None
        self.state = 0
        self.cfg = {"L1": 80, "L2": 120, "L3": 160, "hyst": 10, "cmd_ttl_s": 30, "pwm_min": 20, "pwm_max": 70}
        self.buf = []          # bản đo lúc "mất mạng"
        self.t_start = time.time()
        self.c = make_client(f"fake-esp32-{self.dev}")
        self.c.will_set(self.p + "status", json.dumps({"state": "offline", "fw": "0.2"}), qos=1, retain=True)
        self.c.on_connect = self.on_connect
        self.c.on_message = self.on_message

    # ---------------------------------------------------------------- MQTT
    def on_connect(self, c, u, f, rc):
        print(f"[fake] MQTT noi, rc={rc}")
        for t in ("cmd", "scenario", "config"):
            c.subscribe(self.p + t)
        self.status()

    def status(self):
        up = int(time.time() - self.t_start)
        self.c.publish(self.p + "status", json.dumps({"state": "online", "fw": "0.2", "uptime": up}), retain=True)

    def ack(self, id_, ok, reason=""):
        msg = json.dumps({"id": id_, "ok": ok, "reason": reason}, separators=(",", ":"))
        self.c.publish(self.p + "ack", msg)
        print(f"[fake] ack {msg}")

    def event(self, **kw):
        e = {"ts": int(time.time()), **kw}
        if self.run_id:
            e["run_id"] = self.run_id
        self.c.publish(self.p + "event", json.dumps(e, separators=(",", ":")))

    def on_message(self, c, u, m):
        topic = m.topic[len(self.p):]
        try:
            d = json.loads(m.payload)
        except Exception:
            if topic in ("cmd", "scenario"):
                self.ack("", False, "invalid_cmd")
            return
        if topic == "config":
            if isinstance(d, dict):
                n = {**self.cfg, **{k: v for k, v in d.items() if num(v)}}
                if n["L1"] < n["L2"] < n["L3"] and n["pwm_min"] <= n["pwm_max"] <= 100:
                    self.cfg = n
                    print("[fake] config moi", n)
            return
        if not isinstance(d, dict) or not token(d.get("id"), 19):
            self.ack("", False, "invalid_cmd")
            return
        id_ = d["id"]
        if topic == "cmd":
            self.cmd(id_, d)
        elif topic == "scenario":
            self.scenario(id_, d)

    def cmd(self, id_, d):
        acts = [k for k in ("pump_drain", "mode", "calibrate") if d.get(k) is not None]
        if len(acts) != 1:
            return self.ack(id_, False, "invalid_cmd")
        k = acts[0]
        if k == "pump_drain":
            v = d["pump_drain"]
            if v not in ("on", "off") or ("ttl_s" in d and not num(d["ttl_s"])):
                return self.ack(id_, False, "invalid_cmd")
            if v == "on" and self.level() < 110:
                self.event(type="PUMP_BLOCKED", reason="below_pump_off")
                return self.ack(id_, False, "below_pump_off")
            return self.ack(id_, True)
        if k == "mode":
            if d["mode"] not in ("reactive", "predictive"):
                return self.ack(id_, False, "invalid_cmd")
            self.mode = d["mode"]
            return self.ack(id_, True)
        c = d["calibrate"]
        if (not isinstance(c, dict) or c.get("sensor") not in ("river", "reservoir")
                or not num(c.get("a")) or not num(c.get("b"))):
            return self.ack(id_, False, "invalid_cmd")
        a, b = c["a"], c["b"]
        if a == 0 or abs(a) > 5 or b < 0 or b > 2000:      # giới hạn của P1
            return self.ack(id_, False, "invalid_cmd")
        self.ack(id_, True)

    def scenario(self, id_, d):
        sc, rid = d.get("scenario"), d.get("run_id")
        if sc not in ("start", "stop") or not token(rid, 11):
            return self.ack(id_, False, "invalid_cmd")
        if sc == "stop":
            if not self.run_id:
                return self.ack(id_, False, "invalid_cmd")
            self.event(type="RUN_STOP")      # trước khi xoá run_id
            self.run_id = ""
            return self.ack(id_, True)
        if not all(num(d.get(k)) for k in ("period_s", "P0", "P1")):
            return self.ack(id_, False, "invalid_cmd")
        P0, P1 = d["P0"], d["P1"]
        if d["period_s"] < 10 or not (0 <= P0 <= 100 and 0 <= P1 <= 100) or P0 - P1 < self.cfg["pwm_min"]:
            return self.ack(id_, False, "invalid_cmd")
        if self.run_id:
            self.event(type="RUN_STOP")
        self.run_id, self.run_t0, self.run_p = rid, time.time(), d
        self.event(type="RUN_START")
        self.ack(id_, True, "clamped" if P0 + P1 > self.cfg["pwm_max"] else "")

    # ---------------------------------------------------------------- giả lập đo
    def level(self):
        t = time.time() - self.t_start
        return 100 + 70 * math.sin(2 * math.pi * t / self.a.period)

    def tick(self):
        lv = self.level()
        cfg = self.cfg
        new = 3 if lv >= cfg["L3"] else 2 if lv >= cfg["L2"] else 1 if lv >= cfg["L1"] else 0
        if new != self.state:
            # hysteresis giản lược: chỉ hạ mức khi xuống dưới ngưỡng - hyst
            thr = [0, cfg["L1"], cfg["L2"], cfg["L3"]]
            if new > self.state or lv < thr[self.state] - cfg["hyst"]:
                self.event(type="STATE_CHANGE", scope="river", **{"from": RIV[self.state]}, to=RIV[new],
                           level_mm=round(lv, 1))
                self.state = new
        tide = 0
        if self.run_id:
            p = self.run_p
            t = time.time() - self.run_t0
            tide = int(min(max(p["P0"] + p["P1"] * math.sin(2 * math.pi * t / p["period_s"]), 0), cfg["pwm_max"]))
        self.seq += 1
        res = 60.0
        return {
            "seq": self.seq, "boot": self.boot, "replay": False, "ts": int(time.time()),
            "raw_mm": round(500 - lv, 1), "level_mm": round(lv, 1), "valid": True, "state": RIV[self.state],
            "res_mm": res, "res_valid": True, "res_state": "R_OK", "pump_drain": "off", "pump_tide_pct": tide,
            "reservoir_full": False, "res_float_cut": False, "river_overflow": False, "mode": self.mode,
            "run_id": self.run_id, "rssi": -60, "source": self.a.source,
        }

    def run(self):
        self.c.connect(self.a.host, self.a.port, keepalive=15)
        self.c.loop_start()
        last_status = time.time()
        off_from = self.a.offline_at
        off_to = None if off_from is None else off_from + self.a.offline_for
        try:
            while True:
                t = time.time() - self.t_start
                s = self.tick()
                offline = off_from is not None and off_from <= t < off_to
                if offline:
                    self.buf.append(s)
                    if len(self.buf) == 1:
                        print("[fake] gia mat mang: dem du lieu")
                else:
                    if self.buf:
                        print(f"[fake] co mang lai: gui bu {len(self.buf)} ban (replay=true)")
                        for b in self.buf:
                            b["replay"] = True
                            self.c.publish(self.p + "telemetry", json.dumps(b, separators=(",", ":")))
                        self.buf = []
                    self.c.publish(self.p + "telemetry", json.dumps(s, separators=(",", ":")))
                if time.time() - last_status > 30:
                    self.status()
                    last_status = time.time()
                time.sleep(1)
        except KeyboardInterrupt:
            pass
        finally:
            self.c.loop_stop()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="ESP32 gia lap cho MQTT flood/{device}/")
    ap.add_argument("--host", default="localhost")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--device", default="node01")
    ap.add_argument("--boot", type=int, default=int(time.time()) % 100000, help="so lan khoi dong gia")
    ap.add_argument("--period", type=float, default=120, help="chu ky song muc song gia (s)")
    ap.add_argument("--source", default="sim", choices=["sim", "real"], help="giu 'sim' tru khi C can thu parser")
    ap.add_argument("--offline-at", type=float, default=None, help="giay thu may thi gia mat mang")
    ap.add_argument("--offline-for", type=float, default=300, help="mat mang bao lau (s)")
    Fake(ap.parse_args()).run()

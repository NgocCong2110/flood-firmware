"""Chạy các ca thử lệnh MQTT của P2 (P2 mục 7A, hợp đồng v6.1) và so ACK với kỳ vọng.

Dùng được với ESP32 thật hoặc ESP32 giả (fake_esp32.py).
    pip install paho-mqtt
    python tools/p2_net/send_tests.py --host 192.168.1.10
    python tools/p2_net/send_tests.py --host 192.168.1.10 --only T10,T13

Lưu ý trên board thật:
  - Ca kịch bản (T10, T13–T15, T21) cần scenario.cpp thật của P9; còn hàm rỗng trong stubs.cpp thì luôn invalid_cmd.
  - Ca bơm (PUMP) trả lý do theo mực nước thật (thường below_pump_off khi mực thấp): chấp nhận mọi lý do hợp lệ.
  - Ca calibrate hợp lệ (T03, T04) GHI hệ số giả vào NVS. Chạy xong phải gửi lại hệ số thật (P1).
    Mặc định bỏ qua hai ca này; thêm --with-calib để chạy.
"""
import argparse
import json
import queue
import sys
import time

from mqttcompat import make_client

ANY_PUMP = {"", "sensor_error", "reservoir_full", "interlock_off", "below_pump_off"}

# (ma ca, topic, payload thô, id ACK kỳ vọng, ok kỳ vọng (None = bất kỳ), tập reason chấp nhận, ghi chú)
CASES = [
    ("T03", "cmd", '{"id":"t03","calibrate":{"sensor":"river","a":-1.0,"b":500.0}}', "t03", True, {""}, "calib"),
    ("T04", "cmd", '{"id":"t04","calibrate":{"sensor":"reservoir","a":-1.0,"b":500.0}}', "t04", True, {""}, "calib"),
    ("T05", "cmd", '{"id":"t05","calibrate":{"a":-1,"b":500}}', "t05", False, {"invalid_cmd"}, ""),
    ("T06", "cmd", '{"id":"t06","calibrate":{"sensor":"lake","a":-1,"b":500}}', "t06", False, {"invalid_cmd"}, ""),
    ("T07", "cmd", '{"id":"t07","sensor":"river","calibrate":{"a":-1,"b":500}}', "t07", False, {"invalid_cmd"}, ""),
    ("T08", "cmd", '{"id":"t08","calibrate":{"sensor":"river","a":"-1","b":500}}', "t08", False, {"invalid_cmd"}, ""),
    ("T09", "cmd", '{"id":"t09","calibrate":{"sensor":"river","a":0,"b":500}}', "t09", False, {"invalid_cmd"}, "P1 tu choi"),
    ("T27", "cmd", '{"id":"t27","calibrate":{"sensor":"river","a":6,"b":500}}', "t27", False, {"invalid_cmd"}, "P1 tu choi"),
    ("MODE1", "cmd", '{"id":"m1","mode":"predictive"}', "m1", True, {""}, ""),
    ("MODE2", "cmd", '{"id":"m2","mode":"reactive"}', "m2", True, {""}, ""),
    ("MODE3", "cmd", '{"id":"m3","mode":"auto"}', "m3", False, {"invalid_cmd"}, ""),
    ("PUMP", "cmd", '{"id":"p1","pump_drain":"on","ttl_s":10}', "p1", None, ANY_PUMP, "P7 quyet dinh"),
    ("PUMP2", "cmd", '{"id":"p2","pump_drain":"off"}', "p2", None, ANY_PUMP, "P7 quyet dinh"),
    ("T17a", "cmd", '{"id":"x",', "", False, {"invalid_cmd"}, "JSON hong"),
    ("T17b", "scenario", '{"id":', "", False, {"invalid_cmd"}, "JSON hong"),
    ("T18a", "cmd", '{"id":"abcdefghij0123456789","mode":"reactive"}', "", False, {"invalid_cmd"}, "id 20 ky tu"),
    ("T18b", "cmd", '{"id":"a\\"b","mode":"reactive"}', "", False, {"invalid_cmd"}, "id co dau nhay"),
    ("T19", "cmd", '{"id":"t19","mode":"reactive","pump_drain":"on"}', "t19", False, {"invalid_cmd"}, "2 hanh dong"),
    ("T11a", "scenario", '{"id":"t11a","scenario":"start","period_s":300,"P0":50,"P1":20}', "t11a", False, {"invalid_cmd"}, "thieu run_id"),
    ("T11b", "scenario", '{"id":"t11b","scenario":"start","run_id":"r1","P0":50,"P1":20}', "t11b", False, {"invalid_cmd"}, "thieu period_s"),
    ("T12", "scenario", '{"scenario":"start","run_id":"r1","period_s":300,"P0":50,"P1":20}', "", False, {"invalid_cmd"}, "thieu id"),
    # Kịch bản theo thứ tự (cần P9 thật)
    ("T10", "scenario", '{"id":"scn-start-001","scenario":"start","run_id":"run-001","period_s":300,"P0":50,"P1":20}',
     "scn-start-001", True, {""}, "P9"),
    ("T14", "scenario", '{"id":"scn-stop-x","scenario":"stop","run_id":"other"}', "scn-stop-x", True, {""},
     "P9; v6.1: run_id khac van dung"),
    ("T15", "scenario", '{"id":"scn-stop-y","scenario":"stop","run_id":"run-001"}', "scn-stop-y", False, {"invalid_cmd"},
     "P9; khong co luot"),
    ("T21", "scenario", '{"id":"scn-start-002","scenario":"start","run_id":"run-002","period_s":300,"P0":70,"P1":20}',
     "scn-start-002", True, {"clamped"}, "P9; kep pwm_max"),
    ("T13", "scenario", '{"id":"scn-stop-002","scenario":"stop","run_id":"run-002"}', "scn-stop-002", True, {""}, "P9"),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="localhost")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--device", default="node01")
    ap.add_argument("--timeout", type=float, default=3.0)
    ap.add_argument("--only", default="", help="vd T10,T13")
    ap.add_argument("--with-calib", action="store_true", help="chay ca T03/T04 (ghi he so gia vao NVS!)")
    a = ap.parse_args()
    p = f"flood/{a.device}/"
    acks, status = queue.Queue(), queue.Queue()

    c = make_client("p2-send-tests")

    def on_msg(cl, u, m):
        try:
            d = json.loads(m.payload)
        except Exception:
            d = {"_raw": m.payload.decode(errors="replace")}
        (acks if m.topic.endswith("/ack") else status).put(d)

    c.on_message = on_msg
    c.connect(a.host, a.port, 15)
    c.subscribe(p + "ack")
    c.subscribe(p + "status")
    c.loop_start()

    passed = failed = 0
    # T01: status retain
    try:
        st = status.get(timeout=a.timeout)
        ok = st.get("state") == "online" and "fw" in st and "uptime" in st
        print(f"{'DAT ' if ok else 'TRUOT'} T01   status = {st}")
        passed += ok
        failed += not ok
    except queue.Empty:
        print("TRUOT T01   khong nhan duoc status retain (thiet bi chua tung noi?)")
        failed += 1

    only = {x.strip() for x in a.only.split(",") if x.strip()}
    for code, topic, payload, exp_id, exp_ok, reasons, note in CASES:
        if only and code not in only:
            continue
        if note == "calib" and not a.with_calib and code not in only:
            print(f"BO   {code:5} (ghi NVS; dung --with-calib)")
            continue
        while not acks.empty():
            acks.get_nowait()
        c.publish(p + topic, payload)
        got = None
        end = time.time() + a.timeout
        while time.time() < end:
            try:
                d = acks.get(timeout=end - time.time())
            except queue.Empty:
                break
            if d.get("id") == exp_id:
                got = d
                break
        if got is None:
            ok, detail = False, "khong co ACK"
        else:
            ok = (set(got) == {"id", "ok", "reason"} and (exp_ok is None or got["ok"] is exp_ok)
                  and got["reason"] in reasons)
            detail = json.dumps(got, separators=(",", ":"))
        print(f"{'DAT ' if ok else 'TRUOT'} {code:5} {detail}   {('(' + note + ')') if note else ''}")
        passed += ok
        failed += not ok
        time.sleep(0.3)

    c.loop_stop()
    print(f"\n{passed} dat, {failed} truot")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()

"""tools/calib.py - cong cu hieu chuan tren laptop (chi dung thu vien chuan).
Cach dung:
  python tools/p1_calib/calib.py hoi-quy calib_song.csv --sensor river
  python tools/p1_calib/calib.py hoi-quy calib_ho.csv --sensor reservoir
  python tools/p1_calib/calib.py danh-gia kiem_song.csv
  python tools/p1_calib/calib.py do-lap lap_song.csv
"""
import csv, json, math, sys


def read_cols(path, *names):
    try:
        with open(path, newline="", encoding="utf-8-sig") as f:
            rows = list(csv.DictReader(f))
    except OSError as e:
        sys.exit("khong doc duoc file: %s" % e)
    if not rows:
        sys.exit("file rong")
    try:
        cols = [[float(r[n]) for r in rows] for n in names]
    except KeyError as e:
        sys.exit("thieu cot %s; cot hien co: %s" % (e, list(rows[0].keys())))
    except (TypeError, ValueError):
        sys.exit("du lieu phai la so, khong de trong o du lieu")
    if any(not math.isfinite(value) for col in cols for value in col):
        sys.exit("du lieu phai la so huu han")
    return cols


def fit(raw, ref):  # thuoc = a*raw + b (binh phuong toi thieu)
    n = len(raw)
    mx, my = sum(raw) / n, sum(ref) / n
    sxx = sum((x - mx) ** 2 for x in raw)
    if sxx == 0:
        sys.exit("raw_mm khong doi, khong fit duoc")
    a = sum((x - mx) * (y - my) for x, y in zip(raw, ref)) / sxx
    return a, my - a * mx


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    cmd, path = sys.argv[1], sys.argv[2]
    if cmd == "hoi-quy":
        sensor = sys.argv[sys.argv.index("--sensor") + 1] if "--sensor" in sys.argv[:-1] else None
        if sensor not in ("river", "reservoir"):
            sys.exit("bat buoc --sensor river|reservoir")
        raw, ref = read_cols(path, "raw_mm", "thuoc_mm")
        if len(raw) < 2:
            sys.exit("can >= 2 dong")
        a, b = fit(raw, ref)
        mae = sum(abs(a * x + b - y) for x, y in zip(raw, ref)) / len(raw)
        print("a = %.5f  b = %.2f  (n = %d, MAE tren tap fit = %.2f mm)" % (a, b, len(raw), mae))
        print(json.dumps({"id": "cal-001", "calibrate": {"sensor": sensor, "a": round(a, 5), "b": round(b, 2)}}))
    elif cmd == "danh-gia":
        lv, ref = read_cols(path, "level_mm", "thuoc_mm")
        err = [abs(x - y) for x, y in zip(lv, ref)]
        print("n = %d  MAE = %.2f mm  max = %.2f mm" % (len(err), sum(err) / len(err), max(err)))
    elif cmd == "do-lap":
        (lv,) = read_cols(path, "level_mm")
        if len(lv) < 2:
            sys.exit("can >= 2 mau")
        m = sum(lv) / len(lv)
        sd = math.sqrt(sum((x - m) ** 2 for x in lv) / (len(lv) - 1))
        print("n = %d  trung binh = %.2f mm  do lech chuan = %.2f mm" % (len(lv), m, sd))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()

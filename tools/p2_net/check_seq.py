"""Nghiệm thu #8 (mất Wi-Fi 5 phút): kiểm telemetry không thiếu bản và đúng thứ tự.

1. Ghi log trước khi tắt Wi-Fi:
     mosquitto_sub -h <IP> -t "flood/node01/#" -v > mat_wifi.log
   (mỗi dòng: "<topic> <payload JSON>")
2. Tắt AP đúng 5 phút, bật lại, chờ thêm 2 phút rồi Ctrl+C.
3. python tools/p2_net/check_seq.py mat_wifi.log

Đạt khi mỗi boot "thieu 0". Bản trùng (do gửi lại sau khi mất liên kết) là bình thường, backend bỏ theo
(device_id, boot, seq). Thứ tự theo seq trong DB/backend; trong log thô bản bù có thể xen giữa bản mới.
"""
import json
import sys
from collections import defaultdict


def main(path):
    first = {}          # (boot, seq) -> thứ tự xuất hiện đầu tiên
    dup = 0
    replay = 0
    replay_seq = defaultdict(list)   # boot -> seq của bản replay theo thứ tự nhận
    status = []
    order = 0
    raw = open(path, "rb").read()
    # PowerShell 5 ghi ">" bằng UTF-16; mosquitto_sub/Out-File utf8 có thể có BOM. Tự nhận cả hai.
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        text = raw.decode("utf-16", errors="ignore")
    else:
        text = raw.decode("utf-8-sig", errors="ignore")
    for line in text.splitlines():
        line = line.strip()
        sp = line.find(" ")
        if sp < 0:
            continue
        topic, payload = line[:sp], line[sp + 1:]
        try:
            d = json.loads(payload)
        except Exception:
            continue
        if topic.endswith("/status"):
            status.append(d.get("state"))
        if not topic.endswith("/telemetry"):
            continue
        k = (d.get("boot"), d.get("seq"))
        if None in k:
            continue
        order += 1
        if d.get("replay"):
            replay += 1
            replay_seq[k[0]].append(k[1])
        if k in first:
            dup += 1
            continue
        first[k] = order

    by_boot = defaultdict(list)
    for (b, s), o in first.items():
        by_boot[b].append((o, s))
    all_ok = bool(by_boot)
    for b in sorted(by_boot):
        seqs = [s for _, s in sorted(by_boot[b])]          # theo thứ tự nhận
        miss = sorted(set(range(min(seqs), max(seqs) + 1)) - set(seqs))
        # bản bù phải được gửi theo đúng thứ tự seq tăng dần (từng đợt gửi bù); cho phép quay lại khi có đợt mới
        rs = replay_seq.get(b, [])
        backs = sum(1 for x, y in zip(rs, rs[1:]) if y <= x)
        print(f"boot {b}: {len(seqs)} ban (seq {min(seqs)}..{max(seqs)}), thieu {len(miss)} {miss[:20]}, "
              f"so lan ban bu lui seq: {backs} (moi dot mat mang toi da 1)")
        all_ok &= not miss
    print(f"ban trung (backend se bo): {dup}; ban replay: {replay}")
    print(f"status nhan duoc: {status}")
    print("KET QUA:", "DAT (khong thieu ban)" if all_ok else "TRUOT")
    return 0 if all_ok else 1


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))

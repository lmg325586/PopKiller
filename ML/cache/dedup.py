# dedup.py —— 样本去重 + 标签碰撞检测
# 用法：python dedup.py [a.json b.json ...]，默认合并同目录全部 *.json（自动排除输出文件）
# 产出：cleaned.json（干净训练集） + conflicts.json（碰撞仲裁队列）
import json
import sys
from pathlib import Path

OUT = Path("cleaned.json")
CONFLICT_OUT = Path("conflicts.json")

def window_key(s):
    # 窗口特征键：不含 label、不含 action
    return (s.get("exe"), s.get("title"), s.get("class"), s.get("raw"), s.get("score"))

def main():
    paths = [Path(p) for p in sys.argv[1:]]
    if not paths:
        paths = sorted(p for p in Path(__file__).resolve().parent.glob("*.json")
                       if p.name not in (OUT.name, CONFLICT_OUT.name))
    print(f"载入 {len(paths)} 个 JSON: {', '.join(p.name for p in paths)}")

    groups = {}  # window_key -> {label -> [samples...]}
    total = 0
    skipped_fg = 0
    for src in paths:
        try:
            data = json.loads(src.read_text(encoding="utf-8"))
        except Exception as e:
            print(f"跳过 {src.name}: {e}")
            continue
        for s in data.get("samples", []):
            if s.get("label") not in ("popup", "notpopup"):
                continue
            if (s.get("ev") or "").upper() == "FG":
                skipped_fg += 1
                continue
            total += 1
            groups.setdefault(window_key(s), {}).setdefault(s["label"], []).append(s)

    kept, dup_removed, conflicts = [], 0, []
    for k, g in groups.items():
        all_samples = [v[0] for v in g.values()]
        if len(g) > 1:  # 同窗口特征、不同标签 -> 碰撞，整组进仲裁队列
            conflicts.append({
                "window": {"exe": k[0], "title": k[1], "class": k[2], "raw": k[3], "score": k[4]},
                "label_counts": {lab: len(v) for lab, v in g.items()},
                "representative_samples": all_samples,
            })
            continue
        lab, vals = next(iter(g.items()))
        dup_removed += len(vals) - 1  # 同标签重复行，只留一条
        kept.append(vals[0])

    OUT.write_text(json.dumps(
        {"version": 1, "type": "popkiller_training_samples", "samples": kept},
        ensure_ascii=False, indent=4), encoding="utf-8")
    CONFLICT_OUT.write_text(json.dumps(
        {"version": 1, "type": "popkiller_label_conflicts", "conflicts": conflicts},
        ensure_ascii=False, indent=4), encoding="utf-8")

    print(f"读入样本: {total}")
    print(f"精确重复移除: {dup_removed}")
    print(f"标签碰撞组: {len(conflicts)}（整组跳过，见 {CONFLICT_OUT.name}）")
    print(f"保留: {len(kept)}")
    for c in conflicts:
        w = c["window"]
        print(f"  碰撞: exe={w['exe']} title={w['title']} raw={w['raw']} counts={c['label_counts']}")
    print(f"\n训练请用: python train.py {OUT.name} [仲裁后补充的json...]")
    return 0

if __name__ == "__main__":
    sys.exit(main())
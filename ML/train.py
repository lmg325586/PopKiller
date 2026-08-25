# 用法：python train.py cleaned.json [fixes.json] [--model=...] [--seed=N] [--no-goodexe]
# 特征契约 23 维（必须与 HeuristicML.h 逐位对齐）：
#   raw 0-11 : owner toolwin topmost noact resizable minmax capsys notitle small large temp roaming
#   raw 12-16: hexclass young unsigned idle farcur   （缺失记 -1 哨兵；15/16 当前掩码为 0）
#   派生 17-22: title_len ad_kw_hits known_good_exe widgetwin_class r_dlg32770 exe_digit_ratio
import json
import sys
from pathlib import Path

import numpy as np
from sklearn.ensemble import RandomForestClassifier, VotingClassifier
from sklearn.linear_model import LogisticRegression
from sklearn.preprocessing import StandardScaler
from sklearn.pipeline import Pipeline
from sklearn.model_selection import StratifiedKFold, cross_val_predict
from sklearn.metrics import accuracy_score
from skl2onnx import convert_sklearn
from skl2onnx.common.data_types import FloatTensorType
from sklearn.svm import SVC

try:
    from xgboost import XGBClassifier
except ImportError:
    XGBClassifier = None
try:
    from lightgbm import LGBMClassifier
except ImportError:
    LGBMClassifier = None

AD_KEYWORDS = [
    "广告", "优惠", "促销", "免费", "中奖", "礼包",
    "热点", "速看", "推荐", "清理", "加速", "升级", "弹窗", "资讯",
]
GOOD_EXES = {
    "devenv.exe", "code.exe", "chrome.exe", "msedge.exe", "firefox.exe",
    "windowsterminal.exe", "explorer.exe", "wechat.exe", "weixin.exe",
    "qq.exe", "dingtalk.exe", "tim.exe", "notepad.exe", "notepad++.exe",
    "everything.exe", "snipaste.exe", "listary.exe",
    # V2：本机常用工具 + 系统进程
    "steamwebhelper.exe", "steam.exe", "qbittorrent.exe", "rvrvpngui.exe",
    "mixline.exe", "mixline.ui.exe", "oopz.exe", "translucenttb.exe", "hyp.exe",
    "svchost.exe","cmd.exe","pwsh.exe","powershell.exe"
}

USE_GOOD_EXE = True

FEATURE_NAMES = [
    "r_owner", "r_toolwin", "r_topmost", "r_noact", "r_resizable", "r_minmax",
    "r_capsys", "r_notitle", "r_small", "r_large", "r_temp", "r_roaming",
    "r_hexclass", "r_young", "r_unsigned", "r_idle", "r_farcur",
    "title_len", "ad_kw_hits", "known_good_exe", "widgetwin_class",
    "r_dlg32770", "exe_digit_ratio",
]

def featurize(s: dict):
    raw = s.get("raw") or ""
    if len(raw) < 12:
        return None
    vec = []
    for i in range(12):
        c = raw[i]
        if c == 'T':   vec.append(1.0)
        elif c == 'F': vec.append(0.0)
        else:          return None
    for i in range(12, 17):
        c = raw[i] if i < len(raw) else 'U'
        v = 1.0 if c == 'T' else (0.0 if c == 'F' else -1.0)
        if i in (15, 16):
            v = 0.0   # 行为位掩码：解锁条件=真实位 popup 样本≥10 且系数翻正
        vec.append(v)

    title = (s.get("title") or "").lower()
    cls   = (s.get("class") or "").lower()
    exe   = (s.get("exe") or "").lower()

    t_len = s.get("title_len")
    kw = s.get("ad_kw_hits")
    if t_len is None: t_len = len(title)
    if kw is None:    kw = sum(1 for k in AD_KEYWORDS if k in title)

    vec.append(float(t_len))
    vec.append(float(kw))
    vec.append(1.0 if (USE_GOOD_EXE and exe in GOOD_EXES) else 0.0)
    vec.append(1.0 if "widgetwin" in cls else 0.0)
    vec.append(1.0 if cls == "#32770" else 0.0)
    vec.append(sum(1 for ch in exe if ch.isdigit()) / max(1, len(exe)))
    return vec

def window_key(s):
    # raw 只取前 12 位：同窗口在不同 raw 版本时代的重复记录合并
    return (s.get("exe"), s.get("title"), s.get("class"),
            (s.get("raw") or "")[:12], s.get("score"))

def build_models(spw: float, seed: int):
    rf = lambda: RandomForestClassifier(
        n_estimators=200, class_weight="balanced",
        min_samples_leaf=2, random_state=seed)
    lr = lambda: Pipeline([("sc", StandardScaler()),
                           ("lr", LogisticRegression(max_iter=1000, class_weight="balanced"))])
    models = {
        "rf": rf(),
        "lr": lr(),
        "rf_lr": VotingClassifier([("rf", rf()), ("lr", lr())], voting="soft"),
    }

    models["svm"] = Pipeline([("sc", StandardScaler()),
                              ("svm", SVC(kernel="rbf", class_weight="balanced",
                                          random_state=seed))])

    if XGBClassifier is not None:
        models["xgb"] = XGBClassifier(
            n_estimators=100, max_depth=3, learning_rate=0.1,
            subsample=0.8, colsample_bytree=0.8, min_child_weight=3,
            scale_pos_weight=spw, random_state=seed, eval_metric="logloss")
    if LGBMClassifier is not None:
        models["lgb"] = LGBMClassifier(
            n_estimators=100, max_depth=3, learning_rate=0.1,
            subsample=0.8, colsample_bytree=0.8, min_child_samples=5,
            scale_pos_weight=spw, random_state=seed, verbose=-1)
    return models

def main():
    global USE_GOOD_EXE
    chosen = "rf_lr"
    seed = 42
    paths = []
    for a in sys.argv[1:]:
        if a.startswith("--model="):
            chosen = a.split("=", 1)[1]
        elif a.startswith("--seed="):
            seed = int(a.split("=", 1)[1])
        elif a == "--no-goodexe":
            USE_GOOD_EXE = False
        else:
            paths.append(Path(a))
    if not paths:
        paths = sorted(p for p in Path(__file__).resolve().parent.glob("*.json")
                       if p.name not in ("cleaned.json", "conflicts.json"))
        print(f"自动载入 {len(paths)} 个 JSON: {', '.join(p.name for p in paths)}")
    if not USE_GOOD_EXE:
        print("消融模式：known_good_exe 已禁用")

    skipped = 0
    collected = []
    for src in paths:
        if not src.exists():
            continue
        try:
            data = json.loads(Path(src).read_text(encoding="utf-8"))
        except Exception as e:
            print(f"跳过 {src.name}: {e}")
            continue
        for s in data.get("samples", []):
            lab = s.get("label")
            if lab not in ("popup", "notpopup"):
                continue
            if not (s.get("exe") or "").strip():
                skipped += 1
                continue
            vec = featurize(s)
            if vec is None:
                skipped += 1
                continue
            collected.append((lab, s, vec))

    # 标签碰撞检测：同窗口特征不同标签 -> 整组跳过并报警
    labels_by_key = {}
    for lab, s, vec in collected:
        labels_by_key.setdefault(window_key(s), set()).add(lab)
    for k, v in labels_by_key.items():
        if len(v) > 1:
            print(f"标签碰撞，跳过: exe={k[0]} title={k[1]} raw={k[3]} labels={v}")

    seen = set()
    X, y, records = [], [], []
    for lab, s, vec in collected:
        k = window_key(s)
        if len(labels_by_key[k]) > 1 or k in seen:
            continue
        seen.add(k)
        X.append(vec)
        y.append(1 if lab == "popup" else 0)
        records.append(s)

    if skipped:
        print(f"跳过 {skipped} 条无效样本（空 exe / 缺 raw）")
    if len(X) < 30:
        print(f"有效样本太少（{len(X)}），先积累标注再训练。")
        return 1
    if len(set(y)) < 2:
        print("只有一类标签，无法训练二分类。")
        return 1

    X = np.asarray(X, dtype=np.float32)
    y = np.asarray(y, dtype=np.int64)
    print(f"有效样本: {len(X)}  弹窗: {int(y.sum())}  非弹窗: {int(len(y) - y.sum())}")

    spw = float((y == 0).sum()) / max(1, int((y == 1).sum()))
    models = build_models(spw, seed)
    if chosen not in models:
        print(f"未知模型 {chosen}，可选: {list(models)}")
        return 1

    skf = StratifiedKFold(n_splits=5, shuffle=True, random_state=seed)

    print("== 模型对比（5折折外）==")
    preds = {}
    for name, m in models.items():
        pre = cross_val_predict(m, X, y, cv=skf)
        preds[name] = pre
        tp = int(((pre == 1) & (y == 1)).sum())
        fp = int(((pre == 1) & (y == 0)).sum())
        fn = int(((pre == 0) & (y == 1)).sum())
        p = tp / (tp + fp) if tp + fp else 0.0
        r = tp / (tp + fn) if tp + fn else 0.0
        print(f"  {name:<6} acc={accuracy_score(y, pre):.3f}  popup P={p:.3f} R={r:.3f}")

    # 融合策略对比（裁决 C++ 侧 && 还是 ||）
    rf_pre = np.asarray(preds["rf"])
    lr_pre = np.asarray(preds["lr"])
    print("== 融合策略对比（折外）==")
    for name, pre in [
        ("AND双票", ((rf_pre == 1) & (lr_pre == 1)).astype(int)),
        ("OR一票 ", ((rf_pre == 1) | (lr_pre == 1)).astype(int)),
    ]:
        tp = int(((pre == 1) & (y == 1)).sum())
        fp = int(((pre == 1) & (y == 0)).sum())
        fn = int(((pre == 0) & (y == 1)).sum())
        p = tp / (tp + fp) if tp + fp else 0.0
        r = tp / (tp + fn) if tp + fn else 0.0
        print(f"  {name} acc={accuracy_score(y, pre):.3f}  popup P={p:.3f} R={r:.3f}")

    # 准确率加权投票对比
    rf_prob = cross_val_predict(models["rf"], X, y, cv=skf, method="predict_proba")[:, 1]
    lr_prob = cross_val_predict(models["lr"], X, y, cv=skf, method="predict_proba")[:, 1]
    w_rf = accuracy_score(y, preds["rf"])
    w_lr = accuracy_score(y, preds["lr"])
    wsum = w_rf + w_lr
    print(f"== 准确率加权投票（w_rf={w_rf:.3f} w_lr={w_lr:.3f}）==")
    fuse_wsoft = (w_rf * rf_prob + w_lr * lr_prob) / wsum
    hard_w = ((rf_pre == 1) * w_rf + (lr_pre == 1) * w_lr)
    for name, pre in [
        ("加权软投", (fuse_wsoft >= 0.5).astype(int)),
        ("加权硬投", (hard_w > wsum / 2).astype(int)),
    ]:
        tp = int(((pre == 1) & (y == 1)).sum())
        fp = int(((pre == 1) & (y == 0)).sum())
        fn = int(((pre == 0) & (y == 1)).sum())
        p = tp / (tp + fp) if tp + fp else 0.0
        r = tp / (tp + fn) if tp + fn else 0.0
        print(f"  {name} acc={accuracy_score(y, pre):.3f}  popup P={p:.3f} R={r:.3f}")

    pre = preds[chosen]
    print(f"== 误判明细（{chosen}，折外）==")
    n_bad = 0
    for s, yy, pp in zip(records, y, pre):
        if yy != pp:
            n_bad += 1
            print(f"  实际={'popup' if yy else 'notpopup'}"
                  f" 预测={'popup' if pp else 'notpopup'}"
                  f" | exe={s.get('exe')} title={s.get('title')} raw={s.get('raw')}")
    if n_bad == 0:
        print("  无")

    # === 全量训练并导出双模型（融合逻辑在 C++ 侧）===
    rf_final = RandomForestClassifier(
        n_estimators=200, class_weight="balanced",
        min_samples_leaf=2, random_state=seed)
    lr_final = Pipeline([("sc", StandardScaler()),
                         ("lr", LogisticRegression(max_iter=1000, class_weight="balanced"))])
    rf_final.fit(X, y)
    lr_final.fit(X, y)

    print("== RF 特征重要性（最终模型）==")
    for name, v in sorted(zip(FEATURE_NAMES, rf_final.feature_importances_),
                          key=lambda t: -t[1]):
        print(f"  {name:<15} : {v:.3f}")

    print("== LR 权重系数（正=弹窗倾向，负=非弹窗倾向）==")
    coef = lr_final.named_steps["lr"].coef_[0]
    for name, v in sorted(zip(FEATURE_NAMES, coef), key=lambda t: -abs(t[1])):
        print(f"  {name:<15} : {v:+.3f}")

    print("== 导出双模型 ==")
    init = [("input", FloatTensorType([None, X.shape[1]]))]
    try:
        onx = convert_sklearn(rf_final, initial_types=init)
        Path("popup_rf.onnx").write_bytes(onx.SerializeToString())
        print(f"已导出 popup_rf.onnx，特征数: {X.shape[1]}")
    except Exception as e:
        print(f"RF 导出失败: {e}")
    try:
        onx = convert_sklearn(lr_final, initial_types=init)
        Path("popup_lr.onnx").write_bytes(onx.SerializeToString())
        print(f"已导出 popup_lr.onnx，特征数: {X.shape[1]}")
    except Exception as e:
        print(f"LR 导出失败: {e}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
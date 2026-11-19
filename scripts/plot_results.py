# -*- coding: utf-8 -*-
"""Построение графиков по результатам экспериментов (data/results.csv).

Запуск:  python plot_results.py ..\\data\\results.csv ..\\report
Создаёт два графика:
  graph1_total_time.png  - время выполнения от продолжительности размышления (N = 5 и 7)
  graph2_wait_time.png   - среднее и максимальное время ожидания вилок по конфигурациям
Используются только реальные данные из CSV, полученные программой lab4.exe.
"""
import csv, sys, os
from collections import defaultdict
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join("..", "data", "results.csv")
out = sys.argv[2] if len(sys.argv) > 2 else "."
os.makedirs(out, exist_ok=True)

rows = [r for r in csv.DictReader(open(src, encoding="utf-8")) if r["strategy"] == "asym"]
if not rows:
    sys.exit("в CSV нет строк со strategy=asym")

groups = defaultdict(list)          # (n, think) -> список запусков
for r in rows:
    groups[(int(r["n"]), int(r["think_ms"]))].append(r)

def mean(v): return sum(v) / len(v)

# --- график 1: время выполнения
fig, ax = plt.subplots(figsize=(7, 4.2), dpi=150)
for n in sorted({k[0] for k in groups}):
    thinks = sorted(k[1] for k in groups if k[0] == n)
    avg = [mean([float(r["total_ms"]) for r in groups[(n, t)]]) for t in thinks]
    lo  = [min(float(r["total_ms"]) for r in groups[(n, t)]) for t in thinks]
    hi  = [max(float(r["total_ms"]) for r in groups[(n, t)]) for t in thinks]
    ax.errorbar(thinks, avg, yerr=[[a - l for a, l in zip(avg, lo)], [h - a for a, h in zip(avg, hi)]],
                marker="o", capsize=4, label="N = %d философов" % n)
ax.set_xlabel("Продолжительность размышления, мс"); ax.set_ylabel("Время эксперимента, мс")
ax.set_title("Время выполнения (среднее и разброс по запускам)")
ax.set_xticks(sorted({k[1] for k in groups})); ax.grid(alpha=.3); ax.legend()
fig.savefig(os.path.join(out, "graph1_total_time.png"), bbox_inches="tight"); plt.close(fig)

# --- график 2: время ожидания
keys = sorted(groups)
labels = ["N=%d\nthink=%d мс" % k for k in keys]
avgw = [mean([float(r["avg_wait_ms"]) for r in groups[k]]) for k in keys]
maxw = [max(float(r["max_wait_ms"]) for r in groups[k]) for k in keys]
x = range(len(keys))
fig, ax = plt.subplots(figsize=(7, 4.2), dpi=150)
ax.bar([i - 0.2 for i in x], avgw, width=0.4, label="среднее ожидание на приём пищи")
ax.bar([i + 0.2 for i in x], maxw, width=0.4, label="максимальное ожидание")
ax.set_xticks(list(x)); ax.set_xticklabels(labels)
ax.set_ylabel("Время ожидания вилок, мс"); ax.set_title("Ожидание вилок по конфигурациям")
ax.grid(axis="y", alpha=.3); ax.legend()
fig.savefig(os.path.join(out, "graph2_wait_time.png"), bbox_inches="tight"); plt.close(fig)
print("готово:", out)

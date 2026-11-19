# -*- coding: utf-8 -*-
"""Генерация схем для отчёта (лабораторная 4, вариант 4).
Запуск:  python make_diagrams.py [папка_вывода]
Создаёт: fig_ring.png, fig_arch.png, fig_states.png, fig_flow.png, fig_timeline.png
"""
import math, os, sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Circle, Polygon, FancyArrowPatch

OUT = sys.argv[1] if len(sys.argv) > 1 else "."
os.makedirs(OUT, exist_ok=True)
plt.rcParams["font.family"] = "DejaVu Sans"
INK = "#222222"


def arrow(ax, p, q, color=INK, lw=1.4, style="-|>", ls="-", rad=0.0, z=3):
    ax.add_patch(FancyArrowPatch(p, q, arrowstyle=style, mutation_scale=13, lw=lw, color=color,
                                 linestyle=ls, connectionstyle="arc3,rad=%s" % rad, zorder=z,
                                 shrinkA=0, shrinkB=0))


def rbox(ax, x, y, w, h, text, fc="#EEF2F7", fs=9, bold=False, ec=INK):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.25",
                                fc=fc, ec=ec, lw=1.2, zorder=2))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs,
            fontweight="bold" if bold else "normal", zorder=4)


# ------------------------------------------------------------------ 1. Кольцо философов N = 5
def fig_ring(n=5):
    fig, ax = plt.subplots(figsize=(7.4, 7.0), dpi=150)
    ax.set_xlim(-5.2, 5.2); ax.set_ylim(-5.0, 5.4); ax.set_aspect("equal"); ax.axis("off")
    R, Rf = 3.3, 2.5
    ang = [90 - i * 360 / n for i in range(n)]
    P = [(R * math.cos(math.radians(a)), R * math.sin(math.radians(a))) for a in ang]
    # вилка i расположена между философом i-1 и философом i
    F = []
    for i in range(n):
        a = ang[i] + 180 / n
        F.append((Rf * math.cos(math.radians(a)), Rf * math.sin(math.radians(a))))
    for i in range(n):
        even = (i % 2 == 0)
        first, second = (i, (i + 1) % n) if even else ((i + 1) % n, i)
        col = "#1F6FB5" if even else "#C0562B"
        # стрелки: сплошная — первая вилка, пунктир — вторая
        arrow(ax, P[i], F[first], color=col, lw=2.4, rad=0.0)
        arrow(ax, P[i], F[second], color=col, lw=1.4, ls=(0, (4, 3)), rad=0.0)
        for tgt, num in ((first, "1"), (second, "2")):
            mx = P[i][0] * 0.38 + F[tgt][0] * 0.62
            my = P[i][1] * 0.38 + F[tgt][1] * 0.62
            ax.text(mx, my, num, fontsize=10, fontweight="bold", color=col, ha="center", va="center",
                    bbox=dict(boxstyle="circle,pad=0.12", fc="white", ec=col, lw=1), zorder=6)
    for i in range(n):
        even = (i % 2 == 0)
        ax.add_patch(Circle(P[i], 0.78, fc="#DCE8F5" if even else "#F6E1D6", ec=INK, lw=1.3, zorder=5))
        ax.text(P[i][0], P[i][1], "P%d\n%s" % (i, "чётный" if even else "нечётный"), ha="center",
                va="center", fontsize=8, zorder=7)
        ax.add_patch(FancyBboxPatch((F[i][0] - 0.42, F[i][1] - 0.27), 0.84, 0.54,
                                    boxstyle="round,pad=0.02,rounding_size=0.12", fc="#FFE7A8", ec=INK, lw=1.1, zorder=5))
        ax.text(F[i][0], F[i][1], "вилка %d" % i, ha="center", va="center", fontsize=8, zorder=7)
    ax.text(0, 0.35, "Mutex на каждой вилке", ha="center", fontsize=9.5, fontweight="bold")
    ax.text(0, -0.15, "левая вилка философа i = вилка i\nправая вилка = вилка (i+1) mod N", ha="center",
            fontsize=8.5)
    ax.plot([], [], color="#1F6FB5", lw=2.4, label="чётный: 1 — левая, 2 — правая")
    ax.plot([], [], color="#C0562B", lw=2.4, label="нечётный: 1 — правая, 2 — левая")
    ax.plot([], [], color=INK, lw=2.4, label="сплошная стрелка — первая вилка")
    ax.plot([], [], color=INK, lw=1.4, ls=(0, (4, 3)), label="пунктир — вторая вилка")
    ax.legend(loc="lower center", fontsize=8, frameon=True, ncol=2, bbox_to_anchor=(0.5, -0.04))
    fig.savefig(os.path.join(OUT, "fig_ring.png"), bbox_inches="tight"); plt.close(fig)


# ------------------------------------------------------------------ 2. Архитектура программы
def fig_arch():
    fig, ax = plt.subplots(figsize=(8.4, 7.8), dpi=150)
    ax.set_xlim(0, 100); ax.set_ylim(0, 96); ax.axis("off")
    rbox(ax, 22, 85, 56, 8, "main(): ParseArgs → Config,\nжурнал, timeBeginPeriod(1)", "#DCE8F5", 9, True)
    rbox(ax, 3, 68, 40, 10, "RunSeries()\n4 конфигурации × не менее\n3 запусков (--experiments)", "#EEF2F7", 8.5)
    rbox(ax, 57, 68, 40, 10, "один запуск\n(без --experiments)", "#EEF2F7", 8.5)
    rbox(ax, 8, 46, 84, 15,
         "RunExperiment(): CreateMutex × N → CreateThread × N →\nWaitForMultipleObjects → проверка инвариантов и утечек\n→ статистика → CSV → CloseHandle",
         "#EEF2F7", 9, True)
    rbox(ax, 3, 24, 43, 14, "PhilosopherThread × N\nTHINKING → WAITING → EATING → …\n→ FINISHED", "#E3F1E1", 8.5, True)
    rbox(ax, 54, 24, 43, 14, "AcquireFork / ReleaseFork\nWaitForSingleObject (тайм-аут),\nReleaseMutex, проверка владельца", "#E3F1E1", 8.5)
    rbox(ax, 3, 3, 43, 14, "Общее состояние Run\nforks[] (Mutex), owner[], eating[],\nstate[], stats[], violations,\ntimeouts, stop", "#FFF4D6", 8)
    rbox(ax, 54, 3, 43, 14, "Журнал LogLine()\nCRITICAL_SECTION, время от начала\nэксперимента; CSV → plot_results.py\n→ графики", "#F6E1D6", 8)
    arrow(ax, (40, 85), (23, 78)); arrow(ax, (60, 85), (77, 78))
    arrow(ax, (23, 68), (23, 61)); arrow(ax, (77, 68), (77, 61))
    arrow(ax, (25, 46), (25, 38)); arrow(ax, (75, 46), (75, 38))
    arrow(ax, (46, 31), (54, 31), style="<|-|>")
    arrow(ax, (25, 24), (25, 17), style="<|-|>")
    arrow(ax, (60, 24), (44, 17))
    arrow(ax, (80, 24), (80, 17))
    fig.savefig(os.path.join(OUT, "fig_arch.png"), bbox_inches="tight"); plt.close(fig)


# ------------------------------------------------------------------ 3. Состояния философа
def fig_states():
    fig, ax = plt.subplots(figsize=(8.6, 5.0), dpi=150)
    ax.set_xlim(0, 100); ax.set_ylim(0, 60); ax.axis("off")
    ax.add_patch(Circle((4, 36), 1.5, fc=INK, ec=INK))
    arrow(ax, (5.6, 36), (11, 36))
    rbox(ax, 11, 29, 22, 14, "THINKING\nразмышляет", "#DCE8F5", 9, True)
    rbox(ax, 39, 29, 22, 14, "WAITING\nждёт вилки", "#FFE7A8", 9, True)
    rbox(ax, 67, 29, 22, 14, "EATING\nест", "#E3F1E1", 9, True)
    rbox(ax, 39, 4, 22, 11, "FINISHED\nзавершён", "#EEF2F7", 9, True)
    arrow(ax, (33, 36), (39, 36)); ax.text(36, 44.2, "размышление\nокончено", ha="center", fontsize=7.5)
    arrow(ax, (61, 36), (67, 36)); ax.text(64, 44.2, "обе вилки\nзахвачены", ha="center", fontsize=7.5)
    arrow(ax, (78, 43), (22, 43), rad=0.45)
    ax.text(50, 56.5, "вилки освобождены (ReleaseMutex), приёмов пищи меньше OPERATIONS", ha="center", fontsize=7.8)
    arrow(ax, (50, 29), (50, 15)); ax.text(51.5, 21.5, "тайм-аут ожидания вилки", fontsize=7.5)
    arrow(ax, (20, 29), (39, 11), rad=0.2); ax.text(17, 18, "выполнено OPERATIONS\nприёмов пищи", fontsize=7.5, ha="center")
    ax.text(78, 24, "в состоянии EATING\nвыполняется проверка\nинвариантов", fontsize=7.3, ha="center", color="#555555")
    fig.savefig(os.path.join(OUT, "fig_states.png"), bbox_inches="tight"); plt.close(fig)


# ------------------------------------------------------------------ 4. Блок-схема потока философа
def diamond(ax, cx, cy, w, h, text, fs=8.5):
    ax.add_patch(Polygon([(cx, cy + h / 2), (cx + w / 2, cy), (cx, cy - h / 2), (cx - w / 2, cy)],
                         closed=True, fc="#FFF4D6", ec=INK, lw=1.2, zorder=2))
    ax.text(cx, cy, text, ha="center", va="center", fontsize=fs, zorder=4)


def fig_flow():
    fig, ax = plt.subplots(figsize=(8.0, 10.6), dpi=150)
    ax.set_xlim(0, 100); ax.set_ylim(0, 135); ax.axis("off")
    cx = 42
    rbox(ax, cx - 18, 126, 36, 6, "Начало потока философа i", "#DCE8F5", 9, True)
    rbox(ax, cx - 26, 113, 52, 9, "Порядок вилок: чётный i — левая, затем правая;\nнечётный i — правая, затем левая", "#EEF2F7", 8.5)
    diamond(ax, cx, 100, 34, 11, "выполнено\nOPERATIONS приёмов?")
    rbox(ax, cx - 18, 83, 36, 7, "THINKING: Sleep(думает)", "#DCE8F5", 9)
    rbox(ax, cx - 22, 70, 44, 7, "WAITING: WaitForSingleObject(1-я вилка)", "#FFE7A8", 8.5)
    diamond(ax, cx, 58, 30, 9, "вилка получена?")
    rbox(ax, cx - 22, 43, 44, 7, "WaitForSingleObject(2-я вилка)", "#FFE7A8", 8.5)
    diamond(ax, cx, 31, 30, 9, "вилка получена?")
    rbox(ax, cx - 24, 15, 48, 9, "EATING: проверка инвариантов, Sleep(ест)\nReleaseMutex(2-я), ReleaseMutex(1-я)", "#E3F1E1", 8.5)
    rbox(ax, cx - 18, 2, 36, 6, "FINISHED, конец потока", "#EEF2F7", 9, True)
    arrow(ax, (cx, 126), (cx, 122)); arrow(ax, (cx, 113), (cx, 105.5)); arrow(ax, (cx, 94.5), (cx, 90))
    ax.text(cx + 1.5, 92.5, "нет", fontsize=8)
    arrow(ax, (cx, 83), (cx, 77)); arrow(ax, (cx, 70), (cx, 62.5))
    arrow(ax, (cx, 53.5), (cx, 50)); ax.text(cx + 1.5, 51.8, "да", fontsize=8)
    arrow(ax, (cx, 43), (cx, 35.5)); arrow(ax, (cx, 26.5), (cx, 24)); ax.text(cx + 1.5, 25.3, "да", fontsize=8)
    # возврат в цикл
    ax.plot([cx + 24, 90, 90], [19.5, 19.5, 100], color=INK, lw=1.4)
    arrow(ax, (90, 100), (cx + 17, 100))
    ax.text(70, 21, "следующий приём", fontsize=8)
    # ветки отказа (красные)
    red = "#B03030"
    ax.plot([cx + 15, 76, 76], [58, 58, 5], color=red, lw=1.4); ax.text(cx + 17, 59.5, "нет (тайм-аут)", fontsize=8, color=red)
    ax.plot([cx + 15, 70, 70], [31, 31, 5], color=red, lw=1.4); ax.text(cx + 17, 32.5, "нет: отпустить 1-ю вилку", fontsize=8, color=red)
    ax.plot([76, 70], [5, 5], color=red, lw=1.4)
    arrow(ax, (70, 5), (cx + 18, 5), color=red)
    # выход из цикла: да
    ax.plot([cx - 17, 6, 6], [100, 100, 5], color=INK, lw=1.4); ax.text(cx - 29, 101.5, "да", fontsize=8)
    arrow(ax, (6, 5), (cx - 18, 5))
    fig.savefig(os.path.join(OUT, "fig_flow.png"), bbox_inches="tight"); plt.close(fig)


# ------------------------------------------------------------------ 5. Временная диаграмма: два соседа, одна вилка
def fig_timeline():
    fig, ax = plt.subplots(figsize=(8.6, 6.2), dpi=150)
    ax.set_xlim(0, 100); ax.set_ylim(0, 74); ax.axis("off")
    xs = {"P1": 14, "F2": 50, "P2": 86}
    heads = {"P1": ("P1 (нечётный)\nпервая вилка = 2", "#F6E1D6"),
             "F2": ("вилка 2\n(Mutex)", "#FFE7A8"),
             "P2": ("P2 (чётный)\nпервая вилка = 2", "#DCE8F5")}
    for k, x in xs.items():
        t, c = heads[k]
        rbox(ax, x - 13, 64, 26, 8, t, c, 8.5, True)
        ax.plot([x, x], [64, 3], color="#777777", lw=1, ls="--", zorder=1)
    def msg(a, b, y, text, color=INK, ls="-"):
        arrow(ax, (xs[a], y), (xs[b], y), color=color, ls=ls)
        ax.text((xs[a] + xs[b]) / 2, y + 1.4, text, ha="center", fontsize=8, color=color)
    msg("P1", "F2", 58, "WaitForSingleObject(2)", "#C0562B")
    msg("F2", "P1", 53, "захвачена P1", "#C0562B", ls=(0, (4, 3)))
    msg("P2", "F2", 48, "WaitForSingleObject(2)", "#1F6FB5")
    ax.text(xs["P2"] - 1.5, 43.2, "P2 блокируется, пока вилка у P1", fontsize=8, ha="right", color="#1F6FB5", style="italic")
    ax.text(xs["P1"] + 2, 37, "P1 берёт вилку 1 и ест", fontsize=8, color="#C0562B", style="italic")
    msg("P1", "F2", 29, "ReleaseMutex(2)", "#C0562B")
    msg("F2", "P2", 23, "вилка 2 свободна → захвачена P2", "#1F6FB5", ls=(0, (4, 3)))
    ax.text(xs["P2"] - 2, 16, "P2 берёт вилку 3 и ест", fontsize=8, color="#1F6FB5", style="italic", ha="right")
    ax.text(50, 6, "Оба соседа первой хотят вилку 2 ⇒ одновременно удержать «первую» вилку может только один;\n"
                   "замкнутый цикл «каждый держит одну и ждёт соседа» образоваться не может.",
            ha="center", fontsize=8.2, color="#9A1F1F")
    fig.savefig(os.path.join(OUT, "fig_timeline.png"), bbox_inches="tight"); plt.close(fig)


if __name__ == "__main__":
    fig_ring(); fig_arch(); fig_states(); fig_flow(); fig_timeline()
    print("готово:", OUT)

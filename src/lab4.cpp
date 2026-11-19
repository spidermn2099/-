// Лабораторная №4, вариант 4: «Обедающие философы», асимметричный захват (Mutex).
//
// Философы с чётными номерами сначала захватывают ЛЕВУЮ вилку, с нечётными — ПРАВУЮ.
// Каждая вилка — отдельный объект Mutex (CreateMutex). Философ — отдельный поток.
//
// Сборка (Developer Command Prompt for VS 2022):
//     cl /EHsc /O2 lab4.cpp
// Примеры запуска:
//     lab4.exe --n 5 --think 2 --ops 5                (подробный журнал одного запуска)
//     lab4.exe --n 7 --think 15 --verbose 0           (один запуск, только итог)
//     lab4.exe --experiments --csv ..\data\results.csv (все 4 конфигурации x 3 запуска)
//     lab4.exe --strategy naive --hold 5 --n 5        (контрпример: все берут левую вилку первой)
//
// Индивидуализация (STUDENT_ID задаётся параметром --id):
//     SEED       = 1000 + STUDENT_ID * 97
//     OPERATIONS = BASE_OPERATIONS + STUDENT_ID * 5     (число приёмов пищи на философа)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>      // timeBeginPeriod / timeEndPeriod (с WIN32_LEAN_AND_MEAN не подключается автоматически)
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <random>
#include <algorithm>

#pragma comment(lib, "winmm.lib")   // timeBeginPeriod: иначе Sleep(2) может спать ~15 мс

// ----------------------------------------------------------------------------- параметры
enum Strategy  { STRAT_ASYM, STRAT_NAIVE };
enum SleepMode { SLEEP_SYS, SLEEP_HIRES };
enum State    { ST_THINKING = 0, ST_WAITING, ST_EATING, ST_FINISHED };

static const char* StateName(long s)
{
    switch (s)
    {
    case ST_THINKING: return "THINKING";
    case ST_WAITING:  return "WAITING";
    case ST_EATING:   return "EATING";
    case ST_FINISHED: return "FINISHED";
    default:          return "?";
    }
}

struct Config
{
    int      n          = 5;       // число философов (= число вилок)
    int      thinkMs    = 2;       // средняя продолжительность размышления, мс
    int      eatMs      = 5;       // продолжительность приёма пищи, мс
    int      jitter     = 50;      // разброс размышления, % (0 — строго фиксированное время)
    int      holdMs     = 0;       // пауза между захватом первой и второй вилки, мс
    int      baseOps    = 100;     // BASE_OPERATIONS варианта
    int      opsOverride= 0;       // если > 0 — число приёмов пищи задаётся напрямую
    int      studentId  = 123;     // STUDENT_ID
    bool     individual = true;    // true: индивидуальные параметры, false: стандартные
    Strategy strategy   = STRAT_ASYM;
    SleepMode sleepMode = SLEEP_HIRES; // hires: высокоточный waitable timer; sys: обычный Sleep()
    DWORD    timeoutMs  = 10000;   // тайм-аут ожидания одной вилки (признак взаимной блокировки)
    DWORD    maxRunMs   = 120000;  // предельная длительность одного эксперимента
    bool     verbose    = true;    // подробный журнал
    int      runs       = 1;       // число запусков одной конфигурации
    bool     experiments= false;   // серия: N = {5, 7} x think = {2, 15} мс
    std::string csvPath;           // файл результатов (CSV)
    std::string logPath;           // файл журнала
};

// ----------------------------------------------------------------------------- журнал
static CRITICAL_SECTION g_logCs;     // потоки одного процесса -> достаточно CRITICAL_SECTION
static FILE*            g_logFile = nullptr;
static LARGE_INTEGER    g_freq;
static LARGE_INTEGER    g_t0;        // начало текущего эксперимента

static double NowMs()
{
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - g_t0.QuadPart) * 1000.0 / (double)g_freq.QuadPart;
}

static void LogLine(const char* fmt, ...)
{
    char body[512];
    va_list ap; va_start(ap, fmt); vsnprintf(body, sizeof body, fmt, ap); va_end(ap);
    EnterCriticalSection(&g_logCs);
    printf("[%06ld ms] %s\n", (long)NowMs(), body);
    if (g_logFile) { fprintf(g_logFile, "[%06ld ms] %s\n", (long)NowMs(), body); }
    fflush(stdout);
    LeaveCriticalSection(&g_logCs);
}

static void LogWinError(const char* func)
{
    DWORD e = GetLastError();
    char* msg = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, 0, (LPSTR)&msg, 0, nullptr);
    LogLine("ERROR %s failed, GetLastError=%lu: %s", func, e, msg ? msg : "(no text)");
    if (msg) LocalFree(msg);
}

// ----------------------------------------------------------------------------- состояние эксперимента
struct PStat
{
    DWORD  tid        = 0;
    long   meals      = 0;
    double totalWait  = 0;     // суммарное время от конца размышления до начала еды, мс
    double maxWait    = 0;
    long   timeouts   = 0;
    double finishMs   = 0;
    bool   failed     = false;
};

struct Run
{
    Config cfg;
    int    n = 0;
    int    ops = 0;
    unsigned seed = 0;
    std::vector<HANDLE> forks;
    std::vector<LONG>   owner;     // кто сейчас владеет вилкой (-1 — никто)
    std::vector<LONG>   eating;    // 1, если философ ест
    std::vector<LONG>   state;
    std::vector<PStat>  stats;
    volatile LONG violations = 0;  // нарушения инвариантов
    volatile LONG timeouts   = 0;
    volatile LONG stop       = 0;
};

struct PArg { Run* run; int id; };

static void LogEv(const Run& r, int id, const char* ev, int res = -1)
{
    if (!r.cfg.verbose) return;
    if (res >= 0) LogLine("P%d %s RESOURCE=%d", id, ev, res);
    else          LogLine("P%d %s", id, ev);
}

static void SetState(Run& r, int id, State s) { InterlockedExchange(&r.state[id], (LONG)s); }

// ----------------------------------------------------------------------------- точная задержка
// Обычный Sleep(n) на Windows округляет задержку до шага системного таймера (до ~15.6 мс),
// если разрешение таймера не удалось повысить (timeBeginPeriod). Для моделирования
// длительностей 2/5/15 мс используется высокоточный waitable timer (Windows 10 1803+);
// при его недоступности — обычный Sleep(). Таймер служит ТОЛЬКО для задержек, не для синхронизации.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static HANDLE CreateSleepTimer(const Config& cfg)
{
    if (cfg.sleepMode != SLEEP_HIRES) return nullptr;
    return CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
}

static void PreciseSleep(HANDLE timer, DWORD ms)
{
    if (!timer) { Sleep(ms); return; }
    if (ms == 0) return;
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)ms * 10000;                 // относительное время, единицы по 100 нс
    if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) { Sleep(ms); return; }
    WaitForSingleObject(timer, INFINITE);
}

// ----------------------------------------------------------------------------- захват и освобождение вилки
static bool AcquireFork(Run& r, int id, int f)
{
    LogEv(r, id, "WAITING", f);
    DWORD w = WaitForSingleObject(r.forks[f], r.cfg.timeoutMs);
    if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED)
    {
        if (w == WAIT_ABANDONED) LogLine("P%d WARNING fork %d was abandoned by a dead owner", id, f);
        // Инвариант: вилка не может принадлежать двум философам одновременно
        LONG prev = InterlockedCompareExchange(&r.owner[f], (LONG)id, -1);
        if (prev != -1)
        {
            InterlockedIncrement(&r.violations);
            LogLine("INVARIANT VIOLATION: fork %d acquired by P%d while owned by P%ld", f, id, (long)prev);
        }
        LogEv(r, id, "ACQUIRED", f);
        return true;
    }
    if (w == WAIT_TIMEOUT)
    {
        InterlockedIncrement(&r.timeouts);
        r.stats[id].timeouts++;
        LogLine("P%d TIMEOUT RESOURCE=%d after %lu ms (possible deadlock)", id, f, r.cfg.timeoutMs);
        return false;
    }
    LogWinError("WaitForSingleObject");
    return false;
}

static void ReleaseFork(Run& r, int id, int f)
{
    // Сначала сбрасываем владельца, затем отпускаем Mutex: следующий владелец увидит -1
    LONG prev = InterlockedCompareExchange(&r.owner[f], -1, (LONG)id);
    if (prev != id)
    {
        InterlockedIncrement(&r.violations);
        LogLine("INVARIANT VIOLATION: P%d releases fork %d owned by %ld", id, f, (long)prev);
    }
    LogEv(r, id, "RELEASED", f);              // пишем в журнал ДО ReleaseMutex: порядок событий причинно согласован
    if (!ReleaseMutex(r.forks[f])) LogWinError("ReleaseMutex");
}

// ----------------------------------------------------------------------------- поток философа
static DWORD WINAPI PhilosopherThread(LPVOID param)
{
    PArg* a = (PArg*)param;
    Run& r = *a->run;
    const int id = a->id, n = r.n;
    PStat& st = r.stats[id];
    st.tid = GetCurrentThreadId();

    const int left = id, right = (id + 1) % n;
    int first, second;
    if (r.cfg.strategy == STRAT_ASYM)
    {
        // Асимметричный захват: чётные — левая вилка первой, нечётные — правая первой
        first  = (id % 2 == 0) ? left : right;
        second = (id % 2 == 0) ? right : left;
    }
    else { first = left; second = right; }          // контрпример: все одинаково

    if (r.cfg.verbose)
        LogLine("PHILOSOPHER %d | TID=%lu | LEFT=%d RIGHT=%d | FIRST=%d SECOND=%d | STATE=THINKING",
                id, st.tid, left, right, first, second);

    HANDLE sleepTimer = CreateSleepTimer(r.cfg);    // у каждого потока свой таймер
    std::mt19937 rng(r.seed + (unsigned)id);
    const int j = r.cfg.thinkMs * r.cfg.jitter / 100;
    std::uniform_int_distribution<int> thinkDist(r.cfg.thinkMs - j, r.cfg.thinkMs + j);

    for (int op = 0; op < r.ops && !r.stop; ++op)
    {
        SetState(r, id, ST_THINKING);
        LogEv(r, id, "THINKING");
        PreciseSleep(sleepTimer, (DWORD)std::max(0, thinkDist(rng)));

        SetState(r, id, ST_WAITING);
        const double tWait = NowMs();
        if (!AcquireFork(r, id, first)) { st.failed = true; break; }
        if (r.cfg.holdMs > 0) PreciseSleep(sleepTimer, (DWORD)r.cfg.holdMs);
        if (!AcquireFork(r, id, second)) { ReleaseFork(r, id, first); st.failed = true; break; }

        const double waited = NowMs() - tWait;
        st.totalWait += waited;
        if (waited > st.maxWait) st.maxWait = waited;

        // Приём пищи. Инвариант: два соседа не едят одновременно
        SetState(r, id, ST_EATING);
        InterlockedExchange(&r.eating[id], 1);
        if (InterlockedCompareExchange(&r.eating[(id + n - 1) % n], 0, 0) ||
            InterlockedCompareExchange(&r.eating[(id + 1) % n], 0, 0))
        {
            InterlockedIncrement(&r.violations);
            LogLine("INVARIANT VIOLATION: P%d eats together with a neighbour", id);
        }
        LogEv(r, id, "EATING");
        PreciseSleep(sleepTimer, (DWORD)r.cfg.eatMs);
        InterlockedExchange(&r.eating[id], 0);
        st.meals++;

        ReleaseFork(r, id, second);
        ReleaseFork(r, id, first);
    }

    SetState(r, id, ST_FINISHED);
    st.finishMs = NowMs();
    LogEv(r, id, "FINISHED");
    if (sleepTimer) CloseHandle(sleepTimer);
    return st.failed ? 1 : 0;
}

// ----------------------------------------------------------------------------- один эксперимент
struct Result
{
    bool   ok = false;           // эксперимент выполнен без отказов
    double totalMs = 0;
    long   meals = 0;
    double throughput = 0;       // приёмов пищи в секунду
    double avgWait = 0, maxWait = 0;
    long   timeouts = 0, violations = 0;
    bool   deadlock = false;
};

static Result RunExperiment(const Config& cfg, int runIdx, bool details)
{
    Result res;
    Run r;
    r.cfg = cfg;
    r.n = cfg.n;
    r.ops  = cfg.opsOverride > 0 ? cfg.opsOverride
                                 : (cfg.individual ? cfg.baseOps + cfg.studentId * 5 : cfg.baseOps);
    r.seed = cfg.individual ? (unsigned)(1000 + cfg.studentId * 97) : 1000u;
    r.owner.assign(r.n, -1);
    r.eating.assign(r.n, 0);
    r.state.assign(r.n, ST_THINKING);
    r.stats.assign(r.n, PStat());
    r.forks.assign(r.n, nullptr);

    QueryPerformanceCounter(&g_t0);
    if (details || cfg.verbose)
    LogLine("EXPERIMENT START run=%d N=%d think=%dms eat=%dms ops=%d seed=%u strategy=%s mode=%s sleep=%s",
            runIdx, r.n, cfg.thinkMs, cfg.eatMs, r.ops, r.seed,
            cfg.strategy == STRAT_ASYM ? "asymmetric" : "naive", cfg.individual ? "individual" : "standard",
            cfg.sleepMode == SLEEP_HIRES ? "hires" : "sys");

    // --- создание объектов синхронизации
    for (int i = 0; i < r.n; ++i)
    {
        r.forks[i] = CreateMutexA(nullptr, FALSE, nullptr);
        if (!r.forks[i])
        {
            LogWinError("CreateMutex");
            for (int k = 0; k < i; ++k) CloseHandle(r.forks[k]);
            return res;
        }
    }

    // --- запуск потоков
    std::vector<PArg>   args(r.n);
    std::vector<HANDLE> threads(r.n, nullptr);
    int created = 0;
    QueryPerformanceCounter(&g_t0);
    for (int i = 0; i < r.n; ++i)
    {
        args[i].run = &r; args[i].id = i;
        threads[i] = CreateThread(nullptr, 0, PhilosopherThread, &args[i], 0, nullptr);
        if (!threads[i]) { LogWinError("CreateThread"); InterlockedExchange(&r.stop, 1); break; }
        ++created;
    }

    // --- ожидание завершения с предельной длительностью
    if (created > 0)
    {
        DWORD w = WaitForMultipleObjects((DWORD)created, threads.data(), TRUE, cfg.maxRunMs);
        if (w == WAIT_TIMEOUT)
        {
            LogLine("ERROR experiment exceeded max time %lu ms, stopping threads", cfg.maxRunMs);
            InterlockedExchange(&r.stop, 1);
            w = WaitForMultipleObjects((DWORD)created, threads.data(), TRUE, cfg.timeoutMs + 2000);
            if (w == WAIT_TIMEOUT) LogLine("ERROR some threads did not stop");
        }
        else if (w == WAIT_FAILED) LogWinError("WaitForMultipleObjects");
    }
    res.totalMs = NowMs();

    // --- проверка: после завершения не должно оставаться удерживаемых ресурсов
    for (int i = 0; i < r.n; ++i)
    {
        if (r.owner[i] != -1)
        {
            InterlockedIncrement(&r.violations);
            LogLine("INVARIANT VIOLATION: fork %d still owned by P%ld after finish", i, (long)r.owner[i]);
        }
        DWORD w = WaitForSingleObject(r.forks[i], 0);
        if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) ReleaseMutex(r.forks[i]);
        else { InterlockedIncrement(&r.violations); LogLine("INVARIANT VIOLATION: mutex %d is still held", i); }
    }

    // --- статистика
    double wait = 0;
    for (int i = 0; i < r.n; ++i)
    {
        res.meals += r.stats[i].meals;
        wait += r.stats[i].totalWait;
        res.maxWait = std::max(res.maxWait, r.stats[i].maxWait);
        if (r.stats[i].failed) res.deadlock = true;
    }
    res.avgWait    = res.meals ? wait / (double)res.meals : 0;
    res.throughput = res.totalMs > 0 ? res.meals * 1000.0 / res.totalMs : 0;
    res.timeouts   = r.timeouts;
    res.violations = r.violations;
    res.deadlock   = res.deadlock || res.timeouts > 0;
    res.ok         = !res.deadlock && res.violations == 0 && res.meals == (long)r.n * r.ops;

    if (details)
    {
        printf("\n=== RESULT run=%d: N=%d think=%dms eat=%dms ops=%d seed=%u sleep=%s ===\n",
               runIdx, r.n, cfg.thinkMs, cfg.eatMs, r.ops, r.seed, cfg.sleepMode == SLEEP_HIRES ? "hires" : "sys");
        printf("P  | TID    | state    | meals | avg wait, ms | max wait, ms | timeouts | finished at, ms\n");
        for (int i = 0; i < r.n; ++i)
        {
            const PStat& s = r.stats[i];
            printf("P%-2d| %-6lu | %-8s | %5ld | %12.3f | %12.3f | %8ld | %.1f\n", i, s.tid, StateName(r.state[i]), s.meals,
                   s.meals ? s.totalWait / s.meals : 0.0, s.maxWait, s.timeouts, s.finishMs);
        }
        printf("total time: %.1f ms, meals: %ld (expected %ld), throughput: %.1f meals/s\n",
               res.totalMs, res.meals, (long)r.n * r.ops, res.throughput);
        printf("average wait per meal: %.3f ms, max wait: %.3f ms\n", res.avgWait, res.maxWait);
        printf("timeouts: %ld, invariant violations: %ld\n", res.timeouts, res.violations);
        printf("DEADLOCK: %s | RESULT: %s\n\n", res.deadlock ? "DETECTED" : "none", res.ok ? "OK" : "FAILED");
        fflush(stdout);
    }

    // --- освобождение ресурсов
    for (int i = 0; i < created; ++i) CloseHandle(threads[i]);
    for (int i = 0; i < r.n; ++i)     CloseHandle(r.forks[i]);

    // --- CSV для построения графиков
    if (!cfg.csvPath.empty())
    {
        FILE* f = fopen(cfg.csvPath.c_str(), "a");
        if (!f) { printf("ERROR cannot open CSV file %s\n", cfg.csvPath.c_str()); }
        else
        {
            fseek(f, 0, SEEK_END);
            if (ftell(f) == 0)
                fprintf(f, "strategy,n,think_ms,eat_ms,ops,seed,run,total_ms,meals,throughput,avg_wait_ms,max_wait_ms,timeouts,violations,ok,sleep\n");
            fprintf(f, "%s,%d,%d,%d,%d,%u,%d,%.3f,%ld,%.3f,%.4f,%.4f,%ld,%ld,%d,%s\n",
                    cfg.strategy == STRAT_ASYM ? "asym" : "naive", r.n, cfg.thinkMs, cfg.eatMs, r.ops, r.seed,
                    runIdx, res.totalMs, res.meals, res.throughput, res.avgWait, res.maxWait,
                    res.timeouts, res.violations, res.ok ? 1 : 0, cfg.sleepMode == SLEEP_HIRES ? "hires" : "sys");
            fclose(f);
        }
    }
    return res;
}

// ----------------------------------------------------------------------------- серия экспериментов
static int RunSeries(Config cfg)
{
    cfg.verbose = false;
    struct Cfg { const char* name; int n; int think; };
    const Cfg list[] = { {"A", 5, 2}, {"B", 5, 15}, {"C", 7, 2}, {"D", 7, 15} };
    const int runs = std::max(cfg.runs, 3);          // по заданию — не менее трёх запусков

    printf("\nConfig | N | think, ms | ");
    for (int k = 1; k <= runs; ++k) printf("run %d, ms | ", k);
    printf("avg, ms | avg wait, ms | max wait, ms | timeouts | violations | OK\n");

    int bad = 0;
    for (const Cfg& c : list)
    {
        Config x = cfg; x.n = c.n; x.thinkMs = c.think;
        std::vector<double> t; double aw = 0, mw = 0; long to = 0, vi = 0; bool allOk = true;
        for (int k = 1; k <= runs; ++k)
        {
            Result r = RunExperiment(x, k, false);
            t.push_back(r.totalMs); aw += r.avgWait; mw = std::max(mw, r.maxWait);
            to += r.timeouts; vi += r.violations; allOk = allOk && r.ok;
        }
        double avg = 0; for (double v : t) avg += v; avg /= (double)t.size();
        printf("%-6s | %d | %9d | ", c.name, c.n, c.think);
        for (double v : t) printf("%9.1f | ", v);
        printf("%7.1f | %12.3f | %12.3f | %8ld | %10ld | %s\n", avg, aw / runs, mw, to, vi, allOk ? "yes" : "NO");
        fflush(stdout);
        if (!allOk) ++bad;
    }
    printf("\n%s\n", bad == 0 ? "ALL CONFIGURATIONS: no deadlock, no invariant violations"
                              : "PROBLEMS DETECTED in some configurations");
    return bad == 0 ? 0 : 1;
}

// ----------------------------------------------------------------------------- разбор аргументов
static void Usage()
{
    printf("usage: lab4.exe [options]\n"
           "  --n N             number of philosophers (2..60), default 5\n"
           "  --think MS        average thinking time, ms, default 2\n"
           "  --eat MS          eating time, ms, default 5\n"
           "  --jitter P        thinking time spread, percent, default 50 (0 = fixed)\n"
           "  --hold MS         pause between taking first and second fork, default 0\n"
           "  --id N            STUDENT_ID (last 3 digits), default 123\n"
           "  --base-ops N      BASE_OPERATIONS, default 100\n"
           "  --ops N           meals per philosopher (overrides the formula)\n"
           "  --mode M          individual | standard\n"
           "  --strategy S      asym | naive\n"
           "  --sleep M         hires (precise timer, default) | sys (plain Sleep)\n"
           "  --timeout MS      timeout of waiting for one fork, default 10000\n"
           "  --max-time SEC    maximum duration of one experiment, default 120\n"
           "  --verbose 0|1     detailed event log, default 1\n"
           "  --runs N          runs per configuration\n"
           "  --experiments     series: N={5,7} x think={2,15} ms, >= 3 runs each\n"
           "  --csv FILE        append results to CSV\n"
           "  --log FILE        duplicate the event log into a file\n");
}

static bool ParseArgs(int argc, char** argv, Config& c)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { printf("missing value for %s\n", name); return nullptr; }
            return argv[++i];
        };
        const char* v = nullptr;
        if      (a == "--experiments") c.experiments = true;
        else if (a == "--help" || a == "-h") return false;
        else if (!(v = next(a.c_str()))) return false;
        else if (a == "--n")        c.n = atoi(v);
        else if (a == "--think")    c.thinkMs = atoi(v);
        else if (a == "--eat")      c.eatMs = atoi(v);
        else if (a == "--jitter")   c.jitter = atoi(v);
        else if (a == "--hold")     c.holdMs = atoi(v);
        else if (a == "--id")       c.studentId = atoi(v);
        else if (a == "--base-ops") c.baseOps = atoi(v);
        else if (a == "--ops")      c.opsOverride = atoi(v);
        else if (a == "--timeout")  c.timeoutMs = (DWORD)atoi(v);
        else if (a == "--max-time") c.maxRunMs = (DWORD)atoi(v) * 1000u;
        else if (a == "--verbose")  c.verbose = atoi(v) != 0;
        else if (a == "--runs")     c.runs = atoi(v);
        else if (a == "--csv")      c.csvPath = v;
        else if (a == "--log")      c.logPath = v;
        else if (a == "--mode")     c.individual = (std::string(v) != "standard");
        else if (a == "--sleep")    c.sleepMode = (std::string(v) == "sys") ? SLEEP_SYS : SLEEP_HIRES;
        else if (a == "--strategy") c.strategy = (std::string(v) == "naive") ? STRAT_NAIVE : STRAT_ASYM;
        else { printf("unknown option %s\n", a.c_str()); return false; }
    }
    if (c.n < 2 || c.n > 60)            { printf("N must be in 2..60\n"); return false; }
    if (c.thinkMs < 0 || c.eatMs < 0)   { printf("durations must be >= 0\n"); return false; }
    if (c.jitter < 0 || c.jitter > 100) { printf("jitter must be in 0..100\n"); return false; }
    if (c.runs < 1) c.runs = 1;
    return true;
}

int main(int argc, char** argv)
{
    InitializeCriticalSection(&g_logCs);
    QueryPerformanceFrequency(&g_freq);

    Config cfg;
    if (!ParseArgs(argc, argv, cfg)) { Usage(); DeleteCriticalSection(&g_logCs); return 1; }
    if (!cfg.logPath.empty())
    {
        g_logFile = fopen(cfg.logPath.c_str(), "w");
        if (!g_logFile) printf("WARNING cannot open log file %s\n", cfg.logPath.c_str());
    }

    timeBeginPeriod(1);                       // точность Sleep() 1 мс
    int rc = 0;
    if (cfg.experiments) rc = RunSeries(cfg);
    else
    {
        for (int k = 1; k <= cfg.runs; ++k)
        {
            Result r = RunExperiment(cfg, k, true);
            if (!r.ok) rc = 1;
        }
    }
    timeEndPeriod(1);

    if (g_logFile) fclose(g_logFile);
    DeleteCriticalSection(&g_logCs);
    return rc;
}

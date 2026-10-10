#pragma once

#include "bank.h"

#include <atomic>
#include <chrono>

namespace sim
{

// Просьба остановить симуляцию. Ставится обработчиком Ctrl+C и по истечении времени,
// участники проверяют его перед каждым ходом.
extern std::atomic<bool> g_stopRequested;

enum class Mode
{
    Single,   // все участники ходят по очереди в одном потоке
    Parallel, // каждый участник в своём потоке
};

struct Options
{
    Mode mode = Mode::Single;
    std::chrono::seconds duration{0};
    bool quiet = false; // не печатать действия участников
};

struct Report
{
    double seconds = 0;                // сколько длилась симуляция
    unsigned long long operations = 0; // число операций банка за симуляцию
    Money initialTotal = 0;            // наличные + балансы до начала
    Money cash = 0;                    // Bank::GetCash() после завершения всех потоков
    Money walletsTotal = 0;            // сумма всех кошельков
    Money balancesTotal = 0;           // сумма балансов всех счетов
};

// Запускает симуляцию экономики и возвращается, когда все участники закончили.
// Бросает std::system_error, если не удалось создать поток.
Report Run(const Options& options);

} // namespace sim

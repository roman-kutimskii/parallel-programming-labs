// Лабораторная 3: потокобезопасный банк и симуляция экономики поверх него.
#include "simulation.h"

#include <charconv>
#include <csignal>
#include <format>
#include <iostream>
#include <optional>
#include <string_view>
#include <system_error>

namespace
{

// Обработчик сигнала может делать очень немногое; запись в lock-free atomic — можно.
extern "C" void OnInterrupt(int)
{
    sim::g_stopRequested = true;
}

// Возвращает 0, если аргумент не является положительным целым.
int ParsePositive(std::string_view arg)
{
    int value = 0;
    const auto [end, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), value);
    const bool parsed = (ec == std::errc{}) && (end == arg.data() + arg.size());
    return (parsed && value > 0) ? value : 0;
}

std::optional<sim::Options> ParseArgs(int argc, char* argv[])
{
    sim::Options options;
    bool hasMode = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--quiet")
        {
            options.quiet = true;
        }
        else if (arg == "--mode" && i + 1 < argc)
        {
            const std::string_view mode = argv[++i];
            if (mode == "single")
                options.mode = sim::Mode::Single;
            else if (mode == "parallel")
                options.mode = sim::Mode::Parallel;
            else
                return std::nullopt;
            hasMode = true;
        }
        else if (arg == "--seconds" && i + 1 < argc)
        {
            options.duration = std::chrono::seconds(ParsePositive(argv[++i]));
            if (options.duration.count() == 0)
                return std::nullopt;
        }
        else
        {
            return std::nullopt;
        }
    }
    if (!hasMode || options.duration.count() == 0)
        return std::nullopt;
    return options;
}

} // namespace

int main(int argc, char* argv[])
{
    const auto options = ParseArgs(argc, argv);
    if (!options)
    {
        std::cerr << std::format("Usage: {} --mode <single|parallel> --seconds <N> [--quiet]\n", argv[0]);
        return 1;
    }

    std::signal(SIGINT, OnInterrupt);

    sim::Report report;
    try
    {
        report = sim::Run(*options);
    }
    catch (const std::system_error& e)
    {
        std::cerr << std::format("Не удалось создать поток: {}\n", e.what());
        return 2;
    }
    catch (const std::exception& e)
    {
        std::cerr << std::format("Ошибка: {}\n", e.what());
        return 2;
    }

    const bool cashOk = report.walletsTotal == report.cash;
    const bool totalOk = report.cash + report.balancesTotal == report.initialTotal;

    std::cout << std::format("Режим: {}, время: {:.2f} с\n",
                             options->mode == sim::Mode::Single ? "single" : "parallel", report.seconds);
    std::cout << std::format("Операций банка: {}, в секунду: {:.0f}\n", report.operations,
                             report.operations / report.seconds);
    std::cout << std::format("Сумма кошельков {} == наличные банка {}: {}\n", report.walletsTotal, report.cash,
                             cashOk ? "да" : "НЕТ");
    std::cout << std::format("Наличные {} + счета {} == начальная сумма {}: {}\n", report.cash,
                             report.balancesTotal, report.initialTotal, totalOk ? "да" : "НЕТ");

    return (cashOk && totalOk) ? 0 : 3;
}

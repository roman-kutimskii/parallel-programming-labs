#include <charconv>
#include <format>
#include <iostream>
#include <string_view>
#include <syncstream>
#include <system_error>
#include <thread>
#include <vector>

namespace
{

// Тело потока. Номер принимается по значению: копия живёт столько же,
// сколько сам поток, и не зависит от переменной цикла в main.
void Worker(int index)
{
    // osyncstream копит вывод у себя и отдаёт его в cout одним куском
    // в деструкторе — поэтому строки разных потоков не перемешиваются.
    std::osyncstream(std::cout) << std::format("Поток №{} выполняет свою работу\n", index);
}

// Возвращает 0, если аргумент не является положительным целым.
int ParseThreadCount(std::string_view arg)
{
    int value = 0;
    const auto [end, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), value);
    const bool parsed = (ec == std::errc{}) && (end == arg.data() + arg.size());
    return (parsed && value > 0) ? value : 0;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << std::format("Usage: {} <thread-count>\n", argv[0]);
        return 1;
    }

    const int threadCount = ParseThreadCount(argv[1]);
    if (threadCount == 0)
    {
        std::cerr << "Число потоков должно быть целым положительным числом\n";
        return 1;
    }

    try
    {
        std::vector<std::jthread> threads;
        threads.reserve(threadCount);
        for (int i = 1; i <= threadCount; ++i)
        {
            threads.emplace_back(Worker, i);
        }
        // Здесь заканчивается область видимости вектора: деструктор каждого
        // jthread делает join, поэтому main не завершится раньше потоков.
    }
    catch (const std::system_error& e)
    {
        std::cerr << std::format("Не удалось создать поток: {}\n", e.what());
        return 2;
    }

    return 0;
}

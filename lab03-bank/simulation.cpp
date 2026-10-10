#include "simulation.h"
#include "wallet.h"

#include <array>
#include <format>
#include <iostream>
#include <random>
#include <string_view>
#include <syncstream>
#include <thread>
#include <vector>

namespace sim
{

std::atomic<bool> g_stopRequested{false};

namespace
{

enum Person
{
    Director,
    Father,
    Mother,
    Son,
    Daughter,
    Seller,
    PersonCount,
};

struct PersonInfo
{
    std::string_view name;   // кто: «Сын»
    std::string_view dative; // кому: «Сыну»
    std::string_view bought; // «купил» / «купила»
};

constexpr std::array<PersonInfo, PersonCount> People = {{
    {"Директор", "Директору", "купил"},
    {"Отец", "Отцу", "купил"},
    {"Мать", "Матери", "купила"},
    {"Сын", "Сыну", "купил"},
    {"Дочь", "Дочери", "купила"},
    {"Продавец", "Продавцу", "купил"},
}};

constexpr Money InitialCash = 10'000;  // все деньги в экономике
constexpr Money PlantDeposit = 5'000;  // сразу лежат на счёте электростанции
constexpr Money WalletStart = 1'000;   // у каждого, кроме Директора, в кошельке

// Общие данные симуляции: банк, счета и кошельки. Каждого участника обслуживает свой поток
// (или все по очереди один поток), поэтому всё, что здесь меняется, защищено:
// банк и кошельки — своими мьютексами, а номера счетов после конструктора только читаются.
class Economy
{
public:
    explicit Economy(bool quiet)
        : m_bank(InitialCash)
        , m_quiet(quiet)
    {
        m_plantAccount = m_bank.OpenAccount();
        m_fatherAccount = m_bank.OpenAccount();
        m_sellerAccount = m_bank.OpenAccount();
        m_bank.DepositMoney(m_plantAccount, PlantDeposit);
        for (int p = Father; p < PersonCount; ++p)
            m_wallets[p].Add(WalletStart);
    }

    void MakeTurn(Person who, std::mt19937& rng)
    {
        switch (who)
        {
        case Director:
            DirectorTurn(rng);
            break;
        case Father:
            FatherTurn(rng);
            break;
        case Mother:
        case Son:
        case Daughter:
            ShopperTurn(who, rng);
            break;
        case Seller:
            SellerTurn(rng);
            break;
        default:
            break;
        }
    }

    Money Total() const { return m_bank.GetCash() + BalancesTotal(); }

    Money BalancesTotal() const
    {
        return m_bank.GetAccountBalance(m_plantAccount) + m_bank.GetAccountBalance(m_fatherAccount)
            + m_bank.GetAccountBalance(m_sellerAccount);
    }

    Money WalletsTotal() const
    {
        Money total = 0;
        for (const Wallet& wallet : m_wallets)
            total += wallet.GetAmount();
        return total;
    }

    const Bank& GetBank() const { return m_bank; }

private:
    // Директор электростанции платит Отцу зарплату со счёта станции.
    void DirectorTurn(std::mt19937& rng)
    {
        const Money salary = Random(rng, 100, 200);
        if (Transfer(Director, m_plantAccount, m_fatherAccount, salary))
            Log("Директор перечислил Отцу зарплату {}", salary);
        else
            SkipTurn(Director);
    }

    // Отец снимает наличные, платит за электричество или даёт деньги семье.
    void FatherTurn(std::mt19937& rng)
    {
        switch (Random(rng, 0, 3))
        {
        case 0:
        {
            const Money amount = Random(rng, 200, 600);
            if (!m_bank.TryWithdrawMoney(m_fatherAccount, amount))
                return SkipTurn(Father);
            m_wallets[Father].Add(amount);
            Log("Отец снял со счёта {}", amount);
            break;
        }
        case 1:
        {
            const Money bill = Random(rng, 20, 80);
            if (!Transfer(Father, m_fatherAccount, m_plantAccount, bill))
                return SkipTurn(Father);
            Log("Отец заплатил за электричество {}", bill);
            break;
        }
        default:
        {
            const auto to = static_cast<Person>(Random(rng, Mother, Daughter));
            const Money amount = Random(rng, 100, 300);
            if (!Give(Father, to, amount))
                return SkipTurn(Father);
            Log("Отец дал {} {}", People[to].dative, amount);
            break;
        }
        }
    }

    // Мать и дети покупают в магазине: наличные переходят в кошелёк Продавца.
    void ShopperTurn(Person who, std::mt19937& rng)
    {
        const Money price = Random(rng, 10, 70);
        if (!Give(who, Seller, price))
            return SkipTurn(who);
        Log("{} {} в магазине на {}", People[who].name, People[who].bought, price);
    }

    // Продавец кладёт выручку на счёт или платит за электричество.
    void SellerTurn(std::mt19937& rng)
    {
        if (Random(rng, 0, 1) == 0)
        {
            const Money amount = Random(rng, 150, 350);
            if (!PayFromWallet(Seller, m_sellerAccount, amount))
                return SkipTurn(Seller);
            Log("Продавец положил на счёт выручку {}", amount);
        }
        else
        {
            const Money bill = Random(rng, 150, 350);
            if (!Transfer(Seller, m_sellerAccount, m_plantAccount, bill))
                return SkipTurn(Seller);
            Log("Продавец заплатил за электричество {}", bill);
        }
    }

    // Наличные из кошелька в кошелёк. Мьютексы кошельков захватываются по очереди,
    // а не оба сразу, — поэтому взаимной блокировки быть не может.
    bool Give(Person from, Person to, Money amount)
    {
        if (!m_wallets[from].TrySpend(amount))
            return false;
        m_wallets[to].Add(amount);
        return true;
    }

    // Наличные из кошелька на счёт.
    bool PayFromWallet(Person who, AccountId to, Money amount)
    {
        if (!m_wallets[who].TrySpend(amount))
            return false;
        // Наличных в банке не меньше, чем было в кошельке, поэтому DepositMoney не бросит.
        m_bank.DepositMoney(to, amount);
        return true;
    }

    // Со счёта на счёт: банк умеет только снимать и класть, поэтому деньги проходят
    // через кошелёк участника. Снятие — TryWithdrawMoney: проверка и списание за один вызов.
    bool Transfer(Person who, AccountId from, AccountId to, Money amount)
    {
        if (!m_bank.TryWithdrawMoney(from, amount))
            return false;
        m_wallets[who].Add(amount);
        // Из кошелька тратит только его владелец, то есть этот же поток, —
        // только что положенные деньги никуда не делись.
        return PayFromWallet(who, to, amount);
    }

    void SkipTurn(Person who) { Log("{} пропускает ход: не хватает денег", People[who].name); }

    template <typename... Args>
    void Log(std::format_string<Args...> fmt, Args&&... args)
    {
        if (m_quiet)
            return;
        // osyncstream отдаёт строку в cout одним куском — строки разных потоков не перемешиваются.
        std::osyncstream(std::cout) << std::format(fmt, std::forward<Args>(args)...) << '\n';
    }

    static Money Random(std::mt19937& rng, Money min, Money max)
    {
        return std::uniform_int_distribution<Money>(min, max)(rng);
    }

    Bank m_bank;
    std::array<Wallet, PersonCount> m_wallets;
    AccountId m_plantAccount = 0;
    AccountId m_fatherAccount = 0;
    AccountId m_sellerAccount = 0;
    const bool m_quiet;
};

// Генератор случайных чисел у каждого участника свой: std::mt19937 не потокобезопасен.
std::mt19937 MakeRng(int person)
{
    return std::mt19937(static_cast<std::mt19937::result_type>(person + 1));
}

void RunSingle(Economy& economy, std::chrono::steady_clock::time_point deadline)
{
    std::array<std::mt19937, PersonCount> rngs;
    for (int p = 0; p < PersonCount; ++p)
        rngs[p] = MakeRng(p);

    while (std::chrono::steady_clock::now() < deadline)
    {
        for (int p = 0; p < PersonCount; ++p)
        {
            if (g_stopRequested)
                return;
            economy.MakeTurn(static_cast<Person>(p), rngs[p]);
        }
    }
}

void RunParallel(Economy& economy, std::chrono::steady_clock::time_point deadline)
{
    std::vector<std::jthread> threads;
    threads.reserve(PersonCount);
    for (int p = 0; p < PersonCount; ++p)
    {
        threads.emplace_back([&economy, p] {
            std::mt19937 rng = MakeRng(p);
            try
            {
                while (!g_stopRequested)
                    economy.MakeTurn(static_cast<Person>(p), rng);
            }
            catch (const std::exception& e)
            {
                // Исключение, вылетевшее из потока, вызывает std::terminate — ловим и останавливаем всех.
                std::osyncstream(std::cerr) << std::format("{}: ошибка: {}\n", People[p].name, e.what());
                g_stopRequested = true;
            }
        });
    }

    // Главный поток следит за временем; Ctrl+C тоже ставит флаг.
    while (!g_stopRequested && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    g_stopRequested = true;
    // Деструкторы jthread дожидаются всех участников.
}

} // namespace

Report Run(const Options& options)
{
    Economy economy(options.quiet);
    Report report;
    report.initialTotal = economy.Total();
    const unsigned long long operationsBefore = economy.GetBank().GetOperationsCount();

    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + options.duration;
    if (options.mode == Mode::Single)
        RunSingle(economy, deadline);
    else
        RunParallel(economy, deadline);
    const auto finish = std::chrono::steady_clock::now();

    // Все участники закончили — дальше банк и кошельки читает только этот поток.
    report.seconds = std::chrono::duration<double>(finish - start).count();
    report.operations = economy.GetBank().GetOperationsCount() - operationsBefore;
    report.cash = economy.GetBank().GetCash();
    report.walletsTotal = economy.WalletsTotal();
    report.balancesTotal = economy.BalancesTotal();
    return report;
}

} // namespace sim

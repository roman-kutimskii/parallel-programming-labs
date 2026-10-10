// Тесты класса Bank (и кошелька). Чтобы не тянуть внешние библиотеки, «фреймворк» —
// несколько макросов ниже: TEST регистрирует функцию, CHECK и CHECK_THROWS считают провалы.
#include "bank.h"
#include "wallet.h"

#include <atomic>
#include <format>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{

struct TestCase
{
    const char* name;
    std::function<void()> body;
};

std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

int g_failures = 0;

struct Registrar
{
    Registrar(const char* name, std::function<void()> body) { Registry().push_back({name, std::move(body)}); }
};

void ReportFailure(const char* file, int line, const std::string& what)
{
    ++g_failures;
    std::cerr << std::format("{}:{}: ПРОВАЛ: {}\n", file, line, what);
}

} // namespace

#define TEST(name)                                     \
    void name();                                       \
    const Registrar name##_registrar(#name, name);     \
    void name()

#define CHECK(expr)                                    \
    do                                                 \
    {                                                  \
        if (!(expr))                                   \
            ReportFailure(__FILE__, __LINE__, #expr);  \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                      \
    do                                                                                                  \
    {                                                                                                   \
        const auto a_ = (actual);                                                                       \
        const auto e_ = (expected);                                                                     \
        if (!(a_ == e_))                                                                                \
            ReportFailure(__FILE__, __LINE__, std::format("{} == {} (получили {}, ждали {})", #actual, \
                                                          #expected, a_, e_));                          \
    } while (false)

#define CHECK_THROWS(expr, exception)                                                       \
    do                                                                                      \
    {                                                                                       \
        bool thrown_ = false;                                                               \
        try                                                                                 \
        {                                                                                   \
            (void)(expr);                                                                   \
        }                                                                                   \
        catch (const exception&)                                                            \
        {                                                                                   \
            thrown_ = true;                                                                 \
        }                                                                                   \
        catch (...)                                                                         \
        {                                                                                   \
        }                                                                                   \
        if (!thrown_)                                                                       \
            ReportFailure(__FILE__, __LINE__, std::format("{} бросает {}", #expr, #exception)); \
    } while (false)

// ---------------------------------------------------------------- конструктор, счета

TEST(ConstructorSetsCash)
{
    Bank bank(1000);
    CHECK_EQ(bank.GetCash(), 1000);
}

TEST(ConstructorRejectsNegativeCash)
{
    CHECK_THROWS(Bank(-1), std::out_of_range);
}

TEST(OpenAccountStartsWithZeroAndIdsDiffer)
{
    Bank bank(100);
    const AccountId a = bank.OpenAccount();
    const AccountId b = bank.OpenAccount();
    CHECK(a != b);
    CHECK_EQ(bank.GetAccountBalance(a), 0);
    CHECK_EQ(bank.GetAccountBalance(b), 0);
    CHECK_EQ(bank.GetCash(), 100);
}

TEST(CloseAccountReturnsBalanceToCash)
{
    Bank bank(100);
    const AccountId a = bank.OpenAccount();
    bank.DepositMoney(a, 30);
    CHECK_EQ(bank.CloseAccount(a), 30);
    CHECK_EQ(bank.GetCash(), 100);
    CHECK_THROWS(bank.GetAccountBalance(a), BankOperationError);
}

TEST(CloseUnknownAccountThrows)
{
    Bank bank(100);
    CHECK_THROWS(bank.CloseAccount(42), BankOperationError);
    const AccountId a = bank.OpenAccount();
    bank.CloseAccount(a);
    CHECK_THROWS(bank.CloseAccount(a), BankOperationError); // повторное закрытие
    CHECK_EQ(bank.GetCash(), 100);
}

TEST(ClosedAccountIdIsNotReused)
{
    Bank bank(100);
    const AccountId a = bank.OpenAccount();
    bank.CloseAccount(a);
    CHECK(bank.OpenAccount() != a);
}

// ---------------------------------------------------------------- DepositMoney

TEST(DepositMovesCashToAccount)
{
    Bank bank(100);
    const AccountId a = bank.OpenAccount();
    bank.DepositMoney(a, 40);
    CHECK_EQ(bank.GetAccountBalance(a), 40);
    CHECK_EQ(bank.GetCash(), 60);
    bank.DepositMoney(a, 60); // все наличные
    CHECK_EQ(bank.GetAccountBalance(a), 100);
    CHECK_EQ(bank.GetCash(), 0);
    bank.DepositMoney(a, 0);
    CHECK_EQ(bank.GetAccountBalance(a), 100);
}

TEST(DepositErrorsChangeNothing)
{
    Bank bank(100);
    const AccountId a = bank.OpenAccount();
    bank.DepositMoney(a, 10);
    CHECK_THROWS(bank.DepositMoney(a, -1), std::out_of_range);
    CHECK_THROWS(bank.DepositMoney(a, 91), BankOperationError); // наличных 90
    CHECK_THROWS(bank.DepositMoney(a + 100, 5), BankOperationError);
    CHECK_EQ(bank.GetAccountBalance(a), 10);
    CHECK_EQ(bank.GetCash(), 90);
}

// ---------------------------------------------------------------- WithdrawMoney

TEST(WithdrawMovesAccountToCash)
{
    Bank bank(100);
    const AccountId a = bank.OpenAccount();
    bank.DepositMoney(a, 50);
    bank.WithdrawMoney(a, 20);
    CHECK_EQ(bank.GetAccountBalance(a), 30);
    CHECK_EQ(bank.GetCash(), 70);
    bank.WithdrawMoney(a, 30); // весь остаток
    CHECK_EQ(bank.GetAccountBalance(a), 0);
    CHECK_EQ(bank.GetCash(), 100);
}

TEST(WithdrawErrorsChangeNothing)
{
    Bank bank(100);
    const AccountId a = bank.OpenAccount();
    bank.DepositMoney(a, 50);
    CHECK_THROWS(bank.WithdrawMoney(a, -1), std::out_of_range);
    CHECK_THROWS(bank.WithdrawMoney(a, 51), BankOperationError);
    CHECK_THROWS(bank.WithdrawMoney(a + 100, 1), BankOperationError);
    CHECK_EQ(bank.GetAccountBalance(a), 50);
    CHECK_EQ(bank.GetCash(), 50);
}

// ---------------------------------------------------------------- TryWithdrawMoney

TEST(TryWithdrawSucceedsWhenEnough)
{
    Bank bank(100);
    const AccountId a = bank.OpenAccount();
    bank.DepositMoney(a, 50);
    CHECK(bank.TryWithdrawMoney(a, 50));
    CHECK_EQ(bank.GetAccountBalance(a), 0);
    CHECK_EQ(bank.GetCash(), 100);
}

TEST(TryWithdrawReturnsFalseWhenNotEnough)
{
    Bank bank(100);
    const AccountId a = bank.OpenAccount();
    bank.DepositMoney(a, 50);
    CHECK(!bank.TryWithdrawMoney(a, 51));
    CHECK_EQ(bank.GetAccountBalance(a), 50);
    CHECK_EQ(bank.GetCash(), 50);
}

TEST(TryWithdrawErrorsChangeNothing)
{
    Bank bank(100);
    const AccountId a = bank.OpenAccount();
    bank.DepositMoney(a, 50);
    CHECK_THROWS(bank.TryWithdrawMoney(a, -1), std::out_of_range);
    CHECK_THROWS(bank.TryWithdrawMoney(a + 100, 1), BankOperationError);
    CHECK_EQ(bank.GetAccountBalance(a), 50);
    CHECK_EQ(bank.GetCash(), 50);
}

// ---------------------------------------------------------------- GetAccountBalance, счётчик

TEST(GetBalanceOfUnknownAccountThrows)
{
    Bank bank(100);
    CHECK_THROWS(bank.GetAccountBalance(1), BankOperationError);
}

TEST(OperationsCountIncludesFailedCalls)
{
    Bank bank(100);
    CHECK_EQ(bank.GetOperationsCount(), 0ULL);
    const AccountId a = bank.OpenAccount();               // 1
    bank.DepositMoney(a, 10);                             // 2
    bank.WithdrawMoney(a, 5);                             // 3
    bank.TryWithdrawMoney(a, 1000);                       // 4 — false, но считается
    CHECK_THROWS(bank.DepositMoney(a, -1), std::out_of_range);          // 5
    CHECK_THROWS(bank.WithdrawMoney(a, 1000), BankOperationError);      // 6
    CHECK_THROWS(bank.GetAccountBalance(a + 1), BankOperationError);    // 7
    (void)bank.GetAccountBalance(a);                      // 8
    (void)bank.GetCash();                                 // 9
    bank.CloseAccount(a);                                 // 10
    CHECK_EQ(bank.GetOperationsCount(), 10ULL);
    CHECK_EQ(bank.GetOperationsCount(), 10ULL); // сам GetOperationsCount не считается
}

// ---------------------------------------------------------------- кошелёк

TEST(WalletAddAndSpend)
{
    Wallet wallet(10);
    wallet.Add(5);
    CHECK_EQ(wallet.GetAmount(), 15);
    CHECK(wallet.TrySpend(15));
    CHECK_EQ(wallet.GetAmount(), 0);
    CHECK(!wallet.TrySpend(1));
    CHECK_EQ(wallet.GetAmount(), 0);
    CHECK_THROWS(wallet.Add(-1), std::out_of_range);
    CHECK_THROWS(wallet.TrySpend(-1), std::out_of_range);
}

// ---------------------------------------------------------------- многопоточность

TEST(ParallelDepositWithdrawKeepsMoney)
{
    constexpr int ThreadCount = 8;
    constexpr int Iterations = 100'000;

    Bank bank(1000);
    const AccountId a = bank.OpenAccount();
    bank.DepositMoney(a, 500);
    const Money cashBefore = bank.GetCash();
    const Money balanceBefore = bank.GetAccountBalance(a);
    const unsigned long long opsBefore = bank.GetOperationsCount();
    // Исключение, вылетевшее из потока, убивает всю программу, поэтому ловим его внутри потока.
    // В правильном банке исключений нет: каждый поток кладёт 1 и тут же снимает 1.
    std::atomic<int> errors{0};

    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < ThreadCount; ++t)
        {
            threads.emplace_back([&bank, &errors, a] {
                try
                {
                    for (int i = 0; i < Iterations; ++i)
                    {
                        bank.DepositMoney(a, 1);
                        bank.WithdrawMoney(a, 1);
                    }
                }
                catch (const std::exception&)
                {
                    ++errors;
                }
            });
        }
    } // join

    CHECK_EQ(errors.load(), 0);
    CHECK_EQ(bank.GetOperationsCount() - opsBefore, 1'600'000ULL);
    CHECK_EQ(bank.GetCash(), cashBefore);
    CHECK_EQ(bank.GetAccountBalance(a), balanceBefore);
}

TEST(ParallelWalletKeepsMoney)
{
    constexpr int Iterations = 100'000;
    Wallet from(Iterations);
    Wallet to;
    {
        // Один поток перекладывает по 1, другой тратит из «to», сколько получится.
        std::jthread giver([&] {
            for (int i = 0; i < Iterations; ++i)
                if (from.TrySpend(1))
                    to.Add(1);
        });
        std::jthread spender([&] {
            for (int spent = 0; spent < Iterations / 2;)
                if (to.TrySpend(1))
                    ++spent;
        });
    }
    CHECK_EQ(from.GetAmount(), 0);
    CHECK_EQ(to.GetAmount(), Iterations / 2);
}

int main()
{
    for (const TestCase& test : Registry())
    {
        const int failuresBefore = g_failures;
        try
        {
            test.body();
        }
        catch (const std::exception& e)
        {
            ReportFailure(__FILE__, __LINE__, std::format("{}: неожиданное исключение: {}", test.name, e.what()));
        }
        std::cout << std::format("[{}] {}\n", g_failures == failuresBefore ? " OK " : "FAIL", test.name);
    }
    std::cout << std::format("Тестов: {}, провалов проверок: {}\n", Registry().size(), g_failures);
    return g_failures == 0 ? 0 : 1;
}

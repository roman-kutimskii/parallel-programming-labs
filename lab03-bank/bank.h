#pragma once

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

using AccountId = unsigned long long;
using Money = long long;

// Нет счёта или не хватает денег
class BankOperationError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// Банк со счетами и наличными в обороте. Все методы можно вызывать из разных потоков.
// Сумма наличных и денег на всех счетах не меняется; ни баланс, ни наличные не уходят в минус.
// Отрицательная сумма — std::out_of_range; нет счёта или не хватает денег — BankOperationError.
// Если метод бросил исключение, состояние банка не изменилось.
class Bank
{
public:
    explicit Bank(Money cash);                               // cash — наличные в обороте

    AccountId OpenAccount();                                 // новый счёт с нулём
    Money CloseAccount(AccountId account);                   // остаток уходит в наличные

    void DepositMoney(AccountId account, Money amount);      // наличные -> счёт
    void WithdrawMoney(AccountId account, Money amount);     // счёт -> наличные
    bool TryWithdrawMoney(AccountId account, Money amount);  // false, если не хватает

    Money GetAccountBalance(AccountId account) const;
    Money GetCash() const;
    unsigned long long GetOperationsCount() const;

private:
    // Вызываются под захваченным m_mutex.
    Money& BalanceLocked(AccountId account);
    const Money& BalanceLocked(AccountId account) const;

    mutable std::mutex m_mutex;  // mutable — чтобы захватывать мьютекс в const-методах
    // Защищены m_mutex: наличные и балансы меняются вместе, под одним мьютексом.
    std::unordered_map<AccountId, Money> m_accounts;
    Money m_cash = 0;
    AccountId m_nextAccountId = 1;

    // Счётчик живёт отдельно от мьютекса: атомарный инкремент сам по себе не даёт гонки.
    // mutable — неудачные и const-вызовы тоже считаются.
    mutable std::atomic<unsigned long long> m_operationsCount{0};
};

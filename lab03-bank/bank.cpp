#include "bank.h"

#include <format>

namespace
{

void CheckAmount(Money amount)
{
    if (amount < 0)
        throw std::out_of_range(std::format("Отрицательная сумма: {}", amount));
}

} // namespace

Bank::Bank(Money cash)
    : m_cash(cash)
{
    CheckAmount(cash);
}

AccountId Bank::OpenAccount()
{
    ++m_operationsCount;
    std::lock_guard lock(m_mutex);
    const AccountId id = m_nextAccountId++;
    m_accounts.emplace(id, 0);
    return id;
}

Money Bank::CloseAccount(AccountId account)
{
    ++m_operationsCount;
    std::lock_guard lock(m_mutex);
    const Money balance = BalanceLocked(account);
    m_cash += balance;
    m_accounts.erase(account);
    return balance;
}

void Bank::DepositMoney(AccountId account, Money amount)
{
    ++m_operationsCount;
    CheckAmount(amount);
    std::lock_guard lock(m_mutex);
    // Сначала все проверки, потом изменения: если бросим исключение, банк останется прежним.
    Money& balance = BalanceLocked(account);
    if (m_cash < amount)
        throw BankOperationError(std::format("Не хватает наличных: {} < {}", m_cash, amount));
    m_cash -= amount;
    balance += amount;
}

void Bank::WithdrawMoney(AccountId account, Money amount)
{
    // Не через TryWithdrawMoney: иначе один вызов посчитался бы дважды.
    ++m_operationsCount;
    CheckAmount(amount);
    std::lock_guard lock(m_mutex);
    Money& balance = BalanceLocked(account);
    if (balance < amount)
        throw BankOperationError(std::format("Не хватает денег на счёте {}: {} < {}", account, balance, amount));
    balance -= amount;
    m_cash += amount;
}

bool Bank::TryWithdrawMoney(AccountId account, Money amount)
{
    ++m_operationsCount;
    CheckAmount(amount);
    // Проверка баланса и списание — под одним захватом мьютекса,
    // поэтому между ними никто не успеет снять эти же деньги.
    std::lock_guard lock(m_mutex);
    Money& balance = BalanceLocked(account);
    if (balance < amount)
        return false;
    balance -= amount;
    m_cash += amount;
    return true;
}

Money Bank::GetAccountBalance(AccountId account) const
{
    ++m_operationsCount;
    std::lock_guard lock(m_mutex);
    return BalanceLocked(account);
}

Money Bank::GetCash() const
{
    ++m_operationsCount;
    // Захватываем мьютекс и для чтения: другой поток может в этот момент менять m_cash.
    std::lock_guard lock(m_mutex);
    return m_cash;
}

unsigned long long Bank::GetOperationsCount() const
{
    return m_operationsCount;
}

Money& Bank::BalanceLocked(AccountId account)
{
    const auto it = m_accounts.find(account);
    if (it == m_accounts.end())
        throw BankOperationError(std::format("Нет счёта {}", account));
    return it->second;
}

const Money& Bank::BalanceLocked(AccountId account) const
{
    const auto it = m_accounts.find(account);
    if (it == m_accounts.end())
        throw BankOperationError(std::format("Нет счёта {}", account));
    return it->second;
}

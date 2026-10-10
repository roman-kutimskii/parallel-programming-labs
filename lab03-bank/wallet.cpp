#include "wallet.h"

#include <format>
#include <stdexcept>

namespace
{

void CheckAmount(Money amount)
{
    if (amount < 0)
        throw std::out_of_range(std::format("Отрицательная сумма: {}", amount));
}

} // namespace

Wallet::Wallet(Money amount)
    : m_amount(amount)
{
    CheckAmount(amount);
}

void Wallet::Add(Money amount)
{
    CheckAmount(amount);
    std::lock_guard lock(m_mutex);
    m_amount += amount;
}

bool Wallet::TrySpend(Money amount)
{
    CheckAmount(amount);
    std::lock_guard lock(m_mutex);
    if (m_amount < amount)
        return false;
    m_amount -= amount;
    return true;
}

Money Wallet::GetAmount() const
{
    std::lock_guard lock(m_mutex);
    return m_amount;
}

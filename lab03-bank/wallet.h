#pragma once

#include "bank.h"

#include <mutex>

// Наличные на руках у участника. В кошелёк кладут деньги из чужих потоков
// (Отец — Сыну), а тратит владелец в своём, поэтому у кошелька свой мьютекс.
class Wallet
{
public:
    explicit Wallet(Money amount = 0);

    void Add(Money amount);

    // Проверка «хватает ли» и списание — одна операция под одним захватом мьютекса.
    // Возвращает false и ничего не меняет, если денег меньше amount.
    bool TrySpend(Money amount);

    Money GetAmount() const;

private:
    mutable std::mutex m_mutex;
    Money m_amount = 0;
};

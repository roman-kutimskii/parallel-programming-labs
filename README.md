# Параллельное программирование — лабораторные работы

Эталонные решения лабораторных работ курса «Параллельное программирование».
Каждая лаба живёт в своём каталоге и собирается общим CMake-проектом.

| # | Каталог | Тема |
|---|---------|------|
| 1 | [`lab01-threads`](lab01-threads) | Работа с потоками, `std::jthread` |
| 2 | [`lab02-blur`](lab02-blur) | Размытие BMP в N потоках, замеры ускорения |

## Сборка

Все лабы разом, из корня репозитория:

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Бинарники оказываются в `build/<каталог лабы>/`.

Собрать одну лабу:

```
cmake --build build --target lab01-threads
```

## Требования

Компилятор с поддержкой C++20 — GCC 13+, Clang 17+ или MSVC 19.29+
(нужны `std::jthread`, `std::format`, `std::osyncstream`). CMake 3.20+.

В libc++ (Clang, Apple Clang) `std::osyncstream` пока экспериментальный, поэтому
для Clang CMake-проект добавляет флаг `-fexperimental-library`.

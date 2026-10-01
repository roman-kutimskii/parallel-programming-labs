// Лабораторная 2: размытие BMP в N потоках.
// Схемы деления: h — горизонтальные полосы, v — вертикальные, s — N² квадратов.
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace
{

struct Image
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // RGB, по 3 байта, строки сверху вниз, без выравнивания
};

// Прямоугольник пикселей [x0, x1) × [y0, y1)
struct Rect
{
    int x0, y0, x1, y1;
};

template <typename T>
T ReadLE(const std::uint8_t* p)
{
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i)
        value |= static_cast<T>(static_cast<T>(p[i]) << (8 * i));
    return value;
}

template <typename T>
void WriteLE(std::uint8_t* p, T value)
{
    for (std::size_t i = 0; i < sizeof(T); ++i)
        p[i] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(value) >> (8 * i));
}

// Читает несжатый BMP 24 или 32 бита на пиксель.
Image LoadBmp(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("не удалось открыть " + path);
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), {});
    if (data.size() < 54 || data[0] != 'B' || data[1] != 'M')
        throw std::runtime_error(path + ": не BMP");

    const auto offset = ReadLE<std::uint32_t>(&data[10]);
    const auto width = ReadLE<std::int32_t>(&data[18]);
    const auto rawHeight = ReadLE<std::int32_t>(&data[22]);
    const auto bpp = ReadLE<std::uint16_t>(&data[28]);
    const auto compression = ReadLE<std::uint32_t>(&data[30]);
    if ((bpp != 24 && bpp != 32) || (compression != 0 && compression != 3) || width <= 0 || rawHeight == 0)
        throw std::runtime_error(path + ": поддерживается только несжатый BMP 24/32 бита");

    const bool bottomUp = rawHeight > 0;
    const int height = bottomUp ? rawHeight : -rawHeight;
    const int bytesPerPixel = bpp / 8;
    const std::size_t stride = (static_cast<std::size_t>(width) * bytesPerPixel + 3) / 4 * 4;
    if (offset + stride * height > data.size())
        throw std::runtime_error(path + ": файл обрезан");

    Image img{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 3)};
    for (int y = 0; y < height; ++y)
    {
        const int srcRow = bottomUp ? height - 1 - y : y;
        const std::uint8_t* src = &data[offset + stride * srcRow];
        std::uint8_t* dst = &img.pixels[static_cast<std::size_t>(y) * width * 3];
        for (int x = 0; x < width; ++x)
        {
            dst[3 * x + 0] = src[bytesPerPixel * x + 2]; // BMP хранит BGR
            dst[3 * x + 1] = src[bytesPerPixel * x + 1];
            dst[3 * x + 2] = src[bytesPerPixel * x + 0];
        }
    }
    return img;
}

void SaveBmp(const std::string& path, const Image& img)
{
    const std::size_t stride = (static_cast<std::size_t>(img.width) * 3 + 3) / 4 * 4;
    const std::size_t size = 54 + stride * img.height;
    std::vector<std::uint8_t> data(size, 0);
    data[0] = 'B';
    data[1] = 'M';
    WriteLE<std::uint32_t>(&data[2], static_cast<std::uint32_t>(size));
    WriteLE<std::uint32_t>(&data[10], 54);
    WriteLE<std::uint32_t>(&data[14], 40);
    WriteLE<std::int32_t>(&data[18], img.width);
    WriteLE<std::int32_t>(&data[22], img.height); // bottom-up
    WriteLE<std::uint16_t>(&data[26], 1);
    WriteLE<std::uint16_t>(&data[28], 24);
    for (int y = 0; y < img.height; ++y)
    {
        const std::uint8_t* src = &img.pixels[static_cast<std::size_t>(y) * img.width * 3];
        std::uint8_t* dst = &data[54 + stride * (img.height - 1 - y)];
        for (int x = 0; x < img.width; ++x)
        {
            dst[3 * x + 0] = src[3 * x + 2];
            dst[3 * x + 1] = src[3 * x + 1];
            dst[3 * x + 2] = src[3 * x + 0];
        }
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size())))
        throw std::runtime_error("не удалось записать " + path);
}

// Box blur радиуса r: среднее по квадрату (2r+1)², за границей — ближайший пиксель края.
// Читаем ТОЛЬКО из src, пишем ТОЛЬКО в свои пиксели dst — поэтому потокам
// не нужна синхронизация: общие данные только читаются, а записи не пересекаются.
void BlurRect(const Image& src, Image& dst, Rect rc, int r)
{
    const int w = src.width;
    const int h = src.height;
    const int area = (2 * r + 1) * (2 * r + 1);
    for (int y = rc.y0; y < rc.y1; ++y)
    {
        for (int x = rc.x0; x < rc.x1; ++x)
        {
            int sum[3] = {0, 0, 0};
            for (int dy = -r; dy <= r; ++dy)
            {
                const int sy = std::clamp(y + dy, 0, h - 1);
                const std::uint8_t* row = &src.pixels[static_cast<std::size_t>(sy) * w * 3];
                for (int dx = -r; dx <= r; ++dx)
                {
                    const int sx = std::clamp(x + dx, 0, w - 1);
                    sum[0] += row[3 * sx + 0];
                    sum[1] += row[3 * sx + 1];
                    sum[2] += row[3 * sx + 2];
                }
            }
            std::uint8_t* out = &dst.pixels[(static_cast<std::size_t>(y) * w + x) * 3];
            for (int c = 0; c < 3; ++c)
                out[c] = static_cast<std::uint8_t>((sum[c] + area / 2) / area);
        }
    }
}

// i-я из n частей отрезка [0, size): границы size*i/n — части отличаются не больше чем на 1
int Split(int size, int i, int n)
{
    return static_cast<int>(static_cast<long long>(size) * i / n);
}

// Для каждого потока — список его прямоугольников.
std::vector<std::vector<Rect>> MakeWork(int w, int h, int n, char scheme)
{
    std::vector<std::vector<Rect>> work(n);
    for (int k = 0; k < n; ++k)
    {
        if (scheme == 'h')
            work[k].push_back({0, Split(h, k, n), w, Split(h, k + 1, n)});
        else if (scheme == 'v')
            work[k].push_back({Split(w, k, n), 0, Split(w, k + 1, n), h});
    }
    if (scheme == 's')
    {
        // Сетка n×n. Квадрат (row, col) достаётся потоку (row + col) % n:
        // у каждого потока ровно n квадратов, по одному в каждой строке и столбце сетки.
        for (int row = 0; row < n; ++row)
            for (int col = 0; col < n; ++col)
                work[(row + col) % n].push_back(
                    {Split(w, col, n), Split(h, row, n), Split(w, col + 1, n), Split(h, row + 1, n)});
    }
    return work;
}

void Worker(const Image& src, Image& dst, std::vector<Rect> rects, int radius)
{
    for (const Rect& rc : rects)
        BlurRect(src, dst, rc, radius);
}

// Возвращает 0, если аргумент не является положительным целым.
int ParsePositive(std::string_view arg)
{
    int value = 0;
    const auto [end, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), value);
    const bool parsed = (ec == std::errc{}) && (end == arg.data() + arg.size());
    return (parsed && value > 0) ? value : 0;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 5 || argc > 6)
    {
        std::cerr << std::format("Usage: {} <input.bmp> <output.bmp> <threads> <h|v|s> [radius=5]\n", argv[0]);
        return 1;
    }
    const int threads = ParsePositive(argv[3]);
    const std::string_view schemeArg = argv[4];
    const int radius = argc == 6 ? ParsePositive(argv[5]) : 5;
    if (threads == 0 || radius == 0 || schemeArg.size() != 1 || std::string_view("hvs").find(schemeArg[0]) == std::string_view::npos)
    {
        std::cerr << "threads и radius — целые > 0, схема — h, v или s\n";
        return 1;
    }
    const char scheme = schemeArg[0];

    try
    {
        const Image src = LoadBmp(argv[1]);
        Image dst{src.width, src.height, std::vector<std::uint8_t>(src.pixels.size())};
        auto work = MakeWork(src.width, src.height, threads, scheme);

        // Замеряем только обработку: чтение и запись файла — за пределами.
        const auto start = std::chrono::steady_clock::now();
        {
            std::vector<std::jthread> pool;
            pool.reserve(threads);
            for (int k = 0; k < threads; ++k)
                pool.emplace_back(Worker, std::cref(src), std::ref(dst), std::move(work[k]), radius);
        } // join всех потоков
        const auto finish = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(finish - start).count();

        SaveBmp(argv[2], dst);
        // Одна строка CSV: потоки, схема, радиус, аппаратные потоки, время в мс
        std::cout << std::format("{},{},{},{},{:.3f}\n", threads, scheme, radius,
                                 std::thread::hardware_concurrency(), ms);
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
    return 0;
}

#pragma once

#include <cmath>

#include <compare>

template <typename T>
struct Vec2T
{
    T x{};
    T y{};

    Vec2T() = default;
    constexpr Vec2T(T x, T y):
        x{ x }, y{ y } { }
    constexpr Vec2T(T xy): Vec2T{ xy, xy } { }

    constexpr T mag2() const
    {
        return x * x + y * y;
    }

    friend constexpr Vec2T operator+(const Vec2T& a, const Vec2T& b)
    {
        return { a.x + b.x,
                 a.y + b.y };
    }

    friend constexpr Vec2T operator-(const Vec2T& a, const Vec2T& b)
    {
        return { a.x - b.x,
                 a.y - b.y };
    }

    friend constexpr Vec2T operator*(const Vec2T& a, const Vec2T& b)
    {
        return { a.x * b.x,
                 a.y * b.y };
    }

    friend constexpr Vec2T operator/(const Vec2T& a, const Vec2T& b)
    {
        return { a.x / b.x,
                 a.y / b.y };
    }

    auto operator<=>(const Vec2T&) const = default;
};

template <typename T>
struct Vec4T
{
    T x{};
    T y{};
    T z{};
    T a{};

    Vec4T() = default;
    constexpr Vec4T(T x, T y, T z, T a):
        x{ x }, y{ y }, z{ z }, a{ a } { }
    constexpr Vec4T(T xyza): Vec4T{ xyza, xyza, xyza, xyza } { }

    friend constexpr Vec4T operator+(const Vec4T& a, const Vec4T& b)
    {
        return { a.x + b.x,
                 a.y + b.y,
                 a.z + b.z,
                 a.a + b.a };
    }

    friend constexpr Vec4T operator-(const Vec4T& a, const Vec4T& b)
    {
        return { a.x - b.x,
                 a.y - b.y,
                 a.z - b.z,
                 a.a - b.a };
    }

    friend constexpr Vec4T operator*(const Vec4T& a, const Vec4T& b)
    {
        return { a.x * b.x,
                 a.y * b.y,
                 a.z * b.z,
                 a.a * b.a };
    }

    auto operator<=>(const Vec4T&) const = default;
};

using Vec2f = Vec2T<float>;
using Vec4f = Vec4T<float>;

using Vec2d = Vec2T<double>;
using Vec4d = Vec4T<double>;

using Vec2i = Vec2T<int>;
using Vec4i = Vec4T<int>;

constexpr Vec4f hex_to_vec4f(uint32_t color)
{
    uint32_t r = (color >> 24) & 0xFF;
    uint32_t g = (color >> 16) & 0xFF;
    uint32_t b = (color >> 8)  & 0xFF;
    uint32_t a = (color >> 0)  & 0xFF;
    return { r/255.0f,
             g/255.0f,
             b/255.0f,
             a/255.0f };
}

constexpr uint32_t vec4f_to_hex(const Vec4f& color)
{
    uint32_t r = static_cast<uint32_t>(color.x * 255.f);
    uint32_t g = static_cast<uint32_t>(color.y * 255.f);
    uint32_t b = static_cast<uint32_t>(color.z * 255.f);
    uint32_t a = static_cast<uint32_t>(color.a * 255.f);

    uint32_t result = (r << 24)
                    | (g << 16)
                    | (b << 8)
                    | (a << 0);
    return result;
}

template <typename T>
Vec2T<T> abs(const Vec2T<T>& v)
{
    return { std::abs(v.x), std::abs(v.y) };
}

constexpr Vec4f invert_color(const Vec4f& color)
{
    auto inv = 1.f - color;
    inv.a = color.a;
    return inv;
}

constexpr uint32_t color_rgb(const Vec4f& color)
{
    auto hex = vec4f_to_hex(color);
    // Chop the alpha.
    hex >>= 8;
    return hex;
}

template <typename T, typename U>
constexpr Vec2T<T> ease_expon(Vec2T<T> value, U delta_time)
{
    const T ease_weight = T(1.) - std::pow(T(2.), (T(-40.) * delta_time));
    value = value - value * ease_weight;

    if (std::abs(value.x) < T(0.005))
    {
        value.x = 0;
    }

    if (std::abs(value.y) < T(0.005))
    {
        value.y = 0;
    }
    return value;
}

template <typename T, typename U>
constexpr Vec2T<T> ease_expon_val(Vec2T<T> value, U delta_time, T speed)
{
    const T ease_weight = T(1.) - std::pow(T(2.), (-speed * delta_time));
    value = value - value * ease_weight;

    if (std::abs(value.x) < T(0.005))
    {
        value.x = 0;
    }

    if (std::abs(value.y) < T(0.005))
    {
        value.y = 0;
    }
    return value;
}
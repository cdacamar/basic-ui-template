#pragma once

#include "enum-utils.h"

enum class Width { };
enum class Height { };

struct ScreenDimensions
{
    Width width;
    Height height;
};

enum class FPS { };

// For Graphics.
struct OpaqueWindow
{
    void* value;
};

namespace Text
{
    enum class ID : size_t
    {
        Anonymous = sentinel_for<ID>
    };

    enum class Column : size_t
    {
        Beginning
    };

    enum class Tabstop { };

    enum class CursorLocus : size_t
    {
        Beginning,
        SelectionSentinel = sentinel_for<CursorLocus>
    };

    enum class LineEndingMode : uint8_t
    {
        Auto,
        CRLF,
        LF
    };

    enum class Length : size_t { };

    constexpr CursorLocus operator+(CursorLocus a, CursorLocus b)
    {
        return CursorLocus{ rep(a) + rep(b) };
    }

    constexpr CursorLocus operator+(CursorLocus l, PrimitiveType<CursorLocus> off)
    {
        return l + CursorLocus{ off };
    }

    constexpr CursorLocus operator+(CursorLocus l, Length len)
    {
        return l + CursorLocus{ rep(len) };
    }

    constexpr Length distance(CursorLocus first, CursorLocus last)
    {
        return Length{ rep(last) - rep(first) };
    }

    enum class CharOffset : size_t
    {
        Sentinel = sentinel_for<CharOffset>
    };

    constexpr CursorLocus seek(CharOffset off, Column c = Column::Beginning)
    {
        return CursorLocus{ rep(off) + rep(c) };
    }

    constexpr CharOffset as_offset(CursorLocus locus)
    {
        return CharOffset{ rep(locus) };
    }

    constexpr CharOffset operator+(CharOffset off, Length len)
    {
        return CharOffset{ rep(off) + rep(len) };
    }

    constexpr Column col(CharOffset first, CharOffset last)
    {
        return Column{ rep(last) - rep(first) };
    }

    constexpr Column col(CharOffset first, CursorLocus cursor)
    {
        return col(first, CharOffset{ rep(cursor) });
    }

    constexpr Length distance(CharOffset first, CharOffset last)
    {
        return Length{ rep(last) - rep(first) };
    }

    constexpr Length operator+(Length lhs, Length rhs)
    {
        return Length{ rep(lhs) + rep(rhs) };
    }

    constexpr Length operator-(Length lhs, Length rhs)
    {
        return Length{ rep(lhs) - rep(rhs) };
    }

    enum class EditSort
    {
        Insert,
        Deletion
    };

    struct GenericEdit
    {
        CharOffset first;
        union
        {
            Length len;      // EditSort::Insertion.
            CharOffset last; // EditSort::Deletion.
        };
        EditSort sort;
    };

    enum class CursorLine : size_t
    {
        IndexBeginning,
        Beginning
    };

    struct SearchResult
    {
        CharOffset first;
        CharOffset last;
        CursorLine line;
    };
} // namespace Editor

namespace Glyph
{
    enum class FontSize { };

    enum class Tabstop { };
} // namespace Glyph
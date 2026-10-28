#pragma once

#include <stddef.h>
#include <stdint.h>
#include <type_traits>

struct PhysicalTag {};
struct VirtualTag {};

template <typename Tag>
class Addr {
    public:
    using TagType = Tag;
    using RawType = uintptr_t;

    static constexpr RawType PAGE_SIZE_BYTES = 4096;
    static constexpr RawType PAGE_OFFSET_MASK = PAGE_SIZE_BYTES - 1;

    static_assert(std::is_same_v<Tag, PhysicalTag> ||
                  std::is_same_v<Tag, VirtualTag>);

    constexpr Addr() noexcept : val_(0) {};
    constexpr explicit Addr(RawType val) noexcept : val_(val) {};

    // Raw construction preserves bits; callers validate external addresses explicitly.
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        if constexpr (std::is_same_v<Tag, PhysicalTag>) {
            return (val_ >> 56U) == 0;
        } else {
            const RawType upper = val_ >> 39U;
            const RawType sign = (val_ >> 38U) & RawType{1};
            return upper == (sign == 0 ? 0 : ((RawType{1} << 25U) - 1));
        }
    }

    template <typename T>
    explicit Addr(T* ptr) noexcept :
    val_(reinterpret_cast<RawType>(ptr)) {};

    [[nodiscard]]
    constexpr RawType val() const noexcept {
        return val_;
    }

    template <typename T = void>
    [[nodiscard]] T* as_ptr() const noexcept {
        return reinterpret_cast<T*>(val_);
    }

    [[nodiscard]] constexpr bool is_page_aligned() const noexcept {
        return (val_ & PAGE_OFFSET_MASK) == 0;
    }

    [[nodiscard]] constexpr Addr page_down() const noexcept {
        return Addr(val_ & ~PAGE_OFFSET_MASK);
    }

    [[nodiscard]] constexpr Addr page_up() const noexcept {
        Addr result;
        if (!try_page_up(result)) {
            __builtin_trap();
        }
        return result;
    }

    [[nodiscard]] constexpr bool try_page_up(Addr& result) const noexcept {
        const RawType offset = page_offset();
        return try_add(offset == 0 ? 0 : PAGE_SIZE_BYTES - offset, result);
    }

    [[nodiscard]] constexpr bool try_add(size_t offset, Addr& result) const noexcept {
        if (!is_valid() || offset > UINTPTR_MAX - val_) {
            return false;
        }
        const Addr candidate(val_ + offset);
        if (!candidate.is_valid()) {
            return false;
        }
        result = candidate;
        return true;
    }

    [[nodiscard]] constexpr bool try_sub(size_t offset, Addr& result) const noexcept {
        if (!is_valid() || offset > val_) {
            return false;
        }
        const Addr candidate(val_ - offset);
        if (!candidate.is_valid()) {
            return false;
        }
        result = candidate;
        return true;
    }

    [[nodiscard]] constexpr size_t page_offset() const noexcept {
        return val_ & PAGE_OFFSET_MASK;
    }

    constexpr Addr operator+(size_t offset) const noexcept {
        Addr result;
        if (!try_add(offset, result)) {
            __builtin_trap();
        }
        return result;
    }

    constexpr Addr operator-(size_t offset) const noexcept {
        Addr result;
        if (!try_sub(offset, result)) {
            __builtin_trap();
        }
        return result;
    }

    constexpr ptrdiff_t operator-(Addr other) const noexcept {
        if (val_ >= other.val_) {
            const RawType distance = val_ - other.val_;
            if (distance > static_cast<RawType>(PTRDIFF_MAX)) {
                __builtin_trap();
            }
            return static_cast<ptrdiff_t>(distance);
        }
        const RawType distance = other.val_ - val_;
        const RawType negative_limit = static_cast<RawType>(PTRDIFF_MAX) + 1;
        if (distance > negative_limit) {
            __builtin_trap();
        }
        return distance == negative_limit ? PTRDIFF_MIN : -static_cast<ptrdiff_t>(distance);
    }

    constexpr Addr& operator+=(size_t offset) noexcept { *this = *this + offset; return *this; }
    constexpr Addr& operator-=(size_t offset) noexcept { *this = *this - offset; return *this; }

    constexpr auto operator==(const Addr& other) const { return this->val_ == other.val_; }
    constexpr bool operator<(const Addr& other) const  { return this->val_ < other.val_; };
    constexpr bool operator>(const Addr& other) const  { return this->val_ > other.val_; };
    constexpr auto operator<=(const Addr& other) const  { return this->val_ <= other.val_; }
    constexpr auto operator>=(const Addr& other) const  { return this->val_ >= other.val_; }

    private:
    RawType val_;
};

using PhysAddr = Addr<PhysicalTag>;
using VirtAddr = Addr<VirtualTag>;

template <typename Tag>
class PageNum {
    public:
    using RawType = uint64_t;
    static constexpr unsigned int VALUE_BITS =
        std::is_same_v<Tag, VirtualTag> ? 27U : 44U;
    static constexpr RawType VALUE_MASK = (RawType{1} << VALUE_BITS) - 1;

    static_assert(std::is_same_v<Tag, PhysicalTag> ||
                  std::is_same_v<Tag, VirtualTag>);

    constexpr PageNum() noexcept : val_(0) {};
    constexpr explicit PageNum (RawType val) noexcept : val_(val) {};

    // 从地址向下截断构造（最常用：无论是不是页首，提取其所在的页号）
    [[nodiscard]] static constexpr PageNum from_floor(Addr<Tag> addr) noexcept {
        // Also used by the legacy raw codecs: retain only the supported page bits.
        return PageNum((addr.val() >> 12U) & VALUE_MASK);
    }

    // 从地址向上取整构造（用于按页分配计算上限，如 [start, end) 虚拟区间）
    [[nodiscard]] static constexpr PageNum from_ceil(Addr<Tag> addr) noexcept {
        PageNum result;
        if (!try_from_ceil(addr, result)) {
            __builtin_trap();
        }
        return result;
    }

    [[nodiscard]] static constexpr bool try_from_ceil(
        Addr<Tag> addr, PageNum& result
    ) noexcept {
        Addr<Tag> aligned;
        if (!addr.try_page_up(aligned)) {
            return false;
        }
        result = from_floor(aligned);
        return true;
    }

    // 转回物理/虚拟地址（基地址）
    [[nodiscard]] constexpr Addr<Tag> to_addr() const noexcept {
        if (!is_valid()) {
            __builtin_trap();
        }
        RawType address = val_ << 12U;
        if constexpr (std::is_same_v<Tag, VirtualTag>) {
            if ((val_ & (RawType{1} << 26U)) != 0) {
                address |= ~((RawType{1} << 39U) - 1);
            }
        }
        return Addr<Tag>(static_cast<uintptr_t>(address));
    }

    [[nodiscard]] constexpr RawType val() const noexcept {
        return val_;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return val_ <= VALUE_MASK;
    }

    [[nodiscard]] constexpr unsigned int index(unsigned int level) const noexcept
        requires std::is_same_v<Tag, VirtualTag> {
        if (level >= 3U) {
            return 512U;
        }
        return static_cast<unsigned int>((val_ >> (9U * level)) & RawType{511});
    }

    [[nodiscard]] constexpr bool try_offset(int64_t pages, PageNum& result) const noexcept {
        if (!is_valid()) {
            return false;
        }
        if (pages >= 0) {
            const RawType count = static_cast<RawType>(pages);
            if (count > VALUE_MASK - val_) {
                return false;
            }
            result = PageNum(val_ + count);
        } else {
            const RawType count = static_cast<RawType>(-(pages + 1)) + 1;
            if (count > val_) {
                return false;
            }
            result = PageNum(val_ - count);
        }
        return true;
    }

    constexpr PageNum& operator+=(int64_t pages) noexcept {
        PageNum result;
        if (!try_offset(pages, result)) {
            __builtin_trap();
        }
        *this = result;
        return *this;
    }

    private:
    RawType val_;

};

using PhysPageNum = PageNum<PhysicalTag>;
using VirtPageNum = PageNum<VirtualTag>;

#pragma once

#include <climits>
#include <cstddef>

namespace aoo {

template<typename T, size_t TagBits=16>
class tagged_integer {
public:
    using type = T;

    static_assert(TagBits < sizeof(type) * CHAR_BIT, "too many tag bits");

    static constexpr T tag_bits = TagBits;
    static constexpr T value_bits = sizeof(type) * CHAR_BIT - tag_bits;
    static constexpr T tag_mask = ((type)1 << tag_bits) - 1;
    static constexpr T value_mask = ((type)1 << value_bits) - 1;
    static constexpr T max_value = value_mask;

    tagged_integer() = default;

    // IMPORTANT: do not assert that 'value' is in the range of [0, max_value(
    // because it may contain garbage!! See comment in free_list::pop().
    tagged_integer(type value, type tag)
        : value_(((tag & tag_mask) << value_bits) | (value & value_mask)) {}

    void set_value(type value) {
        // NB: do not check value! See comment above.
        auto tag_part = value_ & ~value_mask;
        value_ = tag_part | (value & value_mask);
    }

    type get_value() const {
        return value_ & value_mask;
    }

    void set_tag(type tag) {
        auto value = value_ & value_mask;
        value_ = ((tag & tag_mask) << value_bits) | value;
    }

    type get_tag() const {
        return value_ >> value_bits;
    }

    T raw_value() const {
        return value_;
    }

    bool operator== (const tagged_integer& other) const {
        return value_ == other.value_;
    }

    bool operator!= (const tagged_integer& other) const {
        return value_ != other.value_;
    }

private:
    T value_ = 0;
};

} // aoo

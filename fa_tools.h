#ifndef FA_TOOLS_H
#define FA_TOOLS_H

#include <array>
#include <iterator>

namespace fa
{

#ifndef FA_BUFFER_SIZE
#define SAMPLES 50000
#else
#define SAMPLES FA_BUFFER_SIZE
#endif

template <typename T>
class buffer
{
public:
    struct buffer_iterator
    {
        // Iterator tags
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t;
        using value_type        = T;
        using pointer           = value_type*;
        using reference         = value_type&;

        buffer_iterator(pointer ptr) : m_ptr(ptr) {}

        reference operator*() const { return *m_ptr; }
        pointer   operator->()      { return  m_ptr; }

        // Prefix increment
        buffer_iterator& operator++()
        {
            m_ptr++;
            return *this;
        }

        // Postfix increment
        buffer_iterator operator++(int)
        {
            buffer_iterator temp = *this;
            ++(*this);
            return temp;
        }

        friend bool operator==(const buffer_iterator& a, const buffer_iterator& b)
        {
            return a.m_ptr == b.m_ptr;
        }

        friend bool operator!=(const buffer_iterator& a, const buffer_iterator& b)
        {
            return a.m_ptr != b.m_ptr;
        }

    private:
        pointer m_ptr;
    };

    buffer_iterator begin() { return buffer_iterator(&_data[0]); }
    buffer_iterator end()   { return buffer_iterator(&_data[SAMPLES]); }

    explicit buffer()
    {
        _data = {0};
        head = 0;
        count = 0;
    }

    T operator[](size_t i) const
    {
        return _data[i];
    }

    inline bool full() const
    {
        return count == SAMPLES;
    }

    inline bool empty() const
    {
        return count == 0;
    }

    inline size_t size() const
    {
        return count;
    }

    void push_back(T value)
    {
        _data[head] = value;
        _data[head + SAMPLES] = value;
        if (count != SAMPLES)
            count++;
        head = (head + 1) % SAMPLES;
    }

    T* data() const
    {
        return &_data[head];
    }

private:
    std::array<T, SAMPLES * 2> _data;
    size_t head;
    size_t count;
};

}

#endif // FA_TOOLS_H

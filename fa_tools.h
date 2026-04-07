#ifndef FA_TOOLS_H
#define FA_TOOLS_H

#include <vector>
#include <iterator>
#include <iostream>

namespace fa
{

template <typename T, size_t N>
class buffer
{
public:
    struct buffer_iterator
    {
        // Iterator tags
        using iterator_category = std::random_access_iterator_tag;
        using difference_type   = std::ptrdiff_t;
        using value_type        = T;
        using pointer           = value_type*;
        using reference         = value_type&;

        buffer_iterator(pointer ptr) : m_ptr(ptr) {}

        reference operator*()  const { return *m_ptr; }
        pointer   operator->() const { return  m_ptr; }

        reference operator[] (difference_type n) const { return *(m_ptr + n); }

        difference_type operator-(buffer_iterator& a) const { return m_ptr - a.m_ptr; }
        difference_type operator+(buffer_iterator& a) const { return m_ptr + a.m_ptr; }

        buffer_iterator operator-(difference_type n)  const { return {m_ptr - n}; }
        buffer_iterator operator+(difference_type n)  const { return {m_ptr + n}; }

        buffer_iterator& operator-=(difference_type n) { m_ptr -= n; return *this; }
        buffer_iterator& operator+=(difference_type n) { m_ptr += n; return *this; }

        // Prefix increment/decrement
        buffer_iterator& operator++() { m_ptr++; return *this; }
        buffer_iterator& operator--() { m_ptr--; return *this; }

        // Postfix increment/decrement
        buffer_iterator operator++(int) { buffer_iterator temp = *this; ++(*this); return temp; }
        buffer_iterator operator--(int) { buffer_iterator temp = *this; --(*this); return temp; }

        bool operator == (const buffer_iterator& a) const { return m_ptr == a.m_ptr; }
        bool operator != (const buffer_iterator& a) const { return m_ptr != a.m_ptr; }
        bool operator <  (const buffer_iterator& a) const { return m_ptr <  a.m_ptr; }
        bool operator >  (const buffer_iterator& a) const { return m_ptr >  a.m_ptr; }
        bool operator <= (const buffer_iterator& a) const { return m_ptr <= a.m_ptr; }
        bool operator >= (const buffer_iterator& a) const { return m_ptr >= a.m_ptr; }

    private:
        pointer m_ptr;
    };

    buffer_iterator begin() { return buffer_iterator(_data.data() + head); }
    buffer_iterator end() { return buffer_iterator(_data.data() + head + count); }

    explicit buffer() : head{0}, tail{0}, count{0}
    {
        _data.resize(N * 2, 0);
    }

    T& operator[](size_t i) { return _data[i]; }

    inline T& front() const { return _data[head]; }
    inline T& back()  const { return _data[tail]; }

    inline bool full() const { return count == N; }
    inline bool empty() const { return count == 0; }
    inline size_t size() const { return count; }
    inline size_t capacity() const { return N; }

    void push_back(T value)
    {
        _data[tail] = value;
        _data[tail + N] = value;
        tail = (tail + 1) % N;
        if (count != N)
            count++;
        else
            head = (head + 1) % N;
    }

    const T* data() const
    {
        return &_data[head];
    }

private:
    std::vector<T> _data;
    size_t head;
    size_t tail;
    size_t count;
};

}

#endif // FA_TOOLS_H

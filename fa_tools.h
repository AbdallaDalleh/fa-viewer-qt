#ifndef FA_TOOLS_H
#define FA_TOOLS_H

#include <array>

#ifndef FA_BUFFER_SIZE
#define SAMPLES 50000
#else
#define SAMPLES FA_BUFFER_SIZE
#endif

namespace fa
{

template <typename T>
class buffer
{
public:
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

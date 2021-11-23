#ifndef CIRCULAR_HPP
#define CIRCULAR_HPP

#include <iterator>

template<template<typename> class Container, typename T>
class CircularBuffer
{
public:
    CircularBuffer(Container<T>& container) :
        m_begin(std::begin(container)),
        m_end(std::end(container)),
        m_current(std::begin(container)) {}

    void put(const T& value) noexcept {
        *m_current = value;
        if (++m_current == m_end)
            m_current = m_begin;
    }

    std::size_t size() const noexcept {
        return std::distance(m_begin, m_end);
    }

private:
    Container<T>::iterator m_begin;
    Container<T>::iterator m_end;
    Container<T>::iterator m_current;
};

#endif // CIRCULAR_HPP


/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-24 18:52:40
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-10-25 20:03:35
 * @version:
 */
#ifndef __VULKAN_RENDERER_ARRAY_H__
#define __VULKAN_RENDERER_ARRAY_H__

#include "foundation_layer/memory.hpp"
#include "foundation_layer/assert.hpp"

namespace Genesis
{
    template <typename T>
    class Array
    {
    public:
        Array();
        ~Array();

        void init(Allocator *allocator, uint32_t capacity, uint32_t size = 0);
        void destroy();

        void push(const T &element);
        void pop();

        void delete_swap(uint32_t index);

        T &operator[](uint32_t index);
        const T &operator[](uint32_t index) const;
        uint32_t size() const;
        uint32_t capacity() const;
        uint32_t sizeBytes() const;
        uint32_t capacityBytes() const;
        void clear();
        T &front();
        T &back();
        const T &front() const;
        const T &back() const;

        void setSize(uint32_t size);
        void setCapacity(uint32_t capacity);
        void grow(uint32_t capacity);

    private:
        T *data_;
        uint32_t size_;
        uint32_t capacity_;
        Allocator *allocator_;

        T &get(uint32_t index);
        const T &get(uint32_t index) const;
    };

    template <typename T>
    class ArrayView
    {
    public:
        ArrayView(T *data, uint32_t size);
        void set(T *data, uint32_t size);

        T &operator[](uint32_t index);
        const T &operator[](uint32_t index) const;

    private:
        T *data_;
        uint32_t size_;
    };

    // Implementation

    template <typename T>
    inline Array<T>::Array()
    {
        // GENESIS_ASSERT(true);
    }

    template <typename T>
    inline Array<T>::~Array()
    {
        // GENESIS_ASSERT(data_ == nullptr);
    }

    template <typename T>
    inline void Array<T>::init(Allocator *allocator, uint32_t capacity, uint32_t size)
    {
        allocator_ = allocator;
        capacity_ = 0;
        size_ = size;
        data_ = nullptr;
        if (capacity > 0)
        {
            grow(capacity);
        }
    }

    template <typename T>
    inline void Array<T>::destroy()
    {
        if (capacity_ > 0)
        {
            allocator_->deallocate(data_);
        }
        data_ = nullptr;
        capacity_ = 0;
        size_ = 0;
    }

    template <typename T>
    inline void Array<T>::push(const T &element)
    {
        if (size_ >= capacity_)
        {
            grow(capacity_ + 1);
        }
        data_[size_] = element;
    }

    template <typename T>
    inline void Array<T>::pop()
    {
        GENESISASSERT(size_);
        --size_;
    }

    template <typename T>
    inline void Array<T>::delete_swap(uint32_t index)
    {
        GENESISASSERT(size_ > 0 && index < size_);
        data_[index] = data_[--size_];
    }

    template <typename T>
    inline const T &Array<T>::get(uint32_t index) const
    {
        return data_[index];
    }

    template <typename T>
    inline T &Array<T>::get(uint32_t index)
    {
        return data_[index];
    }

    template <typename T>
    inline T &Array<T>::operator[](uint32_t index)
    {
        GENESISASSERT(index < size_);
        return get(index);
    }

    template <typename T>
    inline const T &Array<T>::operator[](uint32_t index) const
    {
        GENESISASSERT(index < size_);
        return get(index);
    }

    template <typename T>
    inline uint32_t Array<T>::size() const { return size_; }

    template <typename T>
    inline uint32_t Array<T>::capacity() const { return capacity_; }
    template <typename T>
    inline uint32_t Array<T>::sizeBytes() const { return size_ * sizeof(T); }

    template <typename T>
    inline uint32_t Array<T>::capacityBytes() const { return capacity_ * sizeof(T); }

    template <typename T>
    inline void Array<T>::clear() { size_ = 0; }

    template <typename T>
    inline T &Array<T>::front()
    {
        GENESISASSERT(size_);
        return get(0);
    }

    template <typename T>
    inline T &Array<T>::back()
    {
        GENESISASSERT(size_);
        return get(size_ - 1);
    }

    template <typename T>
    inline const T &Array<T>::front() const
    {
        GENESISASSERT(size_);
        return get(0);
    }

    template <typename T>
    inline const T &Array<T>::back() const
    {
        GENESISASSERT(size_);
        return get(size_ - 1);
    }

    template <typename T>
    inline void Array<T>::grow(uint32_t capacity)
    {
        if (capacity < capacity_ * 2)
        {
            capacity = capacity_ * 2; // Double
        }
        else if (capacity < 4)
        {
            capacity = 4; // 4 bytes minimum
        }

        T *data = (T *)allocator_->allocate(capacity * sizeof(T), alignof(T));
        if (capacity_ > 0)
        {
            memory_copy(data, data_, capacity_ * sizeof(T));
            allocator_->deallocate(data_);
        }

        data_ = data;
        capacity_ = capacity;
    }

    template <typename T>
    inline ArrayView<T>::ArrayView(T *data, uint32_t size) : data_(data), size_(size)
    {
    }

    template <typename T>
    inline void ArrayView<T>::set(T *data, uint32_t size)
    {
        data_ = data;
        size_ = size;
    }

    template <typename T>
    inline T &ArrayView<T>::operator[](uint32_t index)
    {
        GENESISASSERT(index < size_);
        return data_[index];
    }

    template <typename T>
    inline const T &ArrayView<T>::operator[](uint32_t index) const
    {
        GENESISASSERT(index < size_);
        return data_[index];
    }
}

#endif /* __VULKAN_RENDERER_ARRAY_H__ */
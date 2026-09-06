/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-31 19:36:28
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-11-08 19:31:11
 * @version:
 */
#ifndef __VULKAN_RENDERER_SERIALIZATION_BLOB_H__
#define __VULKAN_RENDERER_SERIALIZATION_BLOB_H__

#include "foundation_layer/platform.hpp"
#include "foundation_layer/memory.hpp"
#include "foundation_layer/assert.hpp"
#include "foundation_layer/array.hpp"

//[ref]:https://jorenjoestar.github.io/post/serialization_for_games/

namespace Genesis
{
    class Allocator;

    struct BlobHeader
    {
        uint32_t version;
        uint32_t mappable;
    };

    struct Blob
    {
        BlobHeader header;
    };

    // relative structures /////////////////////////////////////
    template <typename T>
    class RelativePointer
    {
    public:
        void set(char *data);
        void setNull();
        T *get() const;

        T *operator->();
        T &operator*();

        bool isNull() const;
        bool isEqual(const RelativePointer &other) const;

    private:
        int32_t offset_;
    };

    template <typename T>
    class RelativeArray
    {
    public:
        void set(char *data, uint32_t size);
        void clear();
        T *get();
        const T *get() const;

        T &operator[](uint32_t index);
        const T &operator[](uint32_t index) const;

    private:
        uint32_t size_;
        RelativePointer<T> data_;
    };

    // Blob serializer /////////////////////////////////////////
    class BlobSerializer
    {
    public:
        template <typename T>
        T *writePrepare(Allocator *allocator, uint32_t version, size_t size);
        template <typename T>
        void writeAndSerialize(Allocator *allocator, uint32_t version, size_t size, T *data);

        template <typename T>
        T *read(Allocator *allocator, uint32_t version, size_t size, char *blob_memory, bool force_serialization = false);

        void shutdown();

        template <typename T>
        T *allocateS();
        template <typename T>
        void serialize(T *data);
        template <typename T>
        void serialize(Array<T> *data);

    private:
        void doWrite(Allocator *allocator, uint32_t version, size_t size);
        char *allocateS(size_t size); // Static allocation from the blob allocated memory

    private:
        Allocator *allocator_;
        char *blob_memory_{nullptr};
        char *data_memory_{nullptr};

        uint32_t total_size_ = 0;
        uint32_t serialized_offset_ = 0;
        uint32_t allocated_offset_ = 0;

        bool is_reading_{false};
        bool is_mappable_{false};
        bool is_allocated_{false};

        uint32_t serialized_version_ = 0xFFFFFFFF;
        uint32_t cur_version_ = 0xFFFFFFFF;
    };

    // relative structures //////////////pointer///////////////////////
    template <typename T>
    inline void RelativePointer<T>::set(char *data)
    {
        offset_ = data ? (int32_t)(data - reinterpret_cast<char *>(this)) : 0;
    }

    template <typename T>
    inline void RelativePointer<T>::setNull()
    {
        offset_ = 0;
    }

    template <typename T>
    inline T *RelativePointer<T>::get() const
    {
        auto *address = reinterpret_cast<char *>(&offset_) + offset_; // 相对于自身地址的偏移
        return offset_ != 0 ? reinterpret_cast<T *>(address) : nullptr;
    }

    template <typename T>
    inline T *RelativePointer<T>::operator->()
    {
        return get();
    }

    template <typename T>
    inline T &RelativePointer<T>::operator*()
    {
        return *get();
    }

    template <typename T>
    inline bool RelativePointer<T>::isNull() const
    {
        return offset_ == 0;
    }

    template <typename T>
    inline bool RelativePointer<T>::isEqual(const RelativePointer<T> &other) const
    {
        return get() == other.get();
    }

    // relative structures //////////////array///////////////////////
    template <typename T>
    inline T *RelativeArray<T>::get()
    {
        return data_.get();
    }

    template <typename T>
    inline const T *RelativeArray<T>::get() const
    {
        return data_.get();
    }

    template <typename T>
    inline T &RelativeArray<T>::operator[](uint32_t index)
    {
        GENESISASSERT(index < size_);
        return data_.get()[index];
    }

    template <typename T>
    inline const T &RelativeArray<T>::operator[](uint32_t index) const
    {
        GENESISASSERT(index < size_);
        return data_.get()[index];
    }

    template <typename T>
    inline void RelativeArray<T>::set(char *data, uint32_t size)
    {
        data_.set(data);
        size_ = size;
    }

    template <typename T>
    inline void RelativeArray<T>::clear()
    {
        data_.setNull();
        size_ = 0;
    }

    // blob serializer /////////////////////////////////////////
    template <typename T>
    T *BlobSerializer::writePrepare(Allocator *allocator, uint32_t version, size_t size)
    {
        // allocate blob memory(including blob header)
        doWrite(allocator, version, size);

        // allocate root
        allocateS(sizeof(T) - sizeof(BlobHeader));

        // manage blob serialization
        data_memory_ = nullptr;

        return (T *)blob_memory_;
    }

    template <typename T>
    void BlobSerializer::writeAndSerialize(Allocator *allocator, uint32_t version, size_t size, T *data)
    {
        GENESISASSERT(data);

        // allocate blob memory(including blob header)
        doWrite(allocator, version, size);

        // allocate root
        allocateS(sizeof(T) - sizeof(BlobHeader));

        // save root data memory for offset
        data_memory_ = (char *)data;

        serialize<T>(data);
    }

    template <typename T>
    void BlobSerializer::serialize(T *data)
    {
        if (is_reading_)
        {
            memcpy(data, &blob_memory_[serialized_offset_], sizeof(T));
        }
        else
        {
            memcpy(&blob_memory_[serialized_offset_], data, sizeof(T));
        }

        serialized_offset_ += sizeof(T);
    }

    template <typename T>
    inline T *BlobSerializer::allocateS()
    {
        return (T *)allocateS(sizeof(T));
    }

    template <typename T>
    inline T *BlobSerializer::read(Allocator *allocator, uint32_t version, size_t size, char *blob_memory, bool force_serialization)
    {
        allocator_ = allocator;
        blob_memory_ = blob_memory;

        total_size_ = (uint32_t)size;
        serialized_offset_ = allocated_offset_ = 0;
        data_memory_ = nullptr;

        serialized_version_ = version;
        is_reading_ = true;
        is_allocated_ = 0;

        // read header
        BlobHeader *header = reinterpret_cast<BlobHeader *>(blob_memory_);
        cur_version_ = header->version;
        is_mappable_ = header->mappable;

        if (serialized_version_ == cur_version_ && !force_serialization)
        {
            // no need to serialize
            return reinterpret_cast<T *>(blob_memory_);
        }

        is_allocated_ = 1;
        serialized_version_ = cur_version_;

        // allocate root
        data_memory_ = reinterpret_cast<char *>(GenesisAlloc(size, allocator_));

        T *root = reinterpret_cast<T *>(data_memory_);
        serialized_offset_ += sizeof(BlobHeader);

        allocateS(sizeof(T));
        // read from blob to data
        serialize<T>(root);

        return root;
    }

    template <typename T>
    inline void BlobSerializer::serialize(Array<T> *data)
    {
        if (is_reading_)
        {
            // blob -> run time data
        }
        else
        {
            // run time data -> blob
            serialize<uint32_t>(&data->size());
            // padding for deserialize
            uint64_t padding = 0;
            serialize<uint64_t>(&padding);
            serialize<uint64_t>(&padding);

            int32_t data_offset = allocated_offset_ - serialized_offset_;
            // set higher bit for flag
            uint32_t packed_data_offset = (uint32_t)data_offset | (1 << 32);
            serialize<uint32_t>(&packed_data_offset);

            auto cache_serialized_offset = serialized_offset_;
            // move next
            serialized_offset_ = allocated_offset_;
            // allocate next
            allocateS(sizeof(T) * data->size());

            // serialize
            for (uint32_t i = 0; i < data->size(); i++)
            {
                auto *source_data = &data[i];
                serialize<T>(source_data);
                source_data = source_data;
            }
            // restore serialized offset
            serialized_offset_ = cache_serialized_offset;
        }
    }

}

#endif /* __VULKAN_RENDERER_SERIALIZATION_BLOB_H__ */
#pragma once

#include "next/runtime/component_type.h"
#include "next/runtime/entity.h"

#include <algorithm>
#include <cstddef>
#include <new>
#include <vector>

namespace Next {

// A contiguous, type-erased column holding every instance of one component type within an
// archetype. Storage is managed manually (aligned allocation, explicit construct/destroy)
// so that non-trivially-movable / polymorphic components are relocated correctly via the
// registered ComponentOps rather than memcpy.
class ComponentColumn {
public:
    ComponentColumn() = default;
    ComponentColumn(ComponentTypeID type, const ComponentOps& ops) : type_(type), ops_(ops) {}

    ComponentColumn(const ComponentColumn&) = delete;
    ComponentColumn& operator=(const ComponentColumn&) = delete;

    ComponentColumn(ComponentColumn&& other) noexcept { MoveFrom(other); }
    ComponentColumn& operator=(ComponentColumn&& other) noexcept {
        if (this != &other) {
            Destroy();
            MoveFrom(other);
        }
        return *this;
    }
    ~ComponentColumn() { Destroy(); }

    ComponentTypeID Type() const { return type_; }
    size_t Size() const { return count_; }

    void* At(size_t index) { return data_ + (index * ops_.size); }
    const void* At(size_t index) const { return data_ + (index * ops_.size); }

    // Append a default-constructed element.
    void PushDefault() {
        EnsureCapacity(count_ + 1);
        ops_.defaultConstruct(At(count_));
        ++count_;
    }

    // Append an element move-constructed from src. src is left in a moved-from (but still
    // alive) state; the caller is responsible for destroying it.
    void PushMoveFrom(void* src) {
        EnsureCapacity(count_ + 1);
        ops_.moveConstruct(At(count_), src);
        ++count_;
    }

    // Remove element `index` by moving the last element into its slot (swap-pop).
    void RemoveSwapPop(size_t index) {
        const size_t last = count_ - 1;
        if (index != last) {
            ops_.destroy(At(index));
            ops_.moveConstruct(At(index), At(last));
        }
        ops_.destroy(At(last));
        --count_;
    }

private:
    void EnsureCapacity(size_t required) {
        if (required <= capacity_) {
            return;
        }
        const size_t newCapacity = std::max(capacity_ == 0 ? size_t{4} : capacity_ * 2, required);
        std::byte* newData = Allocate(newCapacity);
        for (size_t i = 0; i < count_; ++i) {
            ops_.moveConstruct(newData + (i * ops_.size), At(i));
            ops_.destroy(At(i));
        }
        FreeRaw();
        data_ = newData;
        capacity_ = newCapacity;
    }

    std::byte* Allocate(size_t capacity) const {
        if (capacity == 0) {
            return nullptr;
        }
        return static_cast<std::byte*>(::operator new(capacity * ops_.size, std::align_val_t(ops_.align)));
    }

    void FreeRaw() {
        if (data_ != nullptr) {
            ::operator delete(data_, std::align_val_t(ops_.align));
            data_ = nullptr;
        }
    }

    void Destroy() {
        for (size_t i = 0; i < count_; ++i) {
            ops_.destroy(At(i));
        }
        count_ = 0;
        FreeRaw();
        capacity_ = 0;
    }

    void MoveFrom(ComponentColumn& other) {
        type_ = other.type_;
        ops_ = other.ops_;
        data_ = other.data_;
        count_ = other.count_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.count_ = 0;
        other.capacity_ = 0;
    }

    ComponentTypeID type_ = 0;
    ComponentOps ops_{};
    std::byte* data_ = nullptr;
    size_t count_ = 0;
    size_t capacity_ = 0;
};

// An archetype groups every entity that has exactly one set of component types (its
// signature). Each component type is stored in its own contiguous column, and entity ids
// occupy a parallel row vector. This is the data-oriented core: a query iterates the
// columns of matching archetypes linearly.
class Archetype {
public:
    explicit Archetype(std::vector<ComponentTypeID> signature) : signature_(std::move(signature)) {
        columns_.reserve(signature_.size());
        for (const ComponentTypeID type : signature_) {
            columns_.emplace_back(type, ComponentRegistry::Get().GetOps(type));
        }
    }

    const std::vector<ComponentTypeID>& Signature() const { return signature_; }
    size_t Size() const { return entities_.size(); }
    Entity EntityAt(size_t row) const { return entities_[row]; }

    bool Has(ComponentTypeID type) const { return std::binary_search(signature_.begin(), signature_.end(), type); }

    ComponentColumn* ColumnFor(ComponentTypeID type) {
        for (ComponentColumn& column : columns_) {
            if (column.Type() == type) {
                return &column;
            }
        }
        return nullptr;
    }

    // Typed base pointer of a component column, for the query fast path. Returns nullptr if
    // this archetype does not carry T (callers only iterate rows when the column exists).
    template<typename T>
    T* ColumnData() {
        ComponentColumn* column = ColumnFor(ComponentType<T>::GetID());
        return column != nullptr ? static_cast<T*>(column->At(0)) : nullptr;
    }

    // Append an entity, default-constructing every component column. Returns its row.
    size_t AddEntityDefault(Entity entity) {
        for (ComponentColumn& column : columns_) {
            column.PushDefault();
        }
        entities_.push_back(entity);
        return entities_.size() - 1;
    }

    // Append a row migrated from another archetype: for each of this archetype's columns,
    // move-construct from `from` if it carries that component, otherwise default-construct
    // (the newly-added component). The source row in `from` is left moved-from for the
    // caller to remove. Returns the new row.
    size_t AppendMigratedRow(Entity entity, Archetype& from, size_t fromRow) {
        for (ComponentColumn& column : columns_) {
            ComponentColumn* source = from.ColumnFor(column.Type());
            if (source != nullptr) {
                column.PushMoveFrom(source->At(fromRow));
            } else {
                column.PushDefault();
            }
        }
        entities_.push_back(entity);
        return entities_.size() - 1;
    }

    // Remove a row via swap-pop across all columns and the entity vector. Returns the entity
    // that was relocated into `row` (so the caller can fix its stored row), or an invalid
    // entity if `row` was the last row.
    Entity RemoveRow(size_t row) {
        const size_t last = entities_.size() - 1;
        for (ComponentColumn& column : columns_) {
            column.RemoveSwapPop(row);
        }
        Entity relocated = Entity::Invalid();
        if (row != last) {
            entities_[row] = entities_[last];
            relocated = entities_[row];
        }
        entities_.pop_back();
        return relocated;
    }

private:
    std::vector<ComponentTypeID> signature_;  // sorted ascending
    std::vector<ComponentColumn> columns_;    // parallel to signature_
    std::vector<Entity> entities_;            // one row per entity
};

// --- Signature helpers (signatures are sorted ascending vectors of ComponentTypeID) ----

inline std::vector<ComponentTypeID> SignatureWith(const std::vector<ComponentTypeID>& base, ComponentTypeID type) {
    std::vector<ComponentTypeID> result = base;
    if (!std::binary_search(result.begin(), result.end(), type)) {
        result.insert(std::upper_bound(result.begin(), result.end(), type), type);
    }
    return result;
}

inline std::vector<ComponentTypeID> SignatureWithout(const std::vector<ComponentTypeID>& base, ComponentTypeID type) {
    std::vector<ComponentTypeID> result = base;
    result.erase(std::remove(result.begin(), result.end(), type), result.end());
    return result;
}

// True if `superset` (sorted) contains every id in `query` (sorted).
inline bool SignatureContainsAll(const std::vector<ComponentTypeID>& superset,
                                 const std::vector<ComponentTypeID>& query) {
    return std::includes(superset.begin(), superset.end(), query.begin(), query.end());
}

template<typename... Components>
inline std::vector<ComponentTypeID> MakeSortedSignature() {
    std::vector<ComponentTypeID> signature{ComponentType<Components>::GetID()...};
    std::sort(signature.begin(), signature.end());
    return signature;
}

}  // namespace Next

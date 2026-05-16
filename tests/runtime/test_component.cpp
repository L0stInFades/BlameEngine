#include "next/runtime/component.h"
#include "next/runtime/component_type.h"
#include "next/runtime/entity.h"
#include <gtest/gtest.h>
#include <set>
#include <utility>

namespace Next {
namespace testing {

namespace {
template<int Index>
struct TempComponent : public IComponent {
    int value = Index;
};

template<int... Indices>
void CollectTempComponentIds(std::vector<ComponentTypeID>& ids, std::integer_sequence<int, Indices...>) {
    (ids.push_back(ComponentType<TempComponent<Indices>>::GetID()), ...);
}
} // namespace

using ::testing::Test;

class ComponentTest : public Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test name component default constructor
TEST_F(ComponentTest, NameComponentDefault) {
    NameComponent name;

    EXPECT_STREQ(name.name, "");
    EXPECT_EQ(strlen(name.name), 0u);
}

// Test name component with string
TEST_F(ComponentTest, NameComponentWithString) {
    NameComponent name("TestEntity");

    EXPECT_STREQ(name.name, "TestEntity");
}

// Test name component with long string
TEST_F(ComponentTest, NameComponentWithLongString) {
    std::string longName(100, 'A');

    NameComponent name(longName.c_str());

    // Should be truncated to MAX_NAME_LENGTH - 1
    EXPECT_EQ(strlen(name.name), NameComponent::MAX_NAME_LENGTH - 1);
}

// Test name component with empty string
TEST_F(ComponentTest, NameComponentWithEmptyString) {
    NameComponent name("");

    EXPECT_STREQ(name.name, "");
}

TEST_F(ComponentTest, NameComponentWithNullString) {
    NameComponent name(nullptr);

    EXPECT_STREQ(name.name, "");
    EXPECT_EQ(strlen(name.name), 0u);
}

// Test name component with special characters
TEST_F(ComponentTest, NameComponentWithSpecialChars) {
    NameComponent name("Entity_123!@#$%");

    EXPECT_STREQ(name.name, "Entity_123!@#$%");
}

// Test name component with unicode
TEST_F(ComponentTest, NameComponentWithUnicode) {
    NameComponent name("实体测试");

    EXPECT_STREQ(name.name, "实体测试");
}

// Test hierarchy component default constructor
TEST_F(ComponentTest, HierarchyComponentDefault) {
    HierarchyComponent hierarchy;

    EXPECT_FALSE(hierarchy.parent.IsValid());
    EXPECT_EQ(hierarchy.childCount, 0u);
}

// Test hierarchy component add child
TEST_F(ComponentTest, HierarchyComponentAddChild) {
    HierarchyComponent hierarchy;

    Entity child1(1, 1);
    Entity child2(2, 1);
    Entity child3(3, 1);

    hierarchy.AddChild(child1);
    hierarchy.AddChild(child2);
    hierarchy.AddChild(child3);

    EXPECT_EQ(hierarchy.childCount, 3u);
    EXPECT_EQ(hierarchy.children[0], child1);
    EXPECT_EQ(hierarchy.children[1], child2);
    EXPECT_EQ(hierarchy.children[2], child3);
}

// Test hierarchy component add child limit
TEST_F(ComponentTest, HierarchyComponentAddChildLimit) {
    HierarchyComponent hierarchy;

    // Add more than MAX_CHILDREN
    for (uint32_t i = 0; i < HierarchyComponent::MAX_CHILDREN + 10; ++i) {
        Entity child(i, 1);
        hierarchy.AddChild(child);
    }

    // Should be capped at MAX_CHILDREN
    EXPECT_EQ(hierarchy.childCount, HierarchyComponent::MAX_CHILDREN);
}

// Test hierarchy component remove child
TEST_F(ComponentTest, HierarchyComponentRemoveChild) {
    HierarchyComponent hierarchy;

    Entity child1(1, 1);
    Entity child2(2, 1);
    Entity child3(3, 1);

    hierarchy.AddChild(child1);
    hierarchy.AddChild(child2);
    hierarchy.AddChild(child3);

    EXPECT_EQ(hierarchy.childCount, 3u);

    hierarchy.RemoveChild(child2);

    EXPECT_EQ(hierarchy.childCount, 2u);
    EXPECT_EQ(hierarchy.children[0], child1);
    EXPECT_EQ(hierarchy.children[1], child3);
}

// Test hierarchy component remove non-existent child
TEST_F(ComponentTest, HierarchyComponentRemoveNonExistentChild) {
    HierarchyComponent hierarchy;

    Entity child1(1, 1);
    Entity child2(2, 1);
    Entity notChild(99, 1);

    hierarchy.AddChild(child1);
    hierarchy.AddChild(child2);

    size_t beforeCount = hierarchy.childCount;
    hierarchy.RemoveChild(notChild);

    EXPECT_EQ(hierarchy.childCount, beforeCount);
}

TEST_F(ComponentTest, HierarchyComponentNormalizesInvalidChildCount) {
    HierarchyComponent hierarchy;

    Entity child1(1, 1);
    Entity child2(2, 1);
    Entity child3(3, 1);
    hierarchy.children[0] = child1;
    hierarchy.children[1] = child2;
    hierarchy.childCount = HierarchyComponent::MAX_CHILDREN + 10;

    hierarchy.RemoveChild(child1);

    EXPECT_EQ(hierarchy.childCount, HierarchyComponent::MAX_CHILDREN - 1);
    EXPECT_EQ(hierarchy.children[0], child2);
    EXPECT_FALSE(hierarchy.children[hierarchy.childCount].IsValid());

    HierarchyComponent fullHierarchy;
    fullHierarchy.childCount = HierarchyComponent::MAX_CHILDREN + 10;
    fullHierarchy.AddChild(child3);

    EXPECT_EQ(fullHierarchy.childCount, HierarchyComponent::MAX_CHILDREN);
}

// Test hierarchy component set parent
TEST_F(ComponentTest, HierarchyComponentSetParent) {
    HierarchyComponent hierarchy;

    Entity parent(42, 1);
    hierarchy.parent = parent;

    EXPECT_EQ(hierarchy.parent, parent);
}

// Test mesh renderer component default constructor
TEST_F(ComponentTest, MeshRendererComponentDefault) {
    MeshRendererComponent renderer;

    EXPECT_EQ(renderer.submeshIndex, 0u);
    EXPECT_TRUE(renderer.castShadows);
    EXPECT_TRUE(renderer.receiveShadows);
}

// Test transform component default values
TEST_F(ComponentTest, TransformComponentDefaults) {
    TransformComponent transform;

    EXPECT_FLOAT_EQ(transform.position[0], 0.0f);
    EXPECT_FLOAT_EQ(transform.position[1], 0.0f);
    EXPECT_FLOAT_EQ(transform.position[2], 0.0f);

    EXPECT_FLOAT_EQ(transform.rotation[0], 0.0f);
    EXPECT_FLOAT_EQ(transform.rotation[1], 0.0f);
    EXPECT_FLOAT_EQ(transform.rotation[2], 0.0f);
    EXPECT_FLOAT_EQ(transform.rotation[3], 1.0f);

    EXPECT_FLOAT_EQ(transform.scale[0], 1.0f);
    EXPECT_FLOAT_EQ(transform.scale[1], 1.0f);
    EXPECT_FLOAT_EQ(transform.scale[2], 1.0f);

    EXPECT_FALSE(transform.parent.IsValid());
}

// Test component type ID generation
TEST_F(ComponentTest, ComponentTypeIDGeneration) {
    ComponentTypeID id1 = ComponentType<TransformComponent>::GetID();
    ComponentTypeID id2 = ComponentType<NameComponent>::GetID();
    ComponentTypeID id3 = ComponentType<HierarchyComponent>::GetID();
    ComponentTypeID id4 = ComponentType<TransformComponent>::GetID();  // Same as id1

    // Each type should get unique ID
    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_NE(id1, id3);

    // Same type should return same ID
    EXPECT_EQ(id1, id4);
}

// Test component type ID monotonic
TEST_F(ComponentTest, ComponentTypeIDMonotonic) {
    ComponentTypeID id1 = ComponentType<TransformComponent>::GetID();
    ComponentTypeID id2 = ComponentType<NameComponent>::GetID();
    ComponentTypeID id3 = ComponentType<HierarchyComponent>::GetID();

    // IDs should be monotonically increasing
    EXPECT_LT(id1, id2);
    EXPECT_LT(id2, id3);
}

// Test tag components
TEST_F(ComponentTest, TagComponents) {
    // Tag components are empty structs, but should be usable
    StaticTag staticTag;
    DynamicTag dynamicTag;
    VisibleTag visibleTag;
    InvisibleTag invisibleTag;

    // These are empty structs, so we just verify they can be instantiated
    SUCCEED();
}

// Test custom component
TEST_F(ComponentTest, CustomComponent) {
    struct CustomComponent : public IComponent {
        int value = 42;
        float weight = 1.5f;
    };

    CustomComponent custom;
    EXPECT_EQ(custom.value, 42);
    EXPECT_FLOAT_EQ(custom.weight, 1.5f);

    ComponentTypeID id = ComponentType<CustomComponent>::GetID();
    EXPECT_GT(id, 0u);
}

// Test component type starting ID
TEST_F(ComponentTest, ComponentTypeStartingID) {
    struct FirstComponent : public IComponent {};
    struct SecondComponent : public IComponent {};

    // First registered component should have ID >= 0
    ComponentTypeID id1 = ComponentType<FirstComponent>::GetID();
    ComponentTypeID id2 = ComponentType<SecondComponent>::GetID();

    EXPECT_GE(id1, 0u);
    EXPECT_GT(id2, id1);
}

// Test many component types
TEST_F(ComponentTest, ManyComponentTypes) {
    std::vector<ComponentTypeID> ids;

    // Register many component types
    CollectTempComponentIds(ids, std::make_integer_sequence<int, 100>{});

    // All IDs should be unique
    std::set<ComponentTypeID> uniqueIds(ids.begin(), ids.end());
    EXPECT_EQ(uniqueIds.size(), ids.size());
}

} // namespace testing
} // namespace Next

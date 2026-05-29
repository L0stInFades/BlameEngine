# 开发交接：CP3 → CP4

## 1. 当前状态（CP3 已完成）
- Asset Pipeline 雏形已实现并验证
  - ✅ 资源编译器 `assetc` 可生成测试资源
  - ✅ Asset Runtime 支持同步/异步加载
  - ✅ 包容器可正确加载和解析
  - ✅ 引用计数机制运行正常
  - ✅ Job System 集成工作正常
  - ✅ 所有验收测试通过（2026-01-13）

- 测试资源已生成：
  - `assets/test_cube.mesh` (480 bytes)
  - `assets/test_checker.texture` (184 bytes)
  - `assets/test_pbr.material` (456 bytes)
  - `assets/test_package.npkg` (包含以上3个资源)

## 2. CP4 目标（Runtime World - 完整 ECS）

### 核心目标
实现一个功能完整的 Entity Component System (ECS) 架构，为游戏世界提供高效的数据管理和查询能力。

### 验收标准
- ✅ 完整的 ECS 架构（Entity/Component/System）
- ✅ 高效的组件查询（Archetype 或 Packed Array）
- ✅ Transform 组件系统（层次化场景图）
- ✅ Event Bus 扩展（支持实体事件）
- ✅ 与 Asset Pipeline 集成（MeshRenderer 组件）
- ✅ 10,000+ 实体性能测试

## 3. 建议的首个迭代（1-2 天）

### Phase 1: ECS 核心（优先级：高）

1. **Component 定义系统**
   ```cpp
   // engine/runtime/include/next/runtime/component.h
   class ComponentBase {
       uint32_t typeId_;
       uint32_t entityId_;
   };

   // 常用组件定义
   struct TransformComponent {
       glm::vec3 position;
       glm::quat rotation;
       glm::vec3 scale;
       uint32_t parent;  // 父实体ID
       std::vector<uint32_t> children;
   };

   struct MeshComponent {
       MeshHandle mesh;
       MaterialHandle material;
       uint32_t submeshIndex;
   };

   struct NameComponent {
       char name[64];
   };
   ```

2. **Entity 管理**
   ```cpp
   // engine/runtime/include/next/runtime/entity.h
   class EntityManager {
   public:
       Entity Create();
       void Destroy(Entity entity);
       bool IsValid(Entity entity) const;

       // 组件操作
       template<typename T>
       T& AddComponent(Entity entity);

       template<typename T>
       T& GetComponent(Entity entity);

       template<typename T>
       bool HasComponent(Entity entity) const;

       template<typename T>
       void RemoveComponent(Entity entity);

   private:
       std::vector<Entity> entities_;
       std::unordered_map<ComponentTypeId, std::unique_ptr<ComponentArray>> componentArrays_;
       std::queue<EntityId> freeIds_;
   };
   ```

3. **System 基类**
   ```cpp
   // engine/runtime/include/next/runtime/system.h
   class System {
   public:
       virtual void Initialize() {}
       virtual void Update(float deltaTime) = 0;
       virtual void Shutdown() {}

       virtual void OnEntityCreated(Entity entity) {}
       virtual void OnEntityDestroyed(Entity entity) {}
   };

   // 示例系统
   class TransformSystem : public System {
   public:
       void Update(float deltaTime) override;
       void UpdateHierarchy(Entity entity);
   };
   ```

### Phase 2: 查询系统（优先级：高）

1. **Archetype 查询**（推荐方案）
   ```cpp
   // 高效的组件查询
   class Query {
   public:
       template<typename... Components>
       Query& All();

       template<typename... Components>
       Query& Any();

       Query& None<ComponentType>();

       // 执行查询
       template<typename Func>
       void ForEach(Func&& func);
   };

   // 使用示例
   world.Query()
       .All<TransformComponent, MeshComponent>()
       .ForEach([](Entity entity, TransformComponent& transform, MeshComponent& mesh) {
           // 渲染网格
       });
   ```

### Phase 3: 集成 Asset Pipeline（优先级：中）

1. **创建实体并加载资源**
   ```cpp
   // 在游戏初始化时
   auto& world = World::Instance();
   auto& assetManager = AssetManager::Instance();

   // 加载包
   assetManager.LoadPackage("assets/test_package.npkg");

   // 创建立方体实体
   Entity cube = world.CreateEntity();
   world.AddComponent<NameComponent>(cube, "TestCube");
   world.AddComponent<TransformComponent>(cube, {{0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}});

   // 加载网格和材质
   auto meshHandle = assetManager.LoadAssetSync("TestCube");
   auto matHandle = assetManager.LoadAssetSync("TestPBR");

   world.AddComponent<MeshComponent>(cube, {meshHandle, matHandle, 0});
   ```

2. **资源卸载与生命周期**
   ```cpp
   // 当实体销毁时自动释放资源
   void OnEntityDestroyed(Entity entity) {
       if (world.HasComponent<MeshComponent>(entity)) {
           auto& mesh = world.GetComponent<MeshComponent>(entity);
           assetManager.Release(mesh.mesh);
           assetManager.Release(mesh.material);
       }
   }
   ```

### Phase 4: 性能测试（优先级：高）

1. **压力测试**
   ```cpp
   // 生成 10,000 个实体
   void RunECSStressTest() {
       NEXT_LOG_INFO("Running ECS stress test...");

       auto startTime = GetTimeInSeconds();

       // 创建实体
       for (int i = 0; i < 10000; ++i) {
           Entity entity = world.CreateEntity();
           world.AddComponent<TransformComponent>(entity);
           world.AddComponent<MeshComponent>(entity);
       }

       auto createEndTime = GetTimeInSeconds();
       NEXT_LOG_INFO("Created 10,000 entities in %.2f ms",
                    (createEndTime - startTime) * 1000.0);

       // 查询测试
       int queryCount = 0;
       world.Query().All<TransformComponent, MeshComponent>().ForEach(
           [&](Entity, TransformComponent&, MeshComponent&) {
               queryCount++;
           });

       NEXT_LOG_INFO("Query returned %d entities", queryCount);

       // 更新测试
       auto updateStartTime = GetTimeInSeconds();
       for (int frame = 0; frame < 60; ++frame) {
           world.Update(0.016f);
       }
       auto updateEndTime = GetTimeInSeconds();

       NEXT_LOG_INFO("60 frames update in %.2f ms (avg: %.2f ms/frame)",
                    (updateEndTime - updateStartTime) * 1000.0,
                    (updateEndTime - updateStartTime) * 1000.0 / 60.0);

       // 清理
       for (int i = 0; i < 10000; ++i) {
           // world.DestroyEntity(entities[i]);
       }
   }
   ```

## 4. 先修技术债（优先级：中）

### 4.1 Profiler 线程安全
- 当前 Profiler 在多线程环境下可能有数据竞争
- 建议使用线程本地缓冲或原子操作

### 4.2 日志系统改进
- 支持按组件类型过滤日志
- 添加 Entity ID 到日志上下文

### 4.3 内存分配器
- 考虑为 ECS 实现自定义内存分配器
- 减少碎片和提高缓存局部性

## 5. 辅助原则

### 5.1 性能优先
- 组件数据应该紧凑存储（SoA 或 AoS）
- 查询结果应该缓存或使用迭代器
- 避免在热循环中使用虚函数

### 5.2 易用性
- 提供 RAII 风格的实体创建/销毁
- 支持组件初始化列表
- 提供实体引用类（类似 EntityHandle）

### 5.3 可观测性
- 所有 System 更新应该有 CPU Scope
- 定期报告实体数、组件数、内存使用
- 支持实体/组件的调试视图

## 6. 验收口径（CP4）

- ✅ ECS 架构可创建/销毁实体和组件
- ✅ Transform 系统支持层次化（父子关系）
- ✅ 查询系统可高效遍历实体（< 1ms for 10k entities）
- ✅ Asset Pipeline 可为实体加载资源
- ✅ 性能测试通过（10k 实体，60 FPS）
- ✅ 无内存泄漏（长时间运行测试）

## 7. 示例代码结构

```
engine/runtime/
├── include/next/runtime/
│   ├── entity.h           # Entity 类型定义
│   ├── component.h        # Component 基类和常用组件
│   ├── component_registry.h  # 组件类型注册
│   ├── system.h           # System 基类
│   ├── world.h            # World 管理器
│   └── query.h            # 查询系统
└── src/
    ├── entity.cpp
    ├── component_registry.cpp
    ├── world.cpp
    └── query.cpp

game/song/src/
└── game.cpp
    └── RunECSTest()       # CP4 验收测试
```

## 8. 下一步行动

1. **立即开始**: Phase 1 - ECS 核心实现
2. **并行进行**: Phase 2 - 查询系统设计
3. **集成验证**: Phase 3 - Asset Pipeline 集成
4. **性能测试**: Phase 4 - 压力测试和优化

---

**文档版本**: 1.0
**创建时间**: 2026-01-13
**预计工期**: CP4 - 2-3 天（快速原型）
**下一里程碑**: CP4 完成后 → CP5 渲染基线（DX12 + Render Graph）

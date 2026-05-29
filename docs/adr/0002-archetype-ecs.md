# 0002. Runtime ECS 采用 archetype 数据导向存储

- **Status**: Accepted
- **Date**: 2026-05-29

## Context
原 ECS 用 `unordered_map<Entity, T>` 每类型一张哈希表:同类型组件内存分散、查询是 `O(总实体数)` 全表扫 + 每次访问哈希查找。引擎目标包含海量实体的开放世界,而当初选 ECS 的理由正是数据局部性——旧实现恰恰拿不到这个收益。

## Decision
重写 `engine/runtime` 为 **archetype / 数据导向**存储:相同组件集合的实体共享一个 `Archetype`,每类型一列连续存储(`ComponentColumn`,类型擦除、手工对齐、用 move-construct/destroy 搬迁,绝不 memcpy 多态组件);add/remove 在 archetype 间迁移实体;查询按列线性迭代匹配的 archetype。新增 `CommandBuffer` 延后迭代中的结构变更。**公共 `World` API 保持源码级稳定**。

## Consequences
- 正面:查询 `O(命中数)`、同类型连续内存(cache 友好);127 测试在 ASan/UBSan 下通过;`game.cpp` 与全部测试零改动编译。
- 负面/约束:与任何 archetype ECS 一样,**结构变更会使已取的组件引用失效**(需 re-fetch);存储实现更复杂(手工内存)。签名用排序 `vector<ComponentTypeID>`(非位掩码,无 64 类型上限);跨运行稳定的 typeID(序列化用)未做。

## Alternatives considered
- sparse-set(EnTT 式):引用失效更少,但与所选"完整数据导向"方向不符;archetype 多组件遍历更优。
- 保留 map 实现:被否,拿不到数据局部性。

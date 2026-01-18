# 文档目录

本文档目录包含嵌入式MQTT客户端框架的完整设计文档。

---

## 📚 核心文档

### 🏗️ 架构设计

- **[ARCHITECTURE.md](ARCHITECTURE.md)** - 统一架构设计文档
  - 设计目标与原则
  - 整体架构
  - 核心组件设计
  - 配置管理
  - 线程安全设计
  - 持久化与幂等去重
  - 安全管理
  - 错误处理机制
  - 日志系统
  - 性能优化策略
  - 资源管理
  - wolfMQTT集成
  - 构建与依赖

### 🔌 API接口

- **[API_DESIGN.md](API_DESIGN.md)** - API接口设计文档
  - 完整的API接口清单
  - 所有公共接口定义
  - 参数、返回值、使用示例
  - 类型定义和回调接口

---

## 📖 阅读建议

### 新用户

1. 从 **[ARCHITECTURE.md](ARCHITECTURE.md)** 开始，了解整体架构和设计原则
2. 查看 **[API_DESIGN.md](API_DESIGN.md)**，了解如何使用API接口

### 开发者

1. **[ARCHITECTURE.md](ARCHITECTURE.md)** - 整体架构理解
2. **[API_DESIGN.md](API_DESIGN.md)** - API接口定义和实现指南

### 集成者

1. **[ARCHITECTURE.md](ARCHITECTURE.md)** - 架构概览和配置管理
2. **[API_DESIGN.md](API_DESIGN.md)** - API使用指南

---

## 🎯 文档特点

- **统一架构文档**: 所有核心设计内容整合在一个文档中
- **清晰的结构**: 按照架构师的思维组织，易于理解
- **工业级设计**: 专为嵌入式设备设计，注重可靠性和资源效率
- **完整覆盖**: 涵盖架构、配置、安全、性能等所有核心方面


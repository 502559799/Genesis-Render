# Genesis Render 架构设计文档

## 项目概述
Genesis Render 是一个基于 Vulkan 和 SDL2 的跨平台渲染引擎，支持实时相机捕获和图像处理。

## 整体架构

```
┌───────────────────────────────────────────────────────────────────┐
│                           Application Layer                        │
│  ┌──────────────┐  ┌──────────────┐  ┌─────────────────────────┐  │
│  │   Main Loop  │  │  ImGui UI    │  │  Camera Capture (AVF)   │  │
│  └──────────────┘  └──────────────┘  └─────────────────────────┘  │
└───────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌───────────────────────────────────────────────────────────────────┐
│                          Graphics Layer                            │
│  ┌──────────────┐  ┌──────────────┐  ┌─────────────────────────┐  │
│  │  GpuDevice   │  │  Swapchain   │  │   Render Pass/Commands   │  │
│  └──────────────┘  └──────────────┘  └─────────────────────────┘  │
└───────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌───────────────────────────────────────────────────────────────────┐
│                        Foundation Layer                            │
│  ┌──────────┐  ┌──────────┐  ┌────────┐  ┌────────┐  ┌─────────┐│
│  │  Memory  │  │  Window  │  │  Log   │  │  Array │  │Service  ││
│  └──────────┘  └──────────┘  └────────┘  └────────┘  └─────────┘│
└───────────────────────────────────────────────────────────────────┘
```

## 核心模块详解

### 1. Foundation Layer (基础层)

#### Memory Service
- **职责**: 统一内存管理服务
- **核心类**: `MemoryService`, `Allocator`, `HeapAllocator`
- **功能**: 
  - 系统堆分配器
  - 内存对齐分配
  - TLSF (Two-Level Segregated Fit) 内存分配算法

#### Window Service
- **职责**: 操作系统窗口管理
- **核心类**: `Window`
- **功能**:
  - SDL2 窗口创建与管理
  - 事件回调注册
  - Vulkan Surface 创建

#### Service Pattern
- **设计**: 单例服务模式
- **宏**: `GENESIS_DECLARE_SERVICE`, `GENESIS_SINGLETON_CLASS`
- **特点**: 全局访问，延迟初始化

### 2. Graphics Layer (图形层)

#### GpuDevice
- **职责**: Vulkan 设备管理和渲染调度
- **核心功能**:
  - Instance 创建与调试
  - 物理设备选择与逻辑设备创建
  - Swapchain 管理
  - Render Pass 创建
  - 命令池与命令缓冲区管理
  - 同步对象 (Semaphores, Fences)
  - 描述符池管理

#### 渲染流程
```
  beginFrame()
       │
       ▼
  vkAcquireNextImageKHR
       │
       ▼
  开始 Command Buffer
       │
       ▼
  Begin Render Pass
       │
       ▼
  [用户渲染 / ImGui 渲染]
       │
       ▼
  End Render Pass
       │
       ▼
  提交 Command Buffer
       │
       ▼
  vkQueuePresentKHR
```

#### GPU Resources
- **Buffer**: 顶点/索引/统一缓冲区管理
- **Texture**: 纹理创建与管理
- **Sampler**: 采样器状态
- **ShaderState**: 着色器状态管理

### 3. ImGui Integration

#### ImGuiService
- **职责**: ImGui UI 系统集成
- **后端**:
  - SDL2 后端: 输入事件处理
  - Vulkan 后端: 渲染指令生成
- **功能**:
  - ImDrawData -> Vulkan Command Buffer 转换
  - 字体纹理上传
  - 描述符集管理

## Camera Capture 模块

### macOS AVFoundation 集成
- **框架**: AVFoundation
- **输出格式**: BGRA 32-bit
- **帧率**: 30 FPS (可配置)
- **分辨率**: 1280x720 (默认)

### 纹理上传流程
1. 相机捕获帧 (CPU 内存)
2. 创建/更新 Vulkan 纹理
3. 转换布局为 `SHADER_READ_ONLY_OPTIMAL`
4. 着色器采样处理

## 滤镜系统

### 滤镜架构
```cpp
class Filter {
    virtual void apply(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) = 0;
};

class FilterPipeline {
    std::vector<Filter*> filters;
    void process(TextureHandle input, TextureHandle output);
};
```

### 内置滤镜
1. **灰度滤镜 (Grayscale)**: 亮度公式 `Y = 0.299R + 0.587G + 0.114B`
2. **高斯模糊 (Gaussian Blur)**: 5x5 内核，可分离卷积
3. **边缘检测 (Sobel)**: 水平+垂直梯度计算
4. **色彩调整**: 亮度、对比度、饱和度

## 着色器架构

### Compute Shader Pipeline
- 滤镜使用计算着色器实现
- 工作组大小: 16x16
- 支持滤镜链串行处理

### Vertex/Fragment Pipeline
- 用于最终屏幕显示
- 简单的纹理采样四边形

## 内存模型

### VMA (Vulkan Memory Allocator)
- 集成 GPUOpen VMA 库
- 自动内存类型选择
- 子分配与池管理
- 内存 defragmentation

### 主机一致性内存
- 用于Uniform缓冲区和暂存缓冲区
- 避免频繁的 map/unmap 操作

## 同步机制

### Frame 同步
- `MAX_FRAMES_IN_FLIGHT = 2`: 双缓冲
- `imageAvailableSemaphore`: 交换链图像可用
- `renderFinishedSemaphore`: 渲染完成可呈现
- `inFlightFence`: CPU-GPU 同步

### 资源同步
- 图像布局转换屏障
- 缓冲区拷贝屏障
- 使用 `VK_PIPELINE_STAGE_2_` 新API

## 构建系统

### CMake 目标
- `GenesisFoundation`: 基础层库
- `GenesisApp`: 应用层库  
- `GenesisExternal`: 第三方依赖
- `DEMO1`: 主可执行程序

### 依赖库
- Vulkan SDK: 图形 API
- SDL2: 窗口和输入
- ImGui: 调试 UI
- AVFoundation (macOS): 媒体捕获

## 性能优化点

1. **无临时缓冲区**: 尽可能重用已分配资源
2. **无锁队列**: 多线程渲染命令提交
3. **批处理**: 合并相似资源的描述符更新
4. **动态统一缓冲区**: 减少缓冲区分配和绑定

## 未来扩展方向

- [ ] 几何渲染管线
- [ ] 材质系统
- [ ] 场景图管理
- [ ] PBR 着色器
- [ ] 光线追踪支持
- [ ] HDR 显示输出
- [ ] 异步计算队列

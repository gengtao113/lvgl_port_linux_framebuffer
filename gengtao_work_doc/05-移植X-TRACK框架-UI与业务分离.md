# 05 移植 X-TRACK 框架：UI 与业务逻辑分离（MVP + PageManager + DataCenter）

> 目标工程：`lvgl_port_linux_framebuffer`
> 来源工程：`~/work_xtrack/X-TRACK`（开源 GPS 码表，AT32F435 + LVGL v8）
> 日期：2026-09-06
> 一句话：把 X-TRACK 里最值得学的那套「页面调度 + 数据总线 + MVP 分层」搬到我们的 Linux framebuffer LVGL 工程，让 `main.c` 不再是面条代码。

---

## 一、结论先行：移植什么、不移植什么

X-TRACK 很大（GPS/地图/轨迹/计步/SD 卡），**不要整体搬**。真正值得搬的是三个「框架件」，它们和业务无关、纯 C + LVGL：

| 框架件 | 解决什么问题 | 是否移植 |
|--------|-------------|---------|
| **MVP 分层** | UI 与业务逻辑分开（View / Presenter / Model） | ✅ 移植模式 |
| **PageManager** | 多页面的生命周期（创建/显示/离开/销毁）与切换动画 | ✅ 整目录拷 |
| **DataCenter** | 发布订阅总线，解耦数据生产与消费 | ✅ 整目录拷 |
| DataProc 节点（GPS/IMU/Storage/…） | X-TRACK 的具体业务 | ❌ 自己另写 |
| HAL（AT32 外设抽象） | MCU 外设 | ❌ Linux 已被 lv_drivers(fbdev/evdev) 替代 |
| 地图瓦片 / GPX / 轨迹压缩 | 产品算法 | ❌ 暂不移植 |

**为什么只搬这三件**：Linux 侧「硬件抽象」已经被 LVGL 的 `fbdev`（显示）+ `evdev`（输入）+ 操作系统文件系统天然解决了，X-TRACK 那套 HAL 在 Linux 上没必要再包一层。而页面调度、数据总线、MVP 分层是纯逻辑、跨平台，搬过来直接可用。

---

## 二、现状：我们的 main.c 就是「面条代码」

看现在的 `main.c`，`main()` 一个函数里做了四件事：

```c
int main(void)
{
    lv_init();
    fbdev_init();                 // ① 显示驱动
    /* 双缓冲 + 整屏 draw buffer */
    evdev_init();                 // ② 输入驱动
    /* 播放 GIF + 叠字性能标签 + 500ms 定时器 */
    while(1) { lv_timer_handler(); usleep(5000); }  // ③ 主循环
}
```

现在只有「播 GIF」一件事，勉强能看。但一旦要加「设置页」「地图页」「数据页」，就会变成：每个页面一堆 `lv_obj_create`、一堆全局变量、事件处理里直接读写屏幕——这正是那篇《嵌入式软件为什么越来越难维护》里说的「改一行全局崩溃」的前兆。

**目标**：把 `main.c` 瘦身成「只做 LVGL + fbdev/evdev 初始化 + 主循环」，页面和业务逻辑全部拆到框架里。

---

## 三、三个框架件各解决什么（先有概念）

### 3.1 MVP：UI 与业务分开（对齐那篇文章）

每个页面拆成三个文件：

```text
MyPage/
├── MyPage.c         Presenter + 页面生命周期（协调流程，不做判断细节）
├── MyPageView.c     View（只管「长啥样」：建哪些 label/button）
└── MyPageModel.c    Model（只管「数据和规则」：缓存、边界判断、数据来源）
```

规则：**View 不认识 Model，Model 不认识 View，Presenter 在中间传话。** 换屏只改 View，改业务只改 Model。

### 3.2 PageManager：多页面怎么切换、怎么不泄漏

解决「LVGL 多页面」最头疼的三件事：对象什么时候创建/销毁、编码器焦点什么时候挂/摘、离开页定时器什么时候停。它给每个页面一套生命周期回调：

```c
typedef struct {
    void (*on_load)(PageBase *p);           /* 首次创建：建 Model + View */
    void (*on_did_load)(PageBase *p);
    void (*on_will_appear)(PageBase *p);    /* 即将显示：挂焦点 */
    void (*on_did_appear)(PageBase *p);     /* 动画结束：起定时器 */
    void (*on_will_disappear)(PageBase *p); /* 即将离开：摘焦点、删定时器 */
    void (*on_did_disappear)(PageBase *p);
    void (*on_unload)(PageBase *p);         /* 销毁页面 */
    void (*on_did_unload)(PageBase *p);
    void (*destroy)(PageBase *p);
} PageOps;
```

页面用 `page_push(page, "Pages/MyPage", NULL)` 进、`page_pop(page)` 退，焦点/定时器/动画框架自动管。

### 3.3 DataCenter：数据怎么从「源」流到「界面」

页面不直接读传感器，而是走一条数据总线。四种动作：

- **Publish**（发布）：数据源把最新值广播出去
- **Pull**（拉取）：订阅者主动要一次数据
- **Notify**（通知）：请求某个数据节点「做件事」（如开始录轨）
- **Timer**（定时）：数据节点自己的周期回调

好处：传感器 100Hz 更新、界面 1Hz 刷新，中间靠总线解耦，互不阻塞。

---

## 四、文件清单：从 X-TRACK 拷哪些

来源根：`~/work_xtrack/X-TRACK/Software/X-Track/USER/App/`

### 4.1 PageManager（整目录，9 个文件）

```text
Utils/PageManager/
├── PageManager.h
├── PageBase.h / PageBase.c
├── PM_Base.c  PM_Anim.c  PM_Drag.c  PM_Router.c  PM_State.c
└── PM_Log.h
```

依赖：仅 `lvgl/lvgl.h` + 标准头。无任何 Arduino/HAL 依赖，可直接编译。

### 4.2 DataCenter（整目录，含子目录）

```text
Utils/DataCenter/
├── DataCenter.h / DataCenter.c
├── Account.h / Account.c
├── account_c.h
├── DataCenterLog.h
└── PingPongBuffer/ (PingPongBuffer.h / PingPongBuffer.c)
```

依赖：仅 `lvgl/lvgl.h` + 标准头。`Account` 用自带链表，不依赖 Linux `list.h`。

### 4.3 MVP 页面模板（拷一份当脚手架）

```text
Pages/_Template/
├── Template.h / Template.c        Presenter + 生命周期（base 必须是结构体第一项）
├── TemplateView.h / TemplateView.c
└── TemplateModel.h / TemplateModel.c
```

> **关键约定**：页面结构体必须把 `PageBase base` 放在**第一项**，调度器只认 `PageBase*`。

---

## 五、分阶段落地（每阶段都能编译、都能看到效果）

### 阶段 0：搭目录，把框架文件拷进来，先编译通过

1. 在本工程建目录：

```text
lvgl_port_linux_framebuffer/
└── App/
    ├── Utils/
    │   ├── PageManager/      ← 拷 4.1
    │   └── DataCenter/       ← 拷 4.2
    └── Pages/
        └── _Template/        ← 拷 4.3
```

2. 把 `main.c` 顶部的 include 路径改成相对本工程的（`App/Utils/PageManager/`）。

3. CMake 里把框架源文件加进编译（见第七节）。

**验收**：还没写任何业务，只是把框架编进 `demo`，`cmake --build` 通过、ARM ELF 正常生成。这一步先解决「路径、依赖、C 标准」三个坑。

### 阶段 1：用 PageManager 重构 main.c —— 从「一屏」到「多页框架」

把 `main()` 里那段「播 GIF + 叠字」整体移出，改成：

```c
/* main.c 瘦身版 */
int main(void)
{
    lv_init();
    fbdev_init();
    /* 双缓冲 draw buffer + disp_drv 注册（照旧） */
    evdev_init();
    /* 输入驱动注册（照旧） */

    App_Init();                        /* ← 新：初始化框架 + 装页面 + Push 首页 */

    while(1) { lv_timer_handler(); usleep(5000); }
}
```

新建 `App/App.c`，模仿 X-TRACK 的 `App.cpp`：

```c
void App_Init(void)
{
    static PageManager manager;
    PageManager_Init(&manager, AppFactory_CreatePage);

    /* 设置根页面默认样式（黑底） */
    /* 安装页面 */
    PageManager_Install(&manager, "GifPlayer", "Pages/GifPlayer");

    /* Push 首页 */
    PageManager_Push(&manager, "Pages/GifPlayer", NULL);
}
```

**验收**：编译运行，能进到你 Push 的那个页面（哪怕页面还是空的），说明 PageManager 骨架活了。

### 阶段 2：把现有 GIF 播放改造成一个「页面」（MVP 落地）

把 `main.c` 里 GIF 播放那段，按 MVP 拆成一个页面 `Pages/GifPlayer/`：

```text
GifPlayer.c         Presenter：on_load 建 view/model；on_did_appear 起刷新 timer；on_will_disappear 删 timer
GifPlayerView.c     View：lv_gif_create + 叠字性能标签（只管「长啥样」）
GifPlayerModel.c    Model：GIF 路径、当前状态等数据
```

具体：
- `GifPlayerView.c`：把 `lv_gif_create`、`lv_gif_set_src`、叠字 label 的创建都放这里。
- `GifPlayer.c`：`on_load` 调 `GifPlayerView_Create`；`on_did_appear` 里 `lv_timer_create(perf_timer_cb, 500, ...)`；`on_will_disappear` 里 `lv_timer_del`（对应原来 main 里的 500ms 性能刷新，正好用上「离开页删定时器」）。
- `GifPlayerModel.c`：存 `GIF_PATH`、模式状态，供 View/Presenter 读。

**验收**：编译运行，GIF 照常播放、叠字照常刷新；但代码已经从 `main.c` 一个函数，变成了「一个页面三文件」。到这一步，你就完成了「面条代码 → MVP」的第一次改造。

### 阶段 3：加第二个页面，验证「多页切换 + 生命周期」

复制 `_Template` → `Pages/Settings/`（或任何你要做的第二页），全改类名，在 `AppFactory_CreatePage` 注册、在 `App_Init` 里 `Install`，再从 GifPlayer 页面某个按键 `page_push` 过去。

**验收**：按按键（或 evdev 触摸）在两页之间 Push/Pop，观察：
- 切换有动画；
- 焦点在两个页面间正确转移；
- 离开 GifPlayer 时性能刷新定时器停、回来时又起（这就是生命周期生效的直接证据）。

### 阶段 4：引入 DataCenter，把「数据」从页面里解耦（按需）

当你需要「传感器/外部数据 → 界面」时，再上 DataCenter：

1. 建一个数据节点（例如 `DP_Env.c`，读温度/湿度，Publish 到总线）。
2. 页面 Model 订阅该节点、缓存；View 按自己节奏刷新。
3. 数据源 100Hz、界面 1Hz，靠总线解耦。

> 这一步不是必选。如果当前阶段只有一个 GIF + 一两个静态页，可以先不上 DataCenter，等真有「数据源」了再引入，避免过度设计（对应那篇文章的「先问自己三个问题」）。

---

## 六、目标目录结构（移植完成后）

```text
lvgl_port_linux_framebuffer/
├── main.c                        # 瘦身：LVGL + fbdev/evdev 初始化 + App_Init + 主循环
├── App/
│   ├── App.c / App.h             # 框架初始化、装页面、Push 首页
│   ├── Pages/
│   │   ├── AppFactory.c          # 名字 → 页面工厂（k_page_table）
│   │   ├── _Template/            # 脚手架（加页从复制它开始）
│   │   ├── GifPlayer/            # 原 GIF 播放改造成的页面（MVP 三文件）
│   │   └── ...                   # 以后每加一个页面一个目录
│   ├── Common/
│   │   └── DataProc/             # （阶段 4）你自己的数据节点
│   └── Utils/
│       ├── PageManager/          # 拷自 X-TRACK
│       └── DataCenter/           # 拷自 X-TRACK
├── lvgl/  lv_drivers/            # 原有，不动
├── lv_conf.h  lv_drv_conf.h
└── CMakeLists.txt  build_lvgl.sh
```

---

## 七、CMake 改动

把 `App/` 下的 `.c` 加进 `demo` 源列表。由于框架是纯 C、无子目录构建，用 GLOB 最简单：

```cmake
file(GLOB_RECURSE APP_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/App/*.c")

add_executable(demo
    main.c
    mouse_cursor_icon.c
    ${APP_SRCS}
)

target_include_directories(demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/App
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Utils/PageManager
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Utils/DataCenter
)
```

注意：
- 框架是 C99 语法，你工程已经是 `CMAKE_C_STANDARD 99`，无需改动。
- 若 `-Wall -Wextra` 对框架文件报 unused-function 之类，沿用现有 `-Wno-unused-function -Wno-unused-parameter -Wno-missing-field-initializers` 即可（框架源码里有部分预留函数未用）。
- 交叉编译仍走 `./build_lvgl.sh`，不用改工具链。

---

## 八、风险与注意事项

1. **LVGL 大版本要对齐**：X-TRACK 用 LVGL v8，本工程也是 v8，核心 API（`lv_obj` / `lv_timer` / `lv_anim` / `lv_group`）通用。若某处报「找不到 `lv_task_handler`」，本工程已用 `lv_timer_handler`（v8 正名），等价。
2. **`PageBase` 必须是页面结构体第一项**，否则 `PageBase*` 强转回页面会错位，必崩。
3. **只搬框架，不搬业务**：不要一上来就把 GPS/地图/轨迹也拷过来——那些依赖 MCU 外设和 SD 卡文件系统，Linux 侧语义不同，搬了编译都过不了。
4. **别过度设计**：如果确认短期只有「一个 GIF + 静态页面」，先只上 PageManager + MVP，DataCenter 等有真正的数据源再上。
5. **参考对应笔记**：X-TRACK 的 `gengtao_doc/`（尤其 14 PageManager、17 发布订阅、18 MVP、20 C 化边界、21 动手路线）已经把机制讲透，移植过程中随时对照。

---

## 九、验收清单

| 阶段 | 验收标准 |
|------|---------|
| 0 | 框架文件编进 `demo`，ARM ELF 正常生成，板子能起 |
| 1 | `main.c` 瘦身成功，`App_Init` 能 Push 出一个空页面 |
| 2 | GIF 播放改造为 `GifPlayer` 页三文件，功能不退化 |
| 3 | 两个页面 Push/Pop，动画 + 焦点 + 定时器生命周期正确 |
| 4 | （可选）一个数据节点 Publish，页面 Model 订阅后界面刷新 |

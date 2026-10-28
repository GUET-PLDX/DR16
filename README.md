# DR16

## 1. 模块作用
DR16 遥控解析模块。将 DBUS 数据转换为 CMD 输入和事件。
Manifest 描述：Receiver parsing

## 2. 主要函数说明
1. ThreadDr16: UART 线程仅接收、校验并交接带时间戳的原始帧。
2. ParseRC: 仅由 CMD 所属任务解析并应用数据及绑定事件，同帧完成后统一发布。
3. CheckoutOffline / Offline: 离线检测与失控处理。
4. GetEvent: 对外暴露事件绑定入口。
5. SyncSwitchEvents: 在 CMD 所属任务补发当前左右拨杆；EventBinder 只调用 RequestSwitchSync 请求补发。断链恢复首帧同步拨杆，键盘和鼠标以该帧为基线，避免虚假按下。

## 3. 接入步骤
1. 添加模块并确保 uart_dr16 配置正确。
2. 构造函数注入 CMD，模块会注册输入消费回调；不要在 UART 线程额外 FeedRC。
3. 在 EventBinder 中绑定开关和按键事件。

标准命令流程：
    xrobot_add_mod DR16 --instance-id dr16
    xrobot_gen_main
    cube-cmake --build /home/leo/Documents/bsp-dev-c/build/debug --

## 4. 配置示例（YAML）
module: DR16
entry_header: Modules/DR16/DR16.hpp
constructor_args:
  - CMD: '@cmd'
  - task_stack_depth_uart: 2048
template_args:
[]

## 5. 依赖与硬件
Required Hardware:
- dr16
- dma
- uart

Depends:
[]

## 6. 代码入口
Modules/DR16/DR16.hpp

## LibXR 2.0 静态装配

使用 xrobot 1.0.1 的 `module` / 有序 `args`；构造函数显式注入外设与服务，实例按依赖顺序创建。参数名和顺序以头文件中的构造函数为准，固件示例见 `User/RobotConfig/`。

`OnMonitor()` 为空。所属任务通过 `CMD::UpdateInputs()` 消费输入并检查 100 ms 失联；过期帧、长间隔或缓存溢出先产生离线输出，后续新帧可恢复。8 帧原始缓存使用短锁交接，处理事件和提交 CMD 时不持交接锁。同周期撤销开火不会被后续开火帧覆盖。

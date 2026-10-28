#pragma once

/* clang-format off */
/* === MODULE MANIFEST V2 ===
module_description: Receiver parsing
depends:
- id: GUET-PLDX/CMD
=== END MANIFEST === */
// clang-format on

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "CMD.hpp"
#include "libxr.hpp"
#include "thread.hpp"
#include "timebase.hpp"
#include "uart.hpp"

/**
 * @brief DR16 遥控器通道的取值范围。
 */
#define DR16_CH_VALUE_MIN (364u)  /* 通道最小值 */
#define DR16_CH_VALUE_MID (1024u) /* 通道中间值 */
#define DR16_CH_VALUE_MAX (1684u) /* 通道最大值 */

/* DR16 switch raw value: 1 = top, 2 = bottom, 3 = middle. */
#define DR16_SW_VALUE_UP (1u)   /* Switch top position */
#define DR16_SW_VALUE_DOWN (2u) /* Switch bottom position */
#define DR16_SW_VALUE_MID (3u)  /* Switch middle position */

/**
 * @class DR16
 * @brief DR16 遥控器 DBUS 接收与解析。
 * @details 从 UART 读取 18 字节 DBUS 帧，校验通道范围与拨杆状态，
 *          解码为 CMD::Data 后提交给 CMD，并在拨杆或按键变化时触发事件。
 */
class DR16 {
 public:
  /**
   * @brief 拨杆位置枚举。
   */
  enum class SwitchPos : uint8_t {
    DR16_SW_L_POS_TOP = 0x00,
    DR16_SW_L_POS_BOT = 0x01,
    DR16_SW_L_POS_MID = 0x02,
    DR16_SW_R_POS_TOP = 0x03,
    DR16_SW_R_POS_BOT = 0x04,
    DR16_SW_R_POS_MID = 0x05,
    DR16_SW_POS_NUM = 6
  };

  /**
   * @brief 按键编号；按键以位图形式存放在 Data::key 中。
   */
  enum class Key : uint8_t {
    KEY_W = static_cast<uint8_t>(SwitchPos::DR16_SW_POS_NUM),
    KEY_S,
    KEY_A,
    KEY_D,
    KEY_SHIFT,
    KEY_CTRL,
    KEY_Q,
    KEY_E,
    KEY_R,
    KEY_F,
    KEY_G,
    KEY_Z,
    KEY_X,
    KEY_C,
    KEY_V,
    KEY_B,
    KEY_L_PRESS,
    KEY_R_PRESS,
    KEY_L_RELEASE,
    KEY_R_RELEASE,
    KEY_NUM,
  };

  /**
   * @brief 计算带 Shift 修饰的按键事件编号。
   * @param key 基础按键。
   * @return 带 Shift 修饰的事件编号。
   */
  constexpr uint32_t ShiftWith(Key key) {
    return static_cast<uint8_t>(key) + 1 * static_cast<uint8_t>(Key::KEY_NUM);
  }

  /**
   * @brief 计算带 Ctrl 修饰的按键事件编号。
   * @param key 基础按键。
   * @return 带 Ctrl 修饰的事件编号。
   */
  constexpr uint32_t CtrlWith(Key key) {
    return static_cast<uint8_t>(key) + 2 * static_cast<uint8_t>(Key::KEY_NUM);
  }

  /**
   * @brief 计算带 Shift+Ctrl 修饰的按键事件编号。
   * @param key 基础按键。
   * @return 带 Shift+Ctrl 修饰的事件编号。
   */
  constexpr uint32_t ShiftCtrlWith(Key key) {
    return static_cast<uint8_t>(key) + 3 * static_cast<uint8_t>(Key::KEY_NUM);
  }

  constexpr uint32_t RawValue(Key key) {
    if (key >= Key::KEY_NUM) {
      return 0;
    }
    return 1 << (static_cast<uint8_t>(key) - static_cast<uint8_t>(Key::KEY_W));
  }

  typedef struct __attribute__((packed)) {
    uint16_t ch_r_x;
    uint16_t ch_r_y;
    uint16_t ch_l_x;
    uint16_t ch_l_y;
    uint8_t sw_r;
    uint8_t sw_l;
    int16_t x;
    int16_t y;
    int16_t z;
    uint8_t press_l;
    uint8_t press_r;
    uint16_t key;
    uint16_t res;
  } Data;

  /**
   * @brief 构造 DR16，配置 UART 并创建接收线程。
   * @param uart DBUS 串口；本构造配置为 100000 bps、偶校验、8 数据位、1
   * 停止位。
   * @param cmd 命令接收方 CMD。
   * @param task_stack_depth_uart UART 接收线程栈大小，字节。
   * @param thread_priority_uart UART 接收线程优先级。
   */
  DR16(LibXR::UART& uart, CMD& cmd, uint32_t task_stack_depth_uart = 2048,
       LibXR::Thread::Priority thread_priority_uart =
           LibXR::Thread::Priority::HIGH)
      : cmd_(&cmd), uart_(&uart), sem_(0), op_(sem_, 4) {
    uart_->SetConfig({100000, LibXR::UART::Parity::EVEN, 8, 1});
    /* 创建 UART 接收线程 */
    thread_uart_.Create(this, ThreadDr16, "uart_dr16", task_stack_depth_uart,
                        thread_priority_uart);
  }

  /**
   * @brief 获取 DR16 的事件对象。
   * @return LibXR::Event& 拨杆与按键事件。
   */
  LibXR::Event& GetEvent() { return dr16_event_; }

  /**
   * @brief 同步当前拨杆位置事件。
   * @details 事件绑定完成后补发当前拨杆状态，
   *          避免首个状态事件早于绑定而丢失。

   */
  void RequestSwitchSync() { SyncSwitchEvents(); }

  void SyncSwitchEvents() {
    if (last_data_.sw_l != 0) {
      this->dr16_event_.Active(
          static_cast<uint32_t>(SwitchPos::DR16_SW_L_POS_TOP) +
          last_data_.sw_l - 1);
    }
    if (last_data_.sw_r != 0) {
      this->dr16_event_.Active(
          static_cast<uint32_t>(SwitchPos::DR16_SW_R_POS_TOP) +
          last_data_.sw_r - 1);
    }
  }

  /**
   * @brief 监控循环空实现；接收由 UART 线程完成。
   */
  void OnMonitor() {}

  /**
   * @brief DR16 UART 接收线程入口。
   * @param dr16 DR16 对象指针。
   */
  static void ThreadDr16(DR16* dr16) {
    constexpr std::size_t RX_BUFFER_SIZE = 18;
    uint8_t rx_buffer[RX_BUFFER_SIZE] = {0};
    uint8_t frame_window[RX_BUFFER_SIZE] = {0};
    std::size_t frame_window_size = 0;
    Data decoded{};
    CMD::Data rc_data;

    auto last_time = LibXR::Timebase::GetMilliseconds();
    while (1) {
      if (dr16->uart_->Read({rx_buffer, RX_BUFFER_SIZE}, dr16->op_) ==
          LibXR::ErrorCode::OK) {
        for (std::size_t i = 0; i < RX_BUFFER_SIZE; ++i) {
          if (frame_window_size < RX_BUFFER_SIZE) {
            frame_window[frame_window_size++] = rx_buffer[i];
          } else {
            std::memmove(frame_window, frame_window + 1, RX_BUFFER_SIZE - 1);
            frame_window[RX_BUFFER_SIZE - 1] = rx_buffer[i];
          }

          if (frame_window_size < RX_BUFFER_SIZE) {
            continue;
          }

          if (DecodeRaw(frame_window, decoded) == LibXR::ErrorCode::OK) {
            if (dr16->ParseRC(frame_window, rc_data) == LibXR::ErrorCode::OK) {
              dr16->last_time_ = LibXR::Timebase::GetMilliseconds();
              dr16->cmd_->FeedRC(CMD::RCInputSource::RC_INPUT_DR16, rc_data);
            }
            frame_window_size = 0;
          }
        }
      }
      dr16->CheckoutOffline();
      LibXR::Thread::SleepUntil(last_time, 5);
    }
  }

  /**
   * @brief 由 CMD 所属任务解析并应用一帧 DBUS 数据，同时触发绑定事件。
   * @param raw_data 18 字节原始帧；UART 线程校验后调用本函数。
   * @param output_data 本帧生成的 CMD 数据。
   * @return LibXR::ErrorCode::OK 成功；其他值表示输入无效。
   */
  LibXR::ErrorCode ParseRC(const uint8_t* raw_data, CMD::Data& output_data) {
    Data curr_rc{};
    const auto result = DecodeRaw(raw_data, curr_rc);
    if (result != LibXR::ErrorCode::OK) return result;
#ifndef NDEBUG
    this->data_review_ = curr_rc;
#endif
    output_data = CMD::Data();

    constexpr float FULL_RANGE =
        static_cast<float>(DR16_CH_VALUE_MAX - DR16_CH_VALUE_MIN);
    constexpr float INV_FULL_RANGE = 1.0f / FULL_RANGE;
    constexpr float MOUSE_SCALER = 20.0f / 32768.0f;

    output_data.chassis.vx =
        2 * (static_cast<float>(curr_rc.ch_l_x) - DR16_CH_VALUE_MID) *
        INV_FULL_RANGE;
    output_data.chassis.vy =
        2 * (static_cast<float>(curr_rc.ch_l_y) - DR16_CH_VALUE_MID) *
        INV_FULL_RANGE;
    output_data.chassis.wz =
        -2 * (static_cast<float>(curr_rc.ch_r_x) - DR16_CH_VALUE_MID) *
        INV_FULL_RANGE;

    output_data.gimbal.yaw =
        -2 * (static_cast<float>(curr_rc.ch_r_x) - DR16_CH_VALUE_MID) *
        INV_FULL_RANGE;
    output_data.gimbal.pit =
        2 * (static_cast<float>(curr_rc.ch_r_y) - DR16_CH_VALUE_MID) *
        INV_FULL_RANGE;

    if (curr_rc.key & RawValue(Key::KEY_A)) {
      output_data.chassis.vx -= 1.0f;
    }
    if (curr_rc.key & RawValue(Key::KEY_D)) {
      output_data.chassis.vx += 1.0f;
    }
    if (curr_rc.key & RawValue(Key::KEY_S)) {
      output_data.chassis.vy -= 1.0f;
    }
    if (curr_rc.key & RawValue(Key::KEY_W)) {
      output_data.chassis.vy += 1.0f;
    }

    output_data.chassis.self_define = CMD::ChasStat::NONE;

    output_data.gimbal.pit += static_cast<float>(curr_rc.y) * MOUSE_SCALER;
    output_data.gimbal.yaw += -static_cast<float>(curr_rc.x) * MOUSE_SCALER;

    if (curr_rc.key & RawValue(Key::KEY_SHIFT) or
        curr_rc.res == DR16_CH_VALUE_MAX) {
      output_data.chassis.self_define = CMD::ChasStat::BOOST;
    }

    output_data.chassis.vx = std::clamp(output_data.chassis.vx, -1.0f, 1.0f);
    output_data.chassis.vy = std::clamp(output_data.chassis.vy, -1.0f, 1.0f);
    output_data.chassis.wz = std::clamp(output_data.chassis.wz, -1.0f, 1.0f);

    output_data.launcher.isfire =
        (curr_rc.res == DR16_CH_VALUE_MIN) or (curr_rc.press_l == 1) or
        (curr_rc.sw_r == DR16_SW_VALUE_MID); /* Right switch middle: feed */

    output_data.chassis_online = true;
    output_data.gimbal_online = true;
    output_data.ctrl_source = CMD::ControlSource::CTRL_SOURCE_RC;

    if (this->offline_latched_) {
      /* 断链恢复后的首帧只用于建立边沿基线，避免误触发事件 */
      this->last_data_ = curr_rc;
      this->offline_latched_ = false;
      this->SyncSwitchEvents();
    } else {
      this->ActivateChangedEvents(curr_rc);
      this->last_data_ = curr_rc;
    }

    return LibXR::ErrorCode::OK;
  }

  void Offline() {
    cmd_data_.chassis.vx = 0;
    cmd_data_.chassis.vy = 0;
    cmd_data_.chassis.wz = 0;
    cmd_data_.chassis.self_define = CMD::ChasStat::NONE;

    cmd_data_.gimbal.yaw = 0;
    cmd_data_.gimbal.pit = 0;

    cmd_data_.launcher.isfire = false;

    cmd_data_.chassis_online = false;
    cmd_data_.gimbal_online = false;

    cmd_->FeedRC(CMD::RCInputSource::RC_INPUT_DR16, cmd_data_);
    this->last_data_ = Data{};
    this->offline_latched_ = true;
  }

#ifdef LIBXR_DEBUG_BUILD
  /**
   * @brief
   * @brief 调试构建下的数据视图，便于在调试器中查看原始字段。
   */
  struct DataView {
    uint16_t ch_r_x; /* 右摇杆 X 通道原始值 */
    uint16_t ch_r_y; /* 右摇杆 Y 通道原始值 */
    uint16_t ch_l_x; /* 左摇杆 X 通道原始值 */
    uint16_t ch_l_y; /* 左摇杆 Y 通道原始值 */
    uint8_t sw_r;    /* 右拨杆位置 */
    uint8_t sw_l;    /* 左拨杆位置 */
    int16_t x;       /* 鼠标 X 位移 */
    int16_t y;       /* 鼠标 Y 位移 */
    int16_t z;       /* 鼠标滚轮位移 */
    uint8_t press_l; /* 左键按下状态 */
    uint8_t press_r; /* 右键按下状态 */
    uint16_t key;    /* 键盘按键位图 */
    uint16_t res;    /* 保留字段，用作备用档位判定 */
  };

  /**
   * @brief
   * @brief 把解码后的数据拷贝到调试视图。
   * @param data_view 调试视图。
   * @param data 解码后的数据。
   */
  void DataviewToData(DataView& data_view, Data& data) {
    data_view.ch_r_x = data.ch_r_x;
    data_view.ch_r_y = data.ch_r_y;
    data_view.ch_l_x = data.ch_l_x;
    data_view.ch_l_y = data.ch_l_y;
    data_view.sw_r = data.sw_r;
    data_view.sw_l = data.sw_l;
    data_view.x = data.x;
    data_view.y = data.y;
    data_view.z = data.z;
    data_view.press_l = data.press_l;
    data_view.press_r = data.press_r;
    data_view.key = data.key;
    data_view.res = data.res;
  }
#endif

 private:
  static LibXR::ErrorCode DecodeRaw(const uint8_t* raw_data, Data& curr_rc) {
    if (!raw_data) {
      return LibXR::ErrorCode::PTR_NULL;
    };

    curr_rc.ch_r_x = ((raw_data[0] | raw_data[1] << 8) & 0x07FF);
    curr_rc.ch_r_y = ((raw_data[1] >> 3 | raw_data[2] << 5) & 0x07FF);
    curr_rc.ch_l_x =
        ((raw_data[2] >> 6 | raw_data[3] << 2 | raw_data[4] << 10) & 0x07FF);
    curr_rc.ch_l_y = ((raw_data[4] >> 1 | raw_data[5] << 7) & 0x07FF);

    curr_rc.sw_r = ((raw_data[5] >> 4) & 0x0003);  // bits 4-5
    curr_rc.sw_l = ((raw_data[5] >> 6) & 0x0003);  // bits 6-7

    curr_rc.x = static_cast<int16_t>(raw_data[6] | raw_data[7] << 8);
    curr_rc.y = static_cast<int16_t>(raw_data[8] | raw_data[9] << 8);
    curr_rc.z = static_cast<int16_t>(raw_data[10] | raw_data[11] << 8);

    curr_rc.press_l = raw_data[12];
    curr_rc.press_r = raw_data[13];

    curr_rc.key = static_cast<uint16_t>(raw_data[14] | raw_data[15] << 8);

    curr_rc.res = static_cast<uint16_t>(raw_data[16] | raw_data[17] << 8);

    if (curr_rc.ch_l_x < DR16_CH_VALUE_MIN ||
        curr_rc.ch_l_x > DR16_CH_VALUE_MAX ||
        curr_rc.ch_l_y < DR16_CH_VALUE_MIN ||
        curr_rc.ch_l_y > DR16_CH_VALUE_MAX ||
        curr_rc.ch_r_x < DR16_CH_VALUE_MIN ||
        curr_rc.ch_r_x > DR16_CH_VALUE_MAX ||
        curr_rc.ch_r_y < DR16_CH_VALUE_MIN ||
        curr_rc.ch_r_y > DR16_CH_VALUE_MAX) {
      return LibXR::ErrorCode::CHECK_ERR;
    }

    if (curr_rc.sw_l == 0 || curr_rc.sw_r == 0) {
      return LibXR::ErrorCode::CHECK_ERR;
    }

    return LibXR::ErrorCode::OK;
  }
  CMD* cmd_; /* CMD 接收方 */

  Data last_data_{};     /* 上一帧数据 */
  CMD::Data cmd_data_{}; /* 离线时提交的命令数据 */

  /* 断链后下一帧有效数据只作为边沿基线 */
  bool offline_latched_ = false;
#ifndef NDEBUG
  Data data_review_; /* 最近一帧原始解码数据，供调试查看 */
#endif
  LibXR::UART* uart_;                       /* UART 句柄 */
  LibXR::Event dr16_event_;                 /* 拨杆与按键事件 */
  LibXR::Thread thread_uart_;               /* UART 接收线程 */
  LibXR::Semaphore sem_;                    /* 读操作信号量 */
  LibXR::ReadOperation op_;                 /* UART 读操作 */
  LibXR::MillisecondTimestamp last_time_{}; /* 最近一次有效帧时间 */

  /*-------------------------- 事件处理 --------------------------*/
  void ActivateChangedEvents(const Data& curr_rc) {
    if (curr_rc.sw_l != this->last_data_.sw_l) {
      this->dr16_event_.Active(
          static_cast<uint32_t>(SwitchPos::DR16_SW_L_POS_TOP) + curr_rc.sw_l -
          1);
    }
    if (curr_rc.sw_r != this->last_data_.sw_r) {
      this->dr16_event_.Active(
          static_cast<uint32_t>(SwitchPos::DR16_SW_R_POS_TOP) + curr_rc.sw_r -
          1);
    }

    uint32_t modifier_offset = 0;

    if (curr_rc.key & RawValue(Key::KEY_SHIFT)) {
      modifier_offset += static_cast<uint32_t>(Key::KEY_NUM);
    }
    if (curr_rc.key & RawValue(Key::KEY_CTRL)) {
      modifier_offset += 2 * static_cast<uint32_t>(Key::KEY_NUM);
    }

    for (int i = 0; i < 16; i++) {
      if ((curr_rc.key & (1 << i)) && !(this->last_data_.key & (1 << i))) {
        this->dr16_event_.Active(static_cast<uint32_t>(Key::KEY_W) + i +
                                 modifier_offset);
      }
    }

    if (curr_rc.press_l && !this->last_data_.press_l) {
      this->dr16_event_.Active(static_cast<uint32_t>(Key::KEY_L_PRESS));
    }
    if (!curr_rc.press_l && this->last_data_.press_l) {
      this->dr16_event_.Active(static_cast<uint32_t>(Key::KEY_L_RELEASE));
    }
    if (curr_rc.press_r && !this->last_data_.press_r) {
      this->dr16_event_.Active(static_cast<uint32_t>(Key::KEY_R_PRESS));
    }
    if (!curr_rc.press_r && this->last_data_.press_r) {
      this->dr16_event_.Active(static_cast<uint32_t>(Key::KEY_R_RELEASE));
    }
  }

  void CheckoutOffline() {
    auto current_time = LibXR::Timebase::GetMilliseconds();
    if ((current_time - last_time_).ToMillisecond() > 100) {
      if (!this->offline_latched_) {
        Offline();
      }
    }
  }
};

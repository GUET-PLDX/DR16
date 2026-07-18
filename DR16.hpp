#pragma once

/* clang-format off */
/* === MODULE MANIFEST ===
module_name: DR16
module_description: Receiver parsing
constructor_args:
  - CMD: '@cmd'
  - task_stack_depth_uart: 2048
  - thread_priority_uart: LibXR::Thread::Priority::HIGH
required_hardware: dr16 dma uart
=== END MANIFEST === */
// clang-format on

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "CMD.hpp"
#include "app_framework.hpp"
#include "thread.hpp"
#include "timebase.hpp"
#include "uart.hpp"

/**
 * @brief DR16閬ユ帶鍣ㄩ€氶亾鍊艰寖鍥村畾涔�
 */
#define DR16_CH_VALUE_MIN (364u)  /* 閫氶亾鏈€灏忓€� */
#define DR16_CH_VALUE_MID (1024u) /* 閫氶亾涓棿鍊� */
#define DR16_CH_VALUE_MAX (1684u) /* 閫氶亾鏈€澶у€� */

/**
 * @class DR16
 * @brief DR16閬ユ帶鍣ㄦ暟鎹В鏋愮被
 * @details
 * 璐熻矗鎺ユ敹鍜岃В鏋怐R16閬ユ帶鍣ㄧ殑鏁版嵁锛屽寘鎷憞鏉嗭拷锟斤拷鎷ㄦ潌鍜屾寜閿瓑淇℃伅
 */
class DR16 : public LibXR::Application {
 public:
  /**
   * @brief 鎷ㄦ潌寮€鍏充綅缃灇涓�
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
   * @brief 鎸夐敭鏋氫妇    SET_MODE_RELAX,
    SET_MODE_FOLLOW,
    SET_MODE_ROTOR,
    SET_MODE_INDENPENDENT,
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
   * @brief 璁＄畻Shift缁勫悎閿殑缂栫爜鍊�
   * @param key 鍩虹鎸夐敭
   * @return Shift缁勫悎閿殑缂栫爜鍊�
   */
  constexpr uint32_t ShiftWith(Key key) {
    return static_cast<uint8_t>(key) + 1 * static_cast<uint8_t>(Key::KEY_NUM);
  }

  /**
   * @brief 璁＄畻Ctrl缁勫悎閿殑缂栫爜鍊�
   * @param key 鍩虹鎸夐敭
   * @return Ctrl缁勫悎閿殑缂栫爜鍊�
   */
  constexpr uint32_t CtrlWith(Key key) {
    return static_cast<uint8_t>(key) + 2 * static_cast<uint8_t>(Key::KEY_NUM);
  }

  /**
   * @brief 璁＄畻Shift+Ctrl缁勫悎閿殑缂栫爜鍊�
   * @param key 鍩虹鎸夐敭
   * @return Shift+Ctrl缁勫悎閿殑缂栫爜鍊�
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
   * @brief DR16鏋勯€犲嚱鏁�
   * @param hw 纭欢瀹瑰櫒寮曠敤
   * @param app 搴旂敤绠＄悊鍣ㄥ紩鐢�
   * @param cmd 鎺у埗鍛戒护瀵硅薄寮曠敤
   * @param task_stack_depth_uart UART浠诲姟鏍堟繁搴�
   * @param cmd_data_tp_name CMD鏁版嵁涓婚鍚嶇О
   */
  DR16(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app, CMD& cmd,
       uint32_t task_stack_depth_uart,
       LibXR::Thread::Priority thread_priority_uart =
           LibXR::Thread::Priority::MEDIUM)
      : cmd_(&cmd),
        uart_(hw.Find<LibXR::UART>("uart_dr16")),
        sem_(0),
        op_(sem_, 4) {
    uart_->SetConfig({100000, LibXR::UART::Parity::EVEN, 8, 1});
    /* 鍒涘缓UART绾跨▼ */
    thread_uart_.Create(this, ThreadDr16, "uart_dr16", task_stack_depth_uart,
                        thread_priority_uart);
    app.Register(*this);
  }

  /**
   * @brief 鑾峰彇 DR16 鐨勪簨浠跺鐞嗗櫒
   * @return LibXR::Event& 浜嬩欢澶勭悊鍣ㄧ殑寮曠敤
   */
  LibXR::Event& GetEvent() { return dr16_event_; }

  /**
   * @brief 同步当前拨杆状态事件
   * @details
   * 用于事件绑定完成后补发当前拨杆状态，避免上电首帧事件早于绑定而丢失。

   */
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
   * @brief 鐩戞帶鍑芥暟閲嶅啓
   */
  void OnMonitor() override {}

  /**
   * @brief DR16 UART璇诲彇绾跨▼鍑芥暟
   * @param dr16 DR16瀹炰緥鎸囬拡
   */
  static void ThreadDr16(DR16* dr16) {
    constexpr std::size_t RX_BUFFER_SIZE = 18;
    uint8_t rx_buffer[RX_BUFFER_SIZE] = {0};
    uint8_t frame_window[RX_BUFFER_SIZE] = {0};
    std::size_t frame_window_size = 0;
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

          if (dr16->ParseRC(frame_window, rc_data) == LibXR::ErrorCode::OK) {
            dr16->last_time_ = LibXR::Timebase::GetMilliseconds();
            dr16->cmd_->FeedRC(CMD::RCInputSource::RC_INPUT_DR16, rc_data);
            frame_window_size = 0;
          }
        }
      }
      dr16->CheckoutOffline();
      LibXR::Thread::SleepUntil(last_time, 5);
    }
  }

  /**
   * @brief
   * 瑙ｆ瀽閬ユ帶鍣ㄦ暟鎹苟杞崲涓烘帶鍒跺懡浠�
   */
  /**
   * @brief 瑙ｆ瀽 DBUS 鍘熷鏁版嵁骞剁敓鎴愭帶鍒舵寚浠�
   * @param raw_data 18瀛楄妭鐨勫師濮嬫帴鏀剁紦鍐� (鏉ヨ嚜 ThreadDr16 鐨�
   * rx_buffer)
   * @param output_data 瑙ｆ瀽鍚庣殑 CMD 鏁版嵁 (鐢ㄤ簬鎻愪氦缁欎簯鍙�)
   * @return true 瑙ｆ瀽鎴愬姛, false 鏁版嵁鏃犳晥
   */
  LibXR::ErrorCode ParseRC(const uint8_t* raw_data, CMD::Data& output_data) {
    if (!raw_data) {
      return LibXR::ErrorCode::PTR_NULL;
    };

    Data curr_rc{};

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

#ifndef NDEBUG
    this->data_review_ = curr_rc;
#endif

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

    output_data = CMD::Data();

    constexpr float FULL_RANGE =
        static_cast<float>(DR16_CH_VALUE_MAX - DR16_CH_VALUE_MIN);
    constexpr float INV_FULL_RANGE = 1.0f / FULL_RANGE;
    constexpr float MOUSE_SCALER = 20.0f / 32768.0f;

    output_data.chassis.x =
        2 * (static_cast<float>(curr_rc.ch_l_x) - DR16_CH_VALUE_MID) *
        INV_FULL_RANGE;
    output_data.chassis.y =
        2 * (static_cast<float>(curr_rc.ch_l_y) - DR16_CH_VALUE_MID) *
        INV_FULL_RANGE;
    output_data.chassis.z =
        -2 * (static_cast<float>(curr_rc.ch_r_x) - DR16_CH_VALUE_MID) *
        INV_FULL_RANGE;

    output_data.gimbal.yaw =
        -2 * (static_cast<float>(curr_rc.ch_r_x) - DR16_CH_VALUE_MID) *
        INV_FULL_RANGE;
    output_data.gimbal.pit =
        2 * (static_cast<float>(curr_rc.ch_r_y) - DR16_CH_VALUE_MID) *
        INV_FULL_RANGE;

    if (curr_rc.key & RawValue(Key::KEY_A)) {
      output_data.chassis.x -= 1.0f;
    }
    if (curr_rc.key & RawValue(Key::KEY_D)) {
      output_data.chassis.x += 1.0f;
    }
    if (curr_rc.key & RawValue(Key::KEY_S)) {
      output_data.chassis.y -= 1.0f;
    }
    if (curr_rc.key & RawValue(Key::KEY_W)) {
      output_data.chassis.y += 1.0f;
    }

    output_data.chassis.self_define = CMD::ChasStat::NONE;

    output_data.gimbal.pit += static_cast<float>(curr_rc.y) * MOUSE_SCALER;
    output_data.gimbal.yaw += -static_cast<float>(curr_rc.x) * MOUSE_SCALER;

    if (curr_rc.key & RawValue(Key::KEY_SHIFT) or
        curr_rc.res == DR16_CH_VALUE_MAX) {
      output_data.chassis.self_define = CMD::ChasStat::BOOST;
    }

    if (curr_rc.key & RawValue(Key::KEY_C) or
        curr_rc.res == DR16_CH_VALUE_MIN) {
      output_data.chassis.self_define = CMD::ChasStat::STRETCH;
    }

    output_data.chassis.x = std::clamp(output_data.chassis.x, -1.0f, 1.0f);
    output_data.chassis.y = std::clamp(output_data.chassis.y, -1.0f, 1.0f);
    output_data.chassis.z = std::clamp(output_data.chassis.z, -1.0f, 1.0f);

    output_data.launcher.isfire =
        (curr_rc.res == DR16_CH_VALUE_MIN) or (curr_rc.press_l == 1);

    output_data.chassis_online = true;
    output_data.gimbal_online = true;
    output_data.ctrl_source = CMD::ControlSource::CTRL_SOURCE_RC;

    if (this->offline_latched_) {
      /* 断链恢复后的首帧仅用于建立边沿基线，避免误触发事件 */
      this->last_data_ = curr_rc;
      this->offline_latched_ = false;
    } else {
      this->ActivateChangedEvents(curr_rc);
      this->last_data_ = curr_rc;
    }

    return LibXR::ErrorCode::OK;
  }

  void Offline() {
    cmd_data_.chassis.x = 0;
    cmd_data_.chassis.y = 0;
    cmd_data_.chassis.z = 0;
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
   * 鐢ㄤ簬璋冭瘯鐨勬暟鎹鍥剧粨鏋勪綋锛堥潪浣嶅煙锛�
   */
  struct DataView {
    uint16_t ch_r_x; /* 鍙虫憞鏉哫杞� */
    uint16_t ch_r_y; /* 鍙虫憞鏉哬杞� */
    uint16_t ch_l_x; /* 宸︽憞鏉哫杞� */
    uint16_t ch_l_y; /* 宸︽憞鏉哬杞� */
    uint8_t sw_r;    /* 鍙虫嫧鏉� */
    uint8_t sw_l;    /* 宸︽嫧鏉� */
    int16_t x;       /* 榧犳爣X杞寸Щ鍔� */
    int16_t y;       /* 榧犳爣Y杞寸Щ鍔� */
    int16_t z;       /* 榧犳爣Z杞寸Щ鍔� */
    uint8_t press_l; /* 榧犳爣宸﹂敭鐘舵€� */
    uint8_t press_r; /* 榧犳爣鍙抽敭鐘舵€� */
    uint16_t key;    /* 閿洏鎸夐敭鐘舵€� */
    uint16_t res;    /* 淇濈暀瀛楁 */
  };

  /**
   * @brief
   * 灏嗕綅鍩熸暟鎹浆鎹负鏅€氱粨鏋勪綋鏁版嵁锛堣皟璇曠敤锛�
   * @param data_view 杈撳嚭鐨勬暟鎹鍥�
   * @param data 杈撳叆鐨勪綅鍩熸暟鎹�
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
  CMD* cmd_; /* CMD妯″潡鎸囬拡 */

  Data last_data_{};     /* 上一帧数据 */
  CMD::Data cmd_data_{}; /* 命令数据 */

  /* 离线后下一帧有效数据只作为边沿基线 */
  bool offline_latched_ = false;
#ifndef NDEBUG
  Data data_review_; /* 鍛戒护鏁版嵁棰勮 */
#endif
  LibXR::UART* uart_;         /* UART鎺ュ彛鎸囬拡 */
  LibXR::Event dr16_event_;   /* 浜嬩欢澶勭悊鍣� */
  LibXR::Thread thread_uart_; /* UART绾跨▼ */
  LibXR::Semaphore sem_;      /* 璇绘搷浣滀俊鍙烽噺 */
  LibXR::ReadOperation op_;   /* 璇绘搷浣滐紙闃诲鍨嬶級 */
  LibXR::MillisecondTimestamp last_time_{}; /* 涓婃鎺ユ敹鏃堕棿 */

  /*--------------------------宸ュ叿鍑芥暟-------------------------------------------------*/
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

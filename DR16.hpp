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
 * @brief
 * DR16闁儲甯堕崳銊┾偓姘朵壕閸婅壈瀵栭崶鏉戠暰娑旓拷
 */
#define DR16_CH_VALUE_MIN \
  (364u) /* 闁岸浜鹃張鈧亸蹇撯偓锟� */
#define DR16_CH_VALUE_MID (1024u) /* 闁岸浜炬稉顓㈡？閸婏拷 */
#define DR16_CH_VALUE_MAX                            \
  (1684u) /* 闁岸浜鹃張鈧径褍鈧拷 \
           */

/* DR16 switch raw value: 1 = top, 2 = bottom, 3 = middle. */
#define DR16_SW_VALUE_UP (1u)   /* Switch top position */
#define DR16_SW_VALUE_DOWN (2u) /* Switch bottom position */
#define DR16_SW_VALUE_MID (3u)  /* Switch middle position */

/**
 * @class DR16
 * @brief DR16闁儲甯堕崳銊︽殶閹诡喛袙閺嬫劗琚�
 * @details
 * 鐠愮喕鐭楅幒銉︽暪閸滃矁袙閺嬫€怰16闁儲甯堕崳銊ф畱閺佺増宓侀敍灞藉瘶閹奉剚鎲為弶鍡嫹閿熸枻鎷烽幏銊︽綄閸滃本瀵滈柨顔剧搼娣団剝浼�
 */
class DR16 {
 public:
  /**
   * @brief 閹枫劍娼屽鈧崗鍏呯秴缂冾喗鐏囨稉锟�
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
   * @brief 閹稿鏁弸姘    SET_MODE_RELAX,
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
   * @brief 鐠侊紕鐣籗hift缂佸嫬鎮庨柨顔炬畱缂傛牜鐖滈崐锟�
   * @param key 閸╄櫣顢呴幐澶愭暛
   * @return Shift缂佸嫬鎮庨柨顔炬畱缂傛牜鐖滈崐锟�
   */
  constexpr uint32_t ShiftWith(Key key) {
    return static_cast<uint8_t>(key) + 1 * static_cast<uint8_t>(Key::KEY_NUM);
  }

  /**
   * @brief 鐠侊紕鐣籆trl缂佸嫬鎮庨柨顔炬畱缂傛牜鐖滈崐锟�
   * @param key 閸╄櫣顢呴幐澶愭暛
   * @return Ctrl缂佸嫬鎮庨柨顔炬畱缂傛牜鐖滈崐锟�
   */
  constexpr uint32_t CtrlWith(Key key) {
    return static_cast<uint8_t>(key) + 2 * static_cast<uint8_t>(Key::KEY_NUM);
  }

  /**
   * @brief 鐠侊紕鐣籗hift+Ctrl缂佸嫬鎮庨柨顔炬畱缂傛牜鐖滈崐锟�
   * @param key 閸╄櫣顢呴幐澶愭暛
   * @return Shift+Ctrl缂佸嫬鎮庨柨顔炬畱缂傛牜鐖滈崐锟�
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
   * @brief DR16閺嬪嫰鈧姴鍤遍弫锟�
   * @param cmd 閹貉冨煑閸涙垝鎶ょ€电钖勫鏇犳暏
   * @param task_stack_depth_uart UART娴犺濮熼弽鍫熺箒鎼达拷
   * @param cmd_data_tp_name CMD閺佺増宓佹稉濠氼暯閸氬秶袨
   */
  DR16(LibXR::UART& uart, CMD& cmd, uint32_t task_stack_depth_uart = 2048,
       LibXR::Thread::Priority thread_priority_uart =
           LibXR::Thread::Priority::HIGH)
      : cmd_(&cmd), uart_(&uart), sem_(0), op_(sem_, 4) {
    uart_->SetConfig({100000, LibXR::UART::Parity::EVEN, 8, 1});
    /*閸掓稑缂揢ART缁捐法鈻� */
    thread_uart_.Create(this, ThreadDr16, "uart_dr16", task_stack_depth_uart,
                        thread_priority_uart);
  }

  /**
   * @brief 閼惧嘲褰� DR16 閻ㄥ嫪绨ㄦ禒璺侯槱閻炲棗娅�
   * @return LibXR::Event&
   * 娴滃娆㈡径鍕倞閸ｃ劎娈戝鏇犳暏
   */
  LibXR::Event& GetEvent() { return dr16_event_; }

  /**
   * @brief 鍚屾褰撳墠鎷ㄦ潌鐘舵€佷簨浠�
   * @details
   * 鐢ㄤ簬浜嬩欢缁戝畾瀹屾垚鍚庤ˉ鍙戝綋鍓嶆嫧鏉嗙姸鎬侊紝閬垮厤涓婄數棣栧抚浜嬩欢鏃╀簬缁戝畾鑰屼涪澶便€�

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
   * @brief 閻╂垶甯堕崙鑺ユ殶闁插秴鍟�
   */
  void OnMonitor() {}

  /**
   * @brief DR16 UART鐠囪褰囩痪璺ㄢ柤閸戣姤鏆�
   * @param dr16 DR16鐎圭偘绶ラ幐鍥嫛
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
   * @brief
   * 鐟欙絾鐎介柆銉﹀付閸ｃ劍鏆熼幑顔艰嫙鏉烆剚宕叉稉鐑樺付閸掕泛鎳℃禒锟�
   */
  /**
   * @brief 鍦� CMD 鎵€灞炰换鍔¤В鏋愬苟搴旂敤涓€甯� DBUS
   * 鏁版嵁鍙婄粦瀹氫簨浠躲€�
   * @param raw_data 18 瀛楄妭鏈夋晥鍘熷甯э紱UART
   * 绾跨▼鍙皟鐢� DecodeRaw 鏍￠獙銆�
   * @param output_data 鏈抚鐢熸垚鐨� CMD 鏁版嵁銆�
   * @return LibXR::ErrorCode::OK 鎴愬姛锛涘叾浠栧€艰〃绀鸿緭鍏ユ棤鏁堛€�
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
      /* 鏂摼鎭㈠鍚庣殑棣栧抚浠呯敤浜庡缓绔嬭竟娌垮熀绾匡紝閬垮厤璇Е鍙戜簨浠�
       */
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
   * 閻€劋绨拫鍐槸閻ㄥ嫭鏆熼幑顔款潒閸ュ墽绮ㄩ弸鍕秼閿涘牓娼担宥呯厵閿涳拷
   */
  struct DataView {
    uint16_t ch_r_x; /* 閸欒櫕鎲為弶鍝潪锟� */
    uint16_t ch_r_y; /* 閸欒櫕鎲為弶鍝潪锟� */
    uint16_t ch_l_x; /* 瀹革附鎲為弶鍝潪锟� */
    uint16_t ch_l_y; /* 瀹革附鎲為弶鍝潪锟� */
    uint8_t sw_r;    /* 閸欒櫕瀚ч弶锟� */
    uint8_t sw_l;    /* 瀹革附瀚ч弶锟� */
    int16_t x;       /* 姒х姵鐖鏉炲些閸旓拷 */
    int16_t y;       /* 姒х姵鐖鏉炲些閸旓拷 */
    int16_t z;       /* 姒х姵鐖鏉炲些閸旓拷 */
    uint8_t press_l; /* 姒х姵鐖ｅ锕傛暛閻樿埖鈧拷 */
    uint8_t press_r; /* 姒х姵鐖ｉ崣鎶芥暛閻樿埖鈧拷 */
    uint16_t key;    /* 闁款喚娲忛幐澶愭暛閻樿埖鈧拷 */
    uint16_t res;    /* 娣囨繄鏆€鐎涙顔� */
  };

  /**
   * @brief
   * 鐏忓棔缍呴崺鐔告殶閹诡喛娴嗛幑顫礋閺咁噣鈧氨绮ㄩ弸鍕秼閺佺増宓侀敍鍫ｇ殶鐠囨洜鏁ら敍锟�
   * @param data_view 鏉堟挸鍤惃鍕殶閹诡喛顫嬮崶锟�
   * @param data 鏉堟挸鍙嗛惃鍕秴閸╃喐鏆熼幑锟�
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
  CMD* cmd_; /* CMD濡€虫健閹稿洭鎷� */

  Data last_data_{};     /* 涓婁竴甯ф暟鎹� */
  CMD::Data cmd_data_{}; /* 鍛戒护鏁版嵁 */

  /* 绂荤嚎鍚庝笅涓€甯ф湁鏁堟暟鎹彧浣滀负杈规部鍩虹嚎
   */
  bool offline_latched_ = false;
#ifndef NDEBUG
  Data data_review_; /* 閸涙垝鎶ら弫鐗堝祦妫板嫯顫� */
#endif
  LibXR::UART* uart_;         /* UART閹恒儱褰涢幐鍥嫛 */
  LibXR::Event dr16_event_;   /* 娴滃娆㈡径鍕倞閸ｏ拷 */
  LibXR::Thread thread_uart_; /* UART缁捐法鈻� */
  LibXR::Semaphore sem_;      /* 鐠囩粯鎼锋担婊€淇婇崣鐑藉櫤 */
  LibXR::ReadOperation
      op_; /* 鐠囩粯鎼锋担婊愮礄闂冭顢ｉ崹瀣剁礆 */
  LibXR::MillisecondTimestamp last_time_{}; /* 娑撳﹥顐奸幒銉︽暪閺冨爼妫� */

  /*--------------------------瀹搞儱鍙块崙鑺ユ殶-------------------------------------------------*/
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

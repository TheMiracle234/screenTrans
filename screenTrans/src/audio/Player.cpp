#include <audio/Player.hpp>
#include <iostream>
#include <cassert>
namespace audio {

    void Player::init(uint32_t sampleRate, uint32_t channels, uint32_t periodSizeInFrames, void* user, ma_device_data_proc dataCallBack) {
        if (m_flag & flag_initiated) {
            ma_device_uninit(&m_device);
        }

        m_channels = channels;
        m_periodSizeInFrames = periodSizeInFrames;

        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = channels;

        config.sampleRate = sampleRate;
        config.periodSizeInFrames = periodSizeInFrames;
        config.periods = 2;

        config.dataCallback = dataCallBack;
        config.pUserData = user;

        //config.noFixedSizedCallback = MA_TRUE;

        ma_result result = ma_device_init(nullptr, &config, &m_device);
        assert(result == MA_SUCCESS);

        m_flag |= flag_initiated;
    }

    
    Player::Player(uint32_t sampleRate, uint32_t channels, uint32_t periodSizeInFrames, void* user, ma_device_data_proc dataCallBack) {
        init(sampleRate, channels, periodSizeInFrames, user, dataCallBack);
    }

    
    Player::~Player() {
        stop();
        if (m_flag & flag_initiated) {
            ma_device_uninit(&m_device);
        }
    }

    
    void Player::reset(uint32_t sampleRate, uint32_t channels, uint32_t periodSizeInFrames, void* user, ma_device_data_proc dataCallBack) {
        stop();
        init(sampleRate, channels, periodSizeInFrames, user, dataCallBack);
    }

    
    bool Player::start() {
        auto result = ma_device_start(&m_device);
        if (result != MA_SUCCESS) {
            std::cerr << "Player::Start() error" << std::endl;
            return false;
        }
        m_flag |= flag_isRunning;
        return true;
    }

    
    void Player::stop() {
        m_flag &= ~flag_isRunning;
        ma_device_stop(&m_device);
    }
}
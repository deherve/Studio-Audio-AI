#include "player.hpp"

#include <windows.h>
#include <mmsystem.h>

#include <cwchar>
#include <cstdlib>

static std::wstring mci(const std::wstring & command) {
    wchar_t result[128] = {};
    mciSendStringW(command.c_str(), result, 128, nullptr);
    return result;
}

bool Player::open(const std::string & id, const std::wstring & path) {
    close();
    // mpegvideo passe par Media Foundation/DirectShow : lit MP3 et WAV.
    if (mciSendStringW((L"open \"" + path + L"\" type mpegvideo alias yue2").c_str(), nullptr, 0, nullptr) != 0) {
        return false;
    }
    open_ = true;
    id_   = id;
    mci(L"set yue2 time format milliseconds");
    length_ms_ = _wtoi(mci(L"status yue2 length").c_str());
    set_volume(volume_);
    return true;
}

void Player::close() {
    if (open_) {
        mci(L"close yue2");
    }
    open_      = false;
    id_.clear();
    length_ms_ = 0;
}

void Player::play() {
    if (!open_) {
        return;
    }
    if (position() >= length_ms_ - 50) {
        mci(L"seek yue2 to start");
    }
    mci(L"play yue2");
}

void Player::pause() {
    if (open_) {
        mci(L"pause yue2");
    }
}

void Player::seek(int ms) {
    if (!open_) {
        return;
    }
    bool was_playing = playing();
    mci(L"seek yue2 to " + std::to_wstring(ms));
    if (was_playing) {
        mci(L"play yue2");
    }
}

bool Player::playing() const {
    return open_ && mci(L"status yue2 mode") == L"playing";
}

bool Player::stopped() const {
    return open_ && mci(L"status yue2 mode") == L"stopped";
}

int Player::position() const {
    return open_ ? _wtoi(mci(L"status yue2 position").c_str()) : 0;
}

void Player::set_volume(int percent) {
    volume_ = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    if (open_) {
        mci(L"setaudio yue2 volume to " + std::to_wstring(volume_ * 10));  // échelle MCI : 0 à 1000
    }
}

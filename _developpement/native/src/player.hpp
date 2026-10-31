// Lecteur audio minimal via MCI (winmm) : aucun moteur audio embarqué.
#pragma once

#include <string>

class Player {
  public:
    ~Player() { close(); }

    bool        open(const std::string & id, const std::wstring & path);
    void        close();
    void        play();
    void        pause();
    void        seek(int ms);
    void        set_volume(int percent);  // 0 à 100
    bool        playing() const;
    bool        stopped() const;  // lecture terminée (ou jamais lancée)
    int         position() const;
    int         length() const { return length_ms_; }
    const std::string & id() const { return id_; }

  private:
    std::string id_;
    int         length_ms_ = 0;
    bool        open_      = false;
    int         volume_    = 80;
};

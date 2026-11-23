# 1.1.017 : essais d'interface : fakejob=3 (travail en pause possible), videodlg=Groupe|Album (fenetre « Creer des videos »)
import io, sys
p = r"C:\Users\deher\Downloads\dev\music\_developpement\native\src\app_ui.cpp"
t = io.open(p, encoding="utf-8", newline="").read().replace("\r\n", "\n")
a = '                auto fj = new_job("Essai du point lumineux");\n'
assert t.count(a) == 1
t = t.replace(a, a + '                if (atoi(val.c_str()) == 3) fj->pausable = true;  // 1.1.017 : bouton Pause\n')
b = '            else if (key == "groupstyle")'
assert t.count(b) == 1
t = t.replace(b, '''            else if (key == "videodlg") {  // 1.1.017 : fenetre « Creer des videos » de l'album « Groupe|Album »
                const size_t bar = val.find('|');
                if (bar != std::string::npos) {
                    const std::string k = album_key(val.substr(0, bar), val.substr(bar + 1));
                    open_video_dialog(album_songs(studio->snapshot(), k, false), val.substr(bar + 1), k);
                }
            }
''' + b)
io.open(p, "w", encoding="utf-8", newline="").write(t.replace("\n", "\r\n"))
print("OK")

// Petites fonctions communes (voir util.hpp).
#include "util.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>

std::string trim(const std::string & s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (char & c : s) c = (char) tolower((unsigned char) c);
    return s;
}

std::string read_text(const std::filesystem::path & file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

void write_text(const std::filesystem::path & file, const std::string & text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string url_encode(const std::string & s) {
    std::string out;
    char        buf[4];
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char) c;
        else {
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

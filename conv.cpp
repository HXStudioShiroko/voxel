#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>

using namespace std;

static int hex_ch_to_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static char val_to_hex_ch(int v)
{
    if (v < 10) return static_cast<char>('0' + v);
    return static_cast<char>('A' + (v - 10));
}

static string process_byte(uint8_t b)
{
    char hex_buf[16];
    snprintf(hex_buf, sizeof(hex_buf), "%02X", b);
    char c0 = hex_buf[0];
    char c1 = hex_buf[1];

    auto char2ascii_hex = [](char ch) -> string {
        uint8_t v = static_cast<uint8_t>(ch);
        char tmp[8];
        snprintf(tmp, sizeof(tmp), "%02X", v);
        return string(tmp);
    };
    string s4 = char2ascii_hex(c0) + char2ascii_hex(c1);
    reverse(s4.begin(), s4.end());

    int d0 = hex_ch_to_val(s4[0]);
    int d1 = hex_ch_to_val(s4[1]);
    int d2 = hex_ch_to_val(s4[2]);
    int d3 = hex_ch_to_val(s4[3]);

    int o0 = d0 + d1;
    int o1 = d1 + d2;
    int o2 = d2 + d3;
    char o3 = s4[3];

    string out;
    out += val_to_hex_ch(o0);
    out += val_to_hex_ch(o1);
    out += val_to_hex_ch(o2);
    out += o3;
    return out;
}

static bool transform(const string &plain,
                      string &out_hex1,
                      string &out_step2,
                      string &out_rev,
                      string &out_final)
{
    vector<uint8_t> bytes;
    for (unsigned char ch : plain)
    {
        bytes.push_back(static_cast<uint8_t>(ch));
    }

    vector<string> h1_list, s2_list, rev_list, res_list;
    for (uint8_t b : bytes)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02X", b);
        h1_list.push_back(buf);

        char c0 = buf[0];
        char c1 = buf[1];
        auto c2h = [](char ch) {
            char t[8];
            snprintf(t, sizeof(t), "%02X", static_cast<uint8_t>(ch));
            return string(t);
        };
        string s4 = c2h(c0) + c2h(c1);
        s2_list.push_back(s4);

        string s4rev = s4;
        reverse(s4rev.begin(), s4rev.end());
        rev_list.push_back(s4rev);

        res_list.push_back(process_byte(b));
    }

    auto join = [](const vector<string> &v) {
        string r;
        for (auto &s : v)
        {
            if (!r.empty()) r += " ";
            r += s;
        }
        return r;
    };
    out_hex1 = join(h1_list);
    out_step2 = join(s2_list);
    out_rev = join(rev_list);
    out_final = join(res_list);
    return true;
}

int main()
{
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    cout << "=== Converter (aarch64, hex add, no length limit) ===\n";
    string line;
    while (true)
    {
        cout << "> ";
        if (!getline(cin, line))
            break;
        if (line == "exit")
        {
            cout << "bye\n";
            break;
        }
        string h1, s2, rev, fin;
        transform(line, h1, s2, rev, fin);
        cout << "[1] 字节HEX    : " << h1 << "\n";
        cout << "[2] 字符ASCII  : " << s2 << "\n";
        cout << "[3] 4位倒置    : " << rev << "\n";
        cout << "[4] 最终结果   : " << fin << "\n";
    }
    return 0;
}

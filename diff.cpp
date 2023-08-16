#include <iostream>
#include <fstream>
#include <array>
#include <vector>
#include <deque>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <cassert>
#include <ctime>
#include <unistd.h>
#include <sys/stat.h>

namespace myers_diff {

enum EditInstructionType {
    Delete,
    Add,
    Change,
    Nop
};

struct EditInstruction {
    EditInstructionType type;
    int orig_start;
    int orig_length;
    int new_start;
    int new_length;
};

// reference: http://www.xmailserver.org/diff2.pdf
template <typename Seq>
class DiffSolver {
    public:
    DiffSolver() = default;
    auto shortest_edit_script(const Seq& a, const Seq& b) {
        int n = a.size();
        int m = b.size();
        find_shortest_path(a,b);
        reconstruct_trace(n,m);
        return build_edit_script();
    }
    private:
    void find_shortest_path(const Seq& a, const Seq& b) {
        // search limit for edit distance
        constexpr int max_diff = 10000;
        int n = a.size();
        int m = b.size();
        std::array<int,2*max_diff+1> buf;
        buf[max_diff+1] = 0;
        for (int d = 0; d < max_diff; d++) {
            max_pos_vec v;
            for (int k = -d; k <= d; k += 2) {
                int x, y;
                // select previous furthest reaching path
                if (k == -d || (k != d && buf[max_diff+k-1] < buf[max_diff+k+1])) {
                    x = buf[max_diff+k+1];
                }
                else {
                    x = buf[max_diff+k-1] + 1;
                }
                y = x - k;
                // extend snakes as much as possible
                while (x < n && y < m) {
                    if (a[x] == b[y]) {
                        x++; y++;
                    }
                    else {
                        break;
                    }
                }
                buf[max_diff+k] = x;
                v.push_back(x);
                if (x == n && y == m) {
                    return;
                }
            }
            history_.push_back(v);
        }
    }
    auto reconstruct_trace(int n, int m) {
        using point = std::pair<int,int>;
        std::vector<point> trace;
        trace.push_back(std::make_pair(n,m));
        const int max_n = n > m ? n : m;
        int k = n - m;
        // reconstruct trace backward
        for (int d = history_.size()-1; d >= 0; d--) {
            const auto& v = history_[d];
            assert(v.size() == d+1);
            auto idx = ((k - 1) + d)/2;
            int x1 = idx < 0 ? -(max_n+1) : v[idx];
            int x2 = idx > d ? -(max_n+1) : v[idx+1];
            int x, y;
            if (x1 + 1 > x2) {
                k = k - 1;
                x = x1;
                y = x - k;
            }
            else {
                k = k + 1;
                x = x2;
                y = x - k;
            }
            trace.push_back(std::make_pair(x,y));
        }
        std::reverse(trace.begin(),trace.end());
        trace_ = std::move(trace);
    }
    auto build_edit_script() {
        std::deque<EditInstruction> dq;
        dq.push_back(EditInstruction{EditInstructionType::Nop,0,0,0,0});
        auto [x,y] = trace_[0];
        auto k = x - y;
        for (auto [xn,yn] : trace_) {
            if (x == xn && y == yn) continue; // skip first trace
            auto last_edit = dq.back();
            int kn = xn - yn;
            if (k < kn) {
                EditInstruction edit;
                edit.type = EditInstructionType::Delete;
                edit.orig_start = x;
                edit.orig_length = 1;
                edit.new_start = y;
                edit.new_length = 0;
                if (last_edit.type == EditInstructionType::Delete) {
                    dq.pop_back();
                    edit.orig_start = last_edit.orig_start;
                    edit.orig_length = last_edit.orig_length + 1;
                }
                dq.push_back(edit);
                x++;
            }
            else {
                EditInstruction edit;
                edit.type = EditInstructionType::Add;
                edit.orig_start = x;
                edit.orig_length = 0;
                edit.new_start = y;
                edit.new_length = 1;
                if (last_edit.type == EditInstructionType::Add) {
                    dq.pop_back();
                    edit.new_start = last_edit.new_start;
                    edit.new_length = last_edit.new_length + 1;
                }
                else if (last_edit.type == EditInstructionType::Delete
                         || last_edit.type == EditInstructionType::Change) {
                    dq.pop_back();
                    edit.type = EditInstructionType::Change;
                    edit.orig_start = last_edit.orig_start;
                    edit.orig_length = last_edit.orig_length;
                    edit.new_start = last_edit.new_start;
                    edit.new_length = last_edit.new_length + 1;
                }
                dq.push_back(edit);
                y++;
            }
            if (x != xn) {
                assert(xn - x == yn - y);
                EditInstruction edit;
                edit.type = EditInstructionType::Nop;
                edit.orig_start = x;
                edit.orig_length = xn - x;
                edit.new_start = y;
                edit.new_length = yn - y;
                dq.push_back(edit);
            }
            x = xn; y = yn; k = kn;
        }
        return dq;
    }
    // member valriable
    using max_pos_vec = std::vector<int>;
    std::vector<max_pos_vec> history_;
    using point = std::pair<int,int>;
    std::vector<point> trace_;
};

enum class Color {
    Red,
    Green,
    Cyan,
    Default,
};

static const std::string to_ansi_code(Color c) {
    switch (c) {
        case Color::Red:
            return "\e[31m";
        case Color::Green:
            return "\e[32m";
        case Color::Cyan:
            return "\e[36m";
        case Color::Default:
            return "";
    }
}

void color_print(const std::string& s, Color c) {
    std::string res;
    std::cout << to_ansi_code(c);
    std::cout << s;
    std::cout << "\e[0m";
}

class ModifiedRange {
    public:
    ModifiedRange(int start, int length)
        : range_(start+1, start+length)
    {}
    const std::string to_str() {
        auto [start,end] = range_;
        if (start == end) {
            return std::to_string(start);
        }
        else {
            return std::to_string(start) + "," + std::to_string(end);
        }
    }
    template <typename Seq>
    void print_modifications(const Seq& seq, const std::string& sign, Color color) {
        auto [start,end] = range_;
        for (int i = start-1; i < end; i++) {
            std::string line = sign + seq[i] + std::string("\n");
            color_print(line,color);
        }
    };
    private:
    std::pair<int,int> range_;
};

struct EditSigns {
    std::string no_change = " ";
    std::string deleted = "-";
    std::string inserted = "+";
    std::string sep_changes = "";
};

struct ColorConfig {
    Color no_change = Color::Default;
    Color deleted = Color::Red;
    Color inserted = Color::Green;
    Color unified_header = Color::Cyan;
};

class EditInstructionPrinter {
    public:
    EditInstructionPrinter(EditSigns signs=EditSigns{}, ColorConfig color_config=ColorConfig{})
        : edit_signs_(signs), color_config_(color_config)
    {}
    void print_header(EditInstruction es) {
        std::string header;
        if (es.type == EditInstructionType::Delete) {
            auto range = ModifiedRange(es.orig_start,es.orig_length);
            header = range.to_str() + "d" + std::to_string(es.new_start);
        }
        else if (es.type == EditInstructionType::Add) {
            auto range = ModifiedRange(es.new_start,es.new_length);
            header = std::to_string(es.orig_start) + "a" + range.to_str();
        }
        else if (es.type == EditInstructionType::Change) {
            auto orig_range = ModifiedRange(es.orig_start,es.orig_length);
            auto new_range = ModifiedRange(es.new_start,es.new_length);
            header = orig_range.to_str() + "c" + new_range.to_str();
        }
        std::cout << header << std::endl;
    };
    template <typename Seq>
    void print_modifications(const Seq& a, const Seq& b, EditInstruction es) {
        if (es.type == EditInstructionType::Delete) {
            auto range = ModifiedRange(es.orig_start,es.orig_length);
            range.print_modifications(a,edit_signs_.deleted,get_color(EditInstructionType::Delete));
        }
        else if (es.type == EditInstructionType::Add) {
            auto range = ModifiedRange(es.new_start,es.new_length);
            range.print_modifications(b,edit_signs_.inserted,get_color(EditInstructionType::Add));
        }
        else if (es.type == EditInstructionType::Change) {
            auto orig_range = ModifiedRange(es.orig_start,es.orig_length);
            auto new_range = ModifiedRange(es.new_start,es.new_length);
            orig_range.print_modifications(a,edit_signs_.deleted,get_color(EditInstructionType::Delete));
            std::cout << edit_signs_.sep_changes;
            new_range.print_modifications(b,edit_signs_.inserted,get_color(EditInstructionType::Add));
        }
    }
    private:
    EditSigns edit_signs_;
    ColorConfig color_config_;
    Color get_color(EditInstructionType t) {
        if (t == EditInstructionType::Delete) {
            return color_config_.deleted;
        }
        else if (t == EditInstructionType::Add) {
            return color_config_.inserted;
        }
        else {
            return Color::Default;
        }
    }
};

class Hunk {
    public:
    Hunk(EditInstruction es) {
        edit_scripts_.push_back(es);
        orig_start_ = es.orig_start - 3;
        orig_end_ = es.orig_start + es.orig_length + 3;
        new_start_ = es.new_start - 3;
        new_end_ = es.new_start + es.new_length + 3;
    };
    bool mergeable(EditInstruction es) {
        return es.orig_start <= orig_end_;
    }
    void add_edit_script(EditInstruction es) {
        edit_scripts_.push_back(es);
        orig_end_ = es.orig_start + es.orig_length + 3;
        new_end_ = es.new_start + es.new_length + 3;
    }
    void normalize_range(int n, int m) {
        orig_start_ = std::max(orig_start_,0);
        orig_end_ = std::min(orig_end_,n);
        new_start_ = std::max(new_start_,0);
        new_end_ = std::min(new_end_,m);
    }
    std::string make_header() {
        std::string header;
        header += "@@ ";
        header += "-" + std::to_string(orig_start_ + 1) + "," + std::to_string(orig_end_ - orig_start_);
        header += " ";
        header += "+" + std::to_string(new_start_ + 1) + "," + std::to_string(new_end_ - new_start_);
        header += " @@\n";
        return header;
    }
    template <typename Seq>
    void print_modifications(const Seq& a, const Seq& b) {
        ColorConfig color_config;
        EditInstructionPrinter ep(EditSigns{},color_config);
        normalize_range(a.size(),b.size());
        color_print(make_header(),color_config.unified_header);
        int line = orig_start_;
        for (auto es : edit_scripts_) {
            while (line < es.orig_start) {
                std::cout << " " << a[line] << std::endl;
                line++;
            }
            ep.print_modifications(a,b,es);
            line += es.orig_length;
        }
        while (line < orig_end_) {
            std::cout << " " << a[line] << std::endl;
            line++;
        }
    }
    private:
    std::vector<EditInstruction> edit_scripts_;
    int orig_start_;
    int orig_end_;
    int new_start_;
    int new_end_;
};

class DiffPrinter {
    public:
    DiffPrinter(ColorConfig color_config)
        : color_config_(color_config)
    {}
    public:
    template <typename Seq, typename EditScript>
    auto normal_print(const Seq& a, const Seq& b, const EditScript& edit_script) {
        EditSigns edit_signs;
        edit_signs.deleted = "< ";
        edit_signs.inserted = "> ";
        edit_signs.sep_changes = "---\n";
        EditInstructionPrinter ep(edit_signs);
        for (auto es : edit_script) {
            if (es.type == EditInstructionType::Nop) {
                continue;
            }
            ep.print_header(es);
            ep.print_modifications(a,b,es);
        }
    }
    template <typename Seq, typename EditScript>
    auto unified_print(const Seq& a, const Seq& b, const EditScript& edit_script) {
        std::vector<Hunk> hunks;
        for (auto es : edit_script) {
            if (es.type == EditInstructionType::Nop) {
                continue;
            }
            if (!hunks.empty()) {
                auto& hunk = hunks.back();
                if (hunk.mergeable(es)) {
                    hunk.add_edit_script(es);
                }
                else {
                    hunks.emplace_back(es);
                }
            }
            else {
                hunks.emplace_back(es);
            }
        }
        for (auto hunk : hunks) {
            hunk.print_modifications(a,b);
        }
    }
    private:
    ColorConfig color_config_;
};

auto read_lines(const std::string& file) {
    std::ifstream ifs(file);
    if (!ifs) {
        std::cerr << "Cannot open " << file << std::endl;
        exit(1);
    }
    std::vector<std::string> lines;
    std::string s;
    while (std::getline(ifs,s)) {
        lines.push_back(s);
    }
    return lines;
}

void get_timestamp(const std::string file, char* buf, std::size_t size) {
    struct stat st;
    if (stat(file.c_str(),&st) < 0) {
        std::cout << strerror(errno) << std::endl;
        exit(1);
    }
    strftime(buf,size,"%Y-%m-%e %H:%M:%S",localtime(&st.st_mtime));
}

void print_unified_header(const std::string& file_1, const std::string& file_2) {
    constexpr std::size_t bufsize = 256;
    char buf1[bufsize];
    char buf2[bufsize];
    get_timestamp(file_1,buf1,bufsize);
    get_timestamp(file_2,buf2,bufsize);
    std::cout << "--- " << file_1 << "\t" << buf1 << std::endl;
    std::cout << "+++ " << file_2 << "\t" << buf2 << std::endl;
}

void print_diff(const std::string& file_1, const std::string& file_2, bool unified) {
    using value_type = std::string;
    using seq_type = std::vector<value_type>;
    seq_type a = read_lines(file_1);
    seq_type b = read_lines(file_2);

    DiffSolver<seq_type> ds;
    auto edit_script = ds.shortest_edit_script(a,b);

    ColorConfig color_config;
    DiffPrinter diff_printer(color_config);
    if (unified) {
        print_unified_header(file_1,file_2);
        diff_printer.unified_print(a,b,edit_script);
    }
    else {
        diff_printer.normal_print(a,b,edit_script);
    }
}

} // namespace myers_diff

int main(int argc, char **argv) {
    const char *optstring = "u";
    int c;
    bool unified = false;
    while ((c = getopt(argc,argv,optstring)) != -1) {
        if (c == 'u') {
            unified = true;
        }
    }
    if (argc - optind != 2) {
        std::cerr << "usage: " << argv[0] << " [-u] <file1> <file2>" << std::endl;
        exit(1);
    }
    std::string file_1(argv[optind]);
    std::string file_2(argv[optind+1]);
    myers_diff::print_diff(file_1,file_2,unified);
}

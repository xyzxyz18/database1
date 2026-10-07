// ============================================================================
//  实验一  基于文件系统的“商城库存管理”应用系统
// ----------------------------------------------------------------------------
//  存储方案：不使用任何数据库系统，只使用操作系统提供的“文件系统”保存数据。
//      data/products.txt   商品目录（主数据，可增删改）
//      data/records.txt    进销记录（流水数据，只追加、不删除）
//
//  数据之间的关联方式：
//      进销记录通过 “商品编号” 关联到商品目录中的商品；
//      同时把 “商品名称 / 单价” 冗余保存在记录中，这样商品被删除以后，
//      它的历史进货、销售记录仍然可以被完整地读出和统计。
//
//  编译：g++ -std=c++17 -O2 -Wall src/inventory.cpp -o inventory
//  运行：./inventory            进入交互菜单
//        ./inventory --init     重新生成一套演示数据集
// ============================================================================

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// 常量与数据结构
// ---------------------------------------------------------------------------
const string DATA_DIR     = "data";
const string PRODUCT_FILE = DATA_DIR + "/products.txt";
const string RECORD_FILE  = DATA_DIR + "/records.txt";
const char   SEP          = '|';       // 字段分隔符

// 商品（商品目录中的一条记录）
struct Product {
    string id;          // 商品编号
    string name;        // 商品名称
    string category;    // 商品类别
    double price = 0;   // 单价（元）
    long long stock = 0;// 库存量
};

// 进销记录（一次进货或销售操作，一条流水）
struct Record {
    long long recId = 0;    // 记录号
    string itemId;          // 商品编号  -> 关联到商品目录
    string itemName;        // 商品名称（冗余保存，商品删除后仍可读）
    char   type = 'P';      // 操作类型：'P' 进货 / 'S' 销售
    string opUser;          // 操作人
    string opTime;          // 操作时间 YYYY-MM-DD HH:MM:SS
    long long qty = 0;      // 操作数量
    double unitPrice = 0;   // 操作时单价（冗余保存）
};

// ---------------------------------------------------------------------------
// 基础工具：字符串处理、中文宽度对齐
// ---------------------------------------------------------------------------
string trim(const string& s) {
    size_t b = 0, e = s.size();
    while (b < e && isspace((unsigned char)s[b])) ++b;
    while (e > b && isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}

vector<string> splitBy(const string& line, char sep) {
    vector<string> out;
    string cur;
    for (char c : line) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

// 一个 UTF-8 字符占多少字节
size_t utf8Bytes(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x06) return 2;
    if ((c >> 4) == 0x0E) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

// 字符串在终端里显示的宽度（汉字按 2 个字符宽度计算），用于表格对齐
size_t displayWidth(const string& s) {
    size_t w = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        size_t n = utf8Bytes(c);
        if (n == 1) { w += 1; }
        else if (n == 3) {
            unsigned int cp = ((c & 0x0F) << 12) |
                              (((unsigned char)s[i + 1] & 0x3F) << 6) |
                              ((unsigned char)s[i + 2] & 0x3F);
            bool wide = (cp >= 0x1100 && cp <= 0x115F) ||
                        (cp >= 0x2E80 && cp <= 0xA4CF) ||
                        (cp >= 0xAC00 && cp <= 0xD7A3) ||
                        (cp >= 0xF900 && cp <= 0xFAFF) ||
                        (cp >= 0xFE30 && cp <= 0xFE6F) ||
                        (cp >= 0xFF00 && cp <= 0xFF60) ||
                        (cp >= 0xFFE0 && cp <= 0xFFE6);
            w += wide ? 2 : 1;
        } else { w += 2; }
        i += n;
    }
    return w;
}

string pad(const string& s, size_t width) {
    size_t w = displayWidth(s);
    return (w >= width) ? s : s + string(width - w, ' ');
}

void printSep(const vector<size_t>& widths) {
    cout << "+";
    for (size_t w : widths) cout << string(w + 2, '-') << "+";
    cout << "\n";
}

void printRow(const vector<string>& cells, const vector<size_t>& widths) {
    cout << "|";
    for (size_t i = 0; i < cells.size(); ++i)
        cout << " " << pad(cells[i], widths[i]) << " |";
    cout << "\n";
}

void printTableHead(const vector<string>& head, const vector<size_t>& widths) {
    printSep(widths);
    printRow(head, widths);
    printSep(widths);
}

// ---------------------------------------------------------------------------
// 日期时间：格式为 YYYY-MM-DD HH:MM:SS，定长字符串，可直接用于比较/排序
// ---------------------------------------------------------------------------
bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

int daysInMonth(int y, int m) {
    static const int dm[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && isLeap(y)) return 29;
    return dm[m];
}

string makeTime(int y, int mo, int d, int h, int mi, int s) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             y, mo, d, h, mi, s);
    return string(buf);
}

// 严格校验并解析时间字符串（检查年月日时分秒的取值范围）
bool parseDateTime(const string& s, string& out) {
    if (s.size() != 19) return false;
    if (s[4] != '-' || s[7] != '-' || s[10] != ' ' ||
        s[13] != ':' || s[16] != ':') return false;
    const int digits[14] = {0,1,2,3,5,6,8,9,11,12,14,15,17,18};
    for (int i : digits) if (!isdigit((unsigned char)s[i])) return false;

    int y  = stoi(s.substr(0, 4)),  mo = stoi(s.substr(5, 2));
    int d  = stoi(s.substr(8, 2)),  h  = stoi(s.substr(11, 2));
    int mi = stoi(s.substr(14, 2)), se = stoi(s.substr(17, 2));

    if (y < 1970 || y > 2100) return false;
    if (mo < 1 || mo > 12) return false;
    if (d  < 1 || d  > daysInMonth(y, mo)) return false;
    if (h  < 0 || h  > 23) return false;
    if (mi < 0 || mi > 59) return false;
    if (se < 0 || se > 59) return false;
    out = s;
    return true;
}

string nowString() {
    time_t t = time(nullptr);
    tm* lt = localtime(&t);
    return makeTime(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                    lt->tm_hour, lt->tm_min, lt->tm_sec);
}

// ---------------------------------------------------------------------------
// 文件读写：商品目录（整体读入内存 -> 修改 -> 整体写回）
//           进销记录（只追加）
// ---------------------------------------------------------------------------
void ensureDataDir() {
    if (!fs::exists(DATA_DIR)) fs::create_directories(DATA_DIR);
}

vector<Product> loadProducts() {
    vector<Product> list;
    ifstream in(PRODUCT_FILE);
    if (!in) return list;
    string line;
    while (getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;
        vector<string> f = splitBy(line, SEP);
        if (f.size() < 5) continue;
        Product p;
        p.id = f[0]; p.name = f[1]; p.category = f[2];
        try { p.price = stod(f[3]); p.stock = stoll(f[4]); }
        catch (...) { continue; }          // 跳过损坏的行
        list.push_back(p);
    }
    return list;
}

void saveProducts(const vector<Product>& list) {
    ensureDataDir();
    ofstream out(PRODUCT_FILE, ios::trunc);
    for (const Product& p : list) {
        out << p.id << SEP << p.name << SEP << p.category << SEP
            << fixed << setprecision(2) << p.price << SEP << p.stock << "\n";
    }
}

vector<Record> loadRecords() {
    vector<Record> list;
    ifstream in(RECORD_FILE);
    if (!in) return list;
    string line;
    while (getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;
        vector<string> f = splitBy(line, SEP);
        if (f.size() < 8) continue;
        Record r;
        try {
            r.recId     = stoll(f[0]);
            r.itemId    = f[1];
            r.itemName  = f[2];
            r.type      = f[3].empty() ? 'P' : f[3][0];
            r.opUser    = f[4];
            r.opTime    = f[5];
            r.qty       = stoll(f[6]);
            r.unitPrice = stod(f[7]);
        } catch (...) { continue; }
        list.push_back(r);
    }
    return list;
}

void appendRecord(const Record& r) {
    ensureDataDir();
    ofstream out(RECORD_FILE, ios::app);
    out << r.recId << SEP << r.itemId << SEP << r.itemName << SEP << r.type
        << SEP << r.opUser << SEP << r.opTime << SEP << r.qty << SEP
        << fixed << setprecision(2) << r.unitPrice << "\n";
}

long long nextRecordId() {
    long long mx = 1000;
    for (const Record& r : loadRecords()) mx = max(mx, r.recId);
    return mx + 1;
}

// ---------------------------------------------------------------------------
// 输入工具（带数据合法性检查）
// ---------------------------------------------------------------------------
string readLine(const string& prompt) {
    cout << prompt;
    cout.flush();
    string s;
    if (!getline(cin, s)) { cout << "\n"; exit(0); }   // 输入流结束
    return s;
}

// 不含分隔符且非空，保证写入文件后仍能正确拆分
string readField(const string& prompt) {
    while (true) {
        string s = trim(readLine(prompt));
        if (s.empty()) { cout << "  [错误] 内容不能为空，请重新输入。\n"; continue; }
        if (s.find(SEP) != string::npos) {
            cout << "  [错误] 内容不能包含字符 '" << SEP << "'，请重新输入。\n";
            continue;
        }
        return s;
    }
}

long long readQuantity(const string& prompt) {
    while (true) {
        string s = trim(readLine(prompt));
        if (s.empty()) { cout << "  [错误] 数量不能为空，请重新输入。\n"; continue; }
        bool digits = true;
        for (char c : s) if (!isdigit((unsigned char)c)) digits = false;
        if (!digits) { cout << "  [错误] 数量必须是正整数，请重新输入。\n"; continue; }
        try {
            long long v = stoll(s);
            if (v <= 0)      { cout << "  [错误] 数量必须大于 0。\n"; continue; }
            if (v > 1000000) { cout << "  [错误] 数量过大（上限 1000000）。\n"; continue; }
            return v;
        } catch (...) { cout << "  [错误] 数量超出范围，请重新输入。\n"; }
    }
}

double readPrice(const string& prompt) {
    while (true) {
        string s = trim(readLine(prompt));
        if (s.empty()) { cout << "  [错误] 单价不能为空。\n"; continue; }
        try {
            size_t used = 0;
            double v = stod(s, &used);
            if (used != s.size()) { cout << "  [错误] 单价格式不正确。\n"; continue; }
            if (v <= 0)  { cout << "  [错误] 单价必须大于 0。\n"; continue; }
            if (v > 1e7) { cout << "  [错误] 单价过大。\n"; continue; }
            return v;
        } catch (...) { cout << "  [错误] 单价格式不正确。\n"; }
    }
}

// 操作时间：回车表示取当前系统时间
string readOpTime(const string& prompt) {
    while (true) {
        string s = trim(readLine(prompt));
        if (s.empty()) return nowString();
        string out;
        if (parseDateTime(s, out)) return out;
        cout << "  [错误] 时间格式不正确，应形如 2026-09-20 14:30:00。\n";
    }
}

// 查询用的时间界限：回车表示“不限制”，也允许只写 YYYY-MM-DD
bool readRangeBound(const string& prompt, bool isEnd, string& out) {
    while (true) {
        string s = trim(readLine(prompt));
        if (s.empty()) { out.clear(); return false; }
        string t = s;
        if (s.size() == 10) t = s + (isEnd ? " 23:59:59" : " 00:00:00");
        if (parseDateTime(t, out)) return true;
        cout << "  [错误] 时间格式不正确，应为 YYYY-MM-DD 或 YYYY-MM-DD HH:MM:SS。\n";
    }
}

bool confirm(const string& prompt) {
    string s = trim(readLine(prompt));
    return !s.empty() && (s[0] == 'y' || s[0] == 'Y' || s == "是");
}

string typeName(char t) { return t == 'P' ? "进货" : "销售"; }

// ---------------------------------------------------------------------------
// 功能 1：商品目录查看（按类别组织展示）
// ---------------------------------------------------------------------------
void showCatalog() {
    vector<Product> list = loadProducts();
    cout << "\n===================== 商品目录（按类别组织） =====================\n";
    if (list.empty()) {
        cout << "  当前商品目录为空（可先执行 “新增商品”）。\n";
        cout << "==================================================================\n";
        return;
    }

    map<string, vector<Product>> groups;
    for (const Product& p : list) groups[p.category].push_back(p);

    long long totalStock = 0;
    for (const Product& p : list) totalStock += p.stock;
    cout << "  共 " << groups.size() << " 个类别，" << list.size()
         << " 种商品，库存合计 " << totalStock << " 件\n";

    vector<size_t> w = {10, 24, 10, 10};
    for (auto& kv : groups) {
        vector<Product> items = kv.second;
        sort(items.begin(), items.end(),
             [](const Product& a, const Product& b) { return a.id < b.id; });

        long long sub = 0;
        for (const Product& p : items) sub += p.stock;
        cout << "\n【类别】" << kv.first << "   （" << items.size()
             << " 种商品，库存合计 " << sub << " 件）\n";
        printTableHead({"商品编号", "商品名称", "单价(元)", "库存量"}, w);
        for (const Product& p : items) {
            ostringstream pr, st;
            pr << fixed << setprecision(2) << p.price;
            st << p.stock;
            printRow({p.id, p.name, pr.str(), st.str()}, w);
        }
        printSep(w);
    }
    cout << "==================================================================\n";
}

// ---------------------------------------------------------------------------
// 功能 2、3：库存管理（进货 / 销售）
// ---------------------------------------------------------------------------
void doTrade(char type) {
    const string op = typeName(type);
    cout << "\n--------------------------- " << op << " ---------------------------\n";

    string id = trim(readLine("请输入" + op + "商品编号: "));
    if (id.empty()) { cout << "  [取消] 未输入商品编号。\n"; return; }

    vector<Product> list = loadProducts();
    int idx = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].id == id) { idx = (int)i; break; }
    if (idx < 0) {
        cout << "  [错误] 商品目录中不存在编号为 " << id << " 的商品。\n";
        return;
    }
    Product& p = list[idx];
    cout << "  商品：" << p.name << "（" << p.id << "）    类别：" << p.category << "\n";
    cout << "  单价：" << fixed << setprecision(2) << p.price
         << " 元    当前库存：" << p.stock << " 件\n";

    long long qty = readQuantity("请输入" + op + "数量: ");
    if (type == 'S' && qty > p.stock) {
        cout << "  [错误] 库存不足！当前库存 " << p.stock << " 件，本次销售 "
             << qty << " 件，操作已取消。\n";
        return;
    }
    string oper = readField("请输入操作人: ");
    string t    = readOpTime("请输入操作时间(YYYY-MM-DD HH:MM:SS，回车=当前时间): ");

    long long before = p.stock;
    p.stock += (type == 'P' ? qty : -qty);
    saveProducts(list);

    Record r;
    r.recId = nextRecordId();
    r.itemId = p.id; r.itemName = p.name; r.type = type;
    r.opUser = oper; r.opTime = t; r.qty = qty; r.unitPrice = p.price;
    appendRecord(r);

    cout << "  [成功] " << op << "完成\n";
    cout << "         记录号 " << r.recId << "    商品 " << p.name
         << "（" << p.id << "）\n";
    cout << "         数量 " << qty << " 件    操作人 " << oper
         << "    时间 " << t << "\n";
    cout << "         库存变化： " << before << " 件  ->  " << p.stock << " 件\n";
    cout << "-------------------------------------------------------------------\n";
}

// ---------------------------------------------------------------------------
// 功能 4：商品删除（保留该商品的进货和销售记录）
// ---------------------------------------------------------------------------
void deleteProduct() {
    cout << "\n-------------------------- 删除商品 --------------------------\n";
    string id = trim(readLine("请输入要删除的商品编号: "));
    if (id.empty()) { cout << "  [取消] 未输入商品编号。\n"; return; }

    vector<Product> list = loadProducts();
    int idx = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].id == id) { idx = (int)i; break; }
    if (idx < 0) {
        cout << "  [错误] 商品目录中不存在编号为 " << id << " 的商品。\n";
        return;
    }

    vector<Record> recs = loadRecords();
    long long cnt = 0, inQty = 0, outQty = 0;
    for (const Record& r : recs)
        if (r.itemId == id) {
            ++cnt;
            if (r.type == 'P') inQty += r.qty; else outQty += r.qty;
        }

    const Product& p = list[idx];
    cout << "  待删除商品：" << p.id << "  " << p.name
         << "  类别：" << p.category << "  库存：" << p.stock << " 件\n";
    cout << "  该商品在进销记录文件中共有 " << cnt << " 条历史记录：\n";
    cout << "  进货 " << inQty << " 件，销售 " << outQty
         << " 件。删除商品后这些记录将被保留。\n";

    if (!confirm("确认删除该商品？(y/N): ")) { cout << "  [取消] 已放弃删除。\n"; return; }

    list.erase(list.begin() + idx);
    saveProducts(list);
    cout << "  [成功] 商品 " << id << " 已从商品目录中删除；\n";
    cout << "         进销记录文件未做任何修改，" << cnt
         << " 条历史记录仍然保留、可查。\n";
    cout << "--------------------------------------------------------------\n";
}

// ---------------------------------------------------------------------------
// 功能 5：按类别浏览商品，并按库存量排序
// ---------------------------------------------------------------------------
void browseByCategory() {
    vector<Product> list = loadProducts();
    cout << "\n------------------- 按类别浏览商品（按库存量排序） ------------------\n";
    if (list.empty()) { cout << "  当前商品目录为空。\n"; return; }

    map<string, vector<Product>> groups;
    for (const Product& p : list) groups[p.category].push_back(p);

    vector<string> cats;
    for (auto& kv : groups) cats.push_back(kv.first);

    cout << "  可选类别：\n";
    for (size_t i = 0; i < cats.size(); ++i)
        cout << "    " << (i + 1) << ". " << cats[i]
             << "（" << groups[cats[i]].size() << " 种商品）\n";
    cout << "    0. 全部类别\n";

    string in = trim(readLine("请选择类别编号或直接输入类别名称: "));
    string target;
    if (in == "0" || in.empty()) {
        target = "";
    } else {
        bool numeric = true;
        for (char c : in) if (!isdigit((unsigned char)c)) numeric = false;
        if (numeric) {
            int k = stoi(in);
            if (k < 1 || k > (int)cats.size()) {
                cout << "  [错误] 类别编号超出范围。\n"; return;
            }
            target = cats[k - 1];
        } else {
            target = in;
            if (groups.find(target) == groups.end()) {
                cout << "  [错误] 不存在类别 “" << target << "”。\n"; return;
            }
        }
    }

    vector<Product> items;
    for (const Product& p : list)
        if (target.empty() || p.category == target) items.push_back(p);

    // 按库存量从多到少排序；库存相同时按商品编号排序，保证输出稳定
    sort(items.begin(), items.end(), [](const Product& a, const Product& b) {
        if (a.stock != b.stock) return a.stock > b.stock;
        return a.id < b.id;
    });

    cout << "\n  >>> 浏览范围：" << (target.empty() ? string("全部类别") : target)
         << "，共 " << items.size() << " 种商品（按库存量降序）\n";
    vector<size_t> w = {6, 10, 24, 12, 10, 10};
    printTableHead({"排名", "商品编号", "商品名称", "类别", "单价(元)", "库存量"}, w);
    long long total = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        const Product& p = items[i];
        ostringstream pr, st;
        pr << fixed << setprecision(2) << p.price;
        st << p.stock;
        printRow({to_string(i + 1), p.id, p.name, p.category, pr.str(), st.str()}, w);
        total += p.stock;
    }
    printSep(w);
    cout << "  库存合计：" << total << " 件\n";
    cout << "---------------------------------------------------------------------\n";
}

// ---------------------------------------------------------------------------
// 功能 6：进销记录查询（按商品 + 时间范围 / 操作人）
// ---------------------------------------------------------------------------
void queryRecords() {
    cout << "\n------------------------ 进销记录查询 ------------------------\n";
    string id = trim(readLine("请输入要查询的商品编号: "));
    if (id.empty()) { cout << "  [取消] 未输入商品编号。\n"; return; }

    vector<Record> all = loadRecords();
    bool inCatalog = false;
    for (const Product& p : loadProducts()) if (p.id == id) inCatalog = true;

    string start, end, user;
    cout << "  可按时间范围和操作人进一步筛选，直接回车表示该项不限制。\n";
    bool hasStart = readRangeBound("  起始时间(YYYY-MM-DD [HH:MM:SS]): ", false, start);
    bool hasEnd   = readRangeBound("  结束时间(YYYY-MM-DD [HH:MM:SS]): ", true,  end);
    if (hasStart && hasEnd && start > end) {
        cout << "  [错误] 起始时间晚于结束时间，查询终止。\n"; return;
    }
    user = trim(readLine("  操作人(回车=不限制): "));

    vector<Record> hit;
    string name;
    for (const Record& r : all) {
        if (r.itemId != id) continue;
        if (hasStart && r.opTime < start) continue;
        if (hasEnd   && r.opTime > end)   continue;
        if (!user.empty() && r.opUser != user) continue;
        hit.push_back(r);
        name = r.itemName;
    }

    cout << "\n  商品编号：" << id;
    if (!name.empty()) cout << "    商品名称：" << name;
    if (!inCatalog)
        cout << "\n  说明：该商品已从商品目录中删除，其历史进销记录仍然保留，可正常查询。";
    cout << "\n  查询条件：时间 " << (hasStart ? start : string("不限"))
         << " ~ " << (hasEnd ? end : string("不限"))
         << "，操作人 " << (user.empty() ? string("不限") : user) << "\n";

    if (hit.empty()) { cout << "  没有符合条件的进销记录。\n"; return; }

    sort(hit.begin(), hit.end(), [](const Record& a, const Record& b) {
        if (a.opTime != b.opTime) return a.opTime < b.opTime;
        return a.recId < b.recId;
    });

    vector<size_t> w = {8, 6, 10, 20, 8, 12};
    printTableHead({"记录号", "类型", "操作人", "操作时间", "数量", "金额(元)"}, w);
    long long inQty = 0, outQty = 0;
    double amount = 0;
    for (const Record& r : hit) {
        ostringstream amt;
        amt << fixed << setprecision(2) << r.qty * r.unitPrice;
        printRow({to_string(r.recId), typeName(r.type), r.opUser, r.opTime,
                  to_string(r.qty), amt.str()}, w);
        if (r.type == 'P') inQty += r.qty;
        else { outQty += r.qty; amount += r.qty * r.unitPrice; }
    }
    printSep(w);
    cout << "  命中 " << hit.size() << " 条记录：进货合计 " << inQty
         << " 件，销售合计 " << outQty << " 件，销售金额合计 "
         << fixed << setprecision(2) << amount << " 元\n";
    cout << "-------------------------------------------------------------\n";
}

// ---------------------------------------------------------------------------
// 功能 7：销量汇总（某时间范围内 全部商品 或 某类商品 的总销量）
// ---------------------------------------------------------------------------
void salesSummary() {
    cout << "\n-------------------------- 销量汇总 --------------------------\n";
    string start, end;
    cout << "  请指定统计时间范围（直接回车表示不限制）。\n";
    bool hasStart = readRangeBound("  起始时间(YYYY-MM-DD [HH:MM:SS]): ", false, start);
    bool hasEnd   = readRangeBound("  结束时间(YYYY-MM-DD [HH:MM:SS]): ", true,  end);
    if (hasStart && hasEnd && start > end) {
        cout << "  [错误] 起始时间晚于结束时间，统计终止。\n"; return;
    }

    vector<Product> products = loadProducts();
    vector<Record>  records  = loadRecords();

    // 可选类别：商品目录中出现的类别 + 记录中仍能对应到商品的类别
    set<string> cats;
    for (const Product& p : products) cats.insert(p.category);
    for (const Record& r : records)
        for (const Product& p : products)
            if (p.id == r.itemId) { cats.insert(p.category); break; }

    cout << "  统计范围可选类别：";
    bool first = true;
    for (const string& c : cats) { cout << (first ? "" : "、") << c; first = false; }
    cout << "\n";
    string scope = trim(readLine("  请输入类别名称(回车=全部商品): "));
    if (!scope.empty() && cats.find(scope) == cats.end()) {
        cout << "  [错误] 不存在类别 “" << scope << "”。\n"; return;
    }

    // 统计范围内包含的商品编号（依据商品目录进行归类）
    set<string> idsInScope;
    if (scope.empty()) {
        // 全部商品：记录中的所有商品都参与统计
    } else {
        for (const Product& p : products)
            if (p.category == scope) idsInScope.insert(p.id);
    }

    struct Agg { string id, name, category; long long qty = 0; double amount = 0; };
    map<string, Agg> agg;
    long long totalQty = 0;
    double totalAmount = 0;

    for (const Record& r : records) {
        if (r.type != 'S') continue;                       // 只统计销售记录
        if (hasStart && r.opTime < start) continue;
        if (hasEnd   && r.opTime > end)   continue;
        if (!scope.empty() && idsInScope.find(r.itemId) == idsInScope.end()) continue;
        Agg& a = agg[r.itemId];
        a.id = r.itemId; a.name = r.itemName;
        a.qty += r.qty; a.amount += r.qty * r.unitPrice;
        totalQty += r.qty; totalAmount += r.qty * r.unitPrice;
    }
    for (auto& kv : agg) {
        kv.second.category = "（已删除）";
        for (const Product& p : products)
            if (p.id == kv.first) { kv.second.category = p.category; break; }
    }

    cout << "\n  >>> 统计范围：" << (scope.empty() ? string("全部商品") : scope)
         << "；时间范围：" << (hasStart ? start : string("不限"))
         << " ~ " << (hasEnd ? end : string("不限")) << "\n";
    if (agg.empty()) {
        cout << "  该时间范围内没有任何销售记录，总销量为 0。\n";
        cout << "--------------------------------------------------------------\n";
        return;
    }

    vector<Agg> rows;
    for (auto& kv : agg) rows.push_back(kv.second);
    sort(rows.begin(), rows.end(), [](const Agg& a, const Agg& b) {
        if (a.qty != b.qty) return a.qty > b.qty;
        return a.id < b.id;
    });

    vector<size_t> w = {6, 10, 24, 12, 10, 14};
    printTableHead({"排名", "商品编号", "商品名称", "类别", "销量(件)", "销售额(元)"}, w);
    for (size_t i = 0; i < rows.size(); ++i) {
        ostringstream amt;
        amt << fixed << setprecision(2) << rows[i].amount;
        printRow({to_string(i + 1), rows[i].id, rows[i].name, rows[i].category,
                  to_string(rows[i].qty), amt.str()}, w);
    }
    printSep(w);
    cout << "  统计商品数：" << rows.size() << " 种    总销量：" << totalQty
         << " 件    总销售额：" << fixed << setprecision(2) << totalAmount << " 元\n";
    cout << "--------------------------------------------------------------\n";
}

// ---------------------------------------------------------------------------
// 功能 8：新增商品
// ---------------------------------------------------------------------------
void addProduct() {
    cout << "\n-------------------------- 新增商品 --------------------------\n";
    vector<Product> list = loadProducts();

    string id;
    while (true) {
        id = readField("请输入商品编号: ");
        bool used = false;
        for (const Product& p : list) if (p.id == id) used = true;
        if (used) { cout << "  [错误] 编号 " << id << " 已存在，请更换。\n"; continue; }
        break;
    }
    string name = readField("请输入商品名称: ");
    string cat  = readField("请输入商品类别: ");
    double price = readPrice("请输入商品单价(元): ");
    cout << "  说明：此处录入的是该商品的初始库存量。\n";
    long long stock = readQuantity("请输入初始库存量: ");

    list.push_back(Product{id, name, cat, price, stock});
    saveProducts(list);
    cout << "  [成功] 已新增商品 " << id << "  " << name
         << "（类别：" << cat << "，单价：" << fixed << setprecision(2) << price
         << " 元，库存：" << stock << " 件）。\n";
    cout << "--------------------------------------------------------------\n";
}

// ---------------------------------------------------------------------------
// 演示数据集生成（--init）
// ---------------------------------------------------------------------------
void generateDataset() {
    ensureDataDir();
    struct Seed { const char* id; const char* name; const char* cat; double price; };
    vector<Seed> seeds = {
        {"P1001", "智能手机 A1",     "手机数码", 2999.00},
        {"P1002", "蓝牙耳机 B2",     "手机数码",  399.00},
        {"P1003", "平板电脑 C3",     "手机数码", 1899.00},
        {"P2001", "电饭煲 D1",       "家用电器",  299.00},
        {"P2002", "空气净化器 E2",   "家用电器", 1299.00},
        {"P2003", "电动牙刷 F3",     "家用电器",  259.00},
        {"P3001", "有机牛奶 1L",     "食品饮料",   15.90},
        {"P3002", "精品咖啡豆 500g", "食品饮料",   89.00},
        {"P3003", "坚果礼盒",        "食品饮料",  128.00},
        {"P3004", "矿泉水 550ml",    "食品饮料",    2.00},
        {"P4001", "C++程序设计",     "图书文具",   59.00},
        {"P4002", "数据库系统概论",  "图书文具",   45.00},
        {"P4003", "中性笔套装",      "图书文具",   12.50},
        {"P5001", "运动跑鞋",        "服饰鞋包",  359.00},
        {"P5002", "双肩背包",        "服饰鞋包",  199.00},
    };
    const vector<string> users = {"张三", "李四", "王五", "赵六"};

    // 统计区间：2026-03-01 ~ 2026-09-20
    tm tmv{};
    tmv.tm_year = 2026 - 1900; tmv.tm_mon = 2; tmv.tm_mday = 1; tmv.tm_isdst = -1;
    time_t base = mktime(&tmv);
    tm endv{};
    endv.tm_year = 2026 - 1900; endv.tm_mon = 8; endv.tm_mday = 20;
    endv.tm_hour = 23; endv.tm_min = 59; endv.tm_sec = 59; endv.tm_isdst = -1;
    time_t endt = mktime(&endv);
    long long span = (long long)(endt - base);

    mt19937 rng(20260924u);
    auto rnd = [&](long long n) -> long long {
        return (long long)(rng() % (unsigned long long)n);
    };
    auto randTime = [&]() -> string {
        time_t tt = base + (time_t)rnd(span);
        tm* lt = localtime(&tt);
        return makeTime(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                        lt->tm_hour, lt->tm_min, lt->tm_sec);
    };

    vector<Product> products;
    vector<Record> records;

    for (const Seed& s : seeds) {
        long long stock = 0;
        long long np = 3 + rnd(4);          // 3~6 次进货
        long long ns = 5 + rnd(6);          // 5~10 次销售
        for (long long i = 0; i < np; ++i) {
            Record r;
            r.itemId = s.id; r.itemName = s.name; r.type = 'P';
            r.opUser = users[rnd((long long)users.size())];
            r.qty = 20 + rnd(61);           // 进货 20~80 件
            r.unitPrice = s.price;
            r.opTime = randTime();
            stock += r.qty;
            records.push_back(r);
        }
        for (long long i = 0; i < ns && stock > 0; ++i) {
            Record r;
            r.itemId = s.id; r.itemName = s.name; r.type = 'S';
            r.opUser = users[rnd((long long)users.size())];
            long long cap = min<long long>(30, stock);
            r.qty = 1 + rnd(cap);           // 销售数量不超过当前库存
            r.unitPrice = s.price;
            r.opTime = randTime();
            stock -= r.qty;
            records.push_back(r);
        }
        products.push_back(Product{s.id, s.name, s.cat, s.price, stock});
    }

    // 按时间排序后统一编号，使记录文件呈现为一条时间有序的流水
    sort(records.begin(), records.end(), [](const Record& a, const Record& b) {
        if (a.opTime != b.opTime) return a.opTime < b.opTime;
        return a.itemId < b.itemId;
    });
    long long rid = 1001;
    for (Record& r : records) r.recId = rid++;

    saveProducts(products);
    {
        ofstream out(RECORD_FILE, ios::trunc);
        for (const Record& r : records) {
            out << r.recId << SEP << r.itemId << SEP << r.itemName << SEP << r.type
                << SEP << r.opUser << SEP << r.opTime << SEP << r.qty << SEP
                << fixed << setprecision(2) << r.unitPrice << "\n";
        }
    }

    cout << "[init] 演示数据集生成完毕：\n";
    cout << "       商品文件 " << PRODUCT_FILE << "：" << products.size() << " 种商品\n";
    cout << "       记录文件 " << RECORD_FILE  << "：" << records.size() << " 条进销记录\n";
    cout << "       时间范围 2026-03-01 ~ 2026-09-20，操作人：张三/李四/王五/赵六\n";
}

// ---------------------------------------------------------------------------
// 主菜单
// ---------------------------------------------------------------------------
void showMenu() {
    cout << "\n";
    cout << "================= 商城库存管理系统（基于文件系统） =================\n";
    cout << "   1. 商品目录查看（按类别组织）\n";
    cout << "   2. 进货（增加库存，写入进销记录）\n";
    cout << "   3. 销售（减少库存，写入进销记录）\n";
    cout << "   4. 删除商品（保留该商品的进销记录）\n";
    cout << "   5. 按类别浏览商品（按库存量排序）\n";
    cout << "   6. 进销记录查询（按时间范围 / 操作人）\n";
    cout << "   7. 销量汇总（全部商品或某类商品）\n";
    cout << "   8. 新增商品\n";
    cout << "   0. 退出系统\n";
    cout << "====================================================================\n";
}

int main(int argc, char** argv) {
    if (argc > 1 && string(argv[1]) == "--init") { generateDataset(); return 0; }
    ensureDataDir();

    cout << "欢迎使用“商城库存管理系统”。\n";
    cout << "数据文件：" << PRODUCT_FILE << "（商品目录）、"
         << RECORD_FILE << "（进销记录）\n";

    while (true) {
        showMenu();
        string s = trim(readLine("请选择功能编号: "));
        if (s == "0" || s == "q" || s == "Q") { cout << "已退出系统，再见。\n"; break; }
        if (s == "1") showCatalog();
        else if (s == "2") doTrade('P');
        else if (s == "3") doTrade('S');
        else if (s == "4") deleteProduct();
        else if (s == "5") browseByCategory();
        else if (s == "6") queryRecords();
        else if (s == "7") salesSummary();
        else if (s == "8") addProduct();
        else cout << "  [错误] 无效的功能编号，请输入 0~8。\n";
    }
    return 0;
}

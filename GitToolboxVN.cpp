#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <sstream>
#include <cwctype>
#include <set>

struct CommandDef {
    std::wstring category;
    std::wstring title;
    std::wstring args;
    std::wstring description;
    std::wstring p1Label;
    std::wstring p1Hint;
    std::wstring p2Label;
    std::wstring p2Hint;
    std::wstring p3Label;
    std::wstring p3Hint;
    int risk; // 0=read only, 1=normal write, 2=destructive/dangerous
};

struct OptionItem {
    std::wstring value;
    std::wstring display;
};

struct CacheResult {
    std::wstring repo;
    std::wstring currentBranch;
    std::vector<OptionItem> localBranches;
    std::vector<OptionItem> branches;
    std::vector<OptionItem> originBranches;
    std::vector<OptionItem> refs;
    std::vector<OptionItem> remotes;
    std::wstring originDefaultBranch;
    std::vector<OptionItem> commits;
    std::vector<OptionItem> tags;
    std::vector<OptionItem> stashes;
    std::vector<OptionItem> changedFiles;
    std::wstring error;
};

static HINSTANCE g_hInst{};
static HWND g_main{}, g_repo{}, g_search{}, g_category{}, g_list{}, g_desc{}, g_preview{}, g_p1Label{}, g_p1{}, g_p2Label{}, g_p2{}, g_p3Label{}, g_p3{}, g_run{}, g_output{}, g_status{}, g_refresh{};
static HFONT g_font{}, g_mono{};
static std::vector<CommandDef> g_commands;
static std::vector<int> g_filtered;
static CacheResult g_cache;
static std::vector<OptionItem> g_p1Options, g_p2Options, g_p3Options;
static int g_selected = -1;
static constexpr UINT WM_APP_DONE = WM_APP + 1;
static constexpr UINT WM_APP_CACHE_READY = WM_APP + 2;

static std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c){ return (wchar_t)towlower(c); });
    return s;
}

static std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    if (n) GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}

static void SetText(HWND h, const std::wstring& s) { SetWindowTextW(h, s.c_str()); }

static std::wstring Trim(std::wstring s) {
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}

static std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::wstring line;
    for (wchar_t c : text) {
        if (c == L'\r') continue;
        if (c == L'\n') {
            line = Trim(line);
            if (!line.empty()) lines.push_back(line);
            line.clear();
        } else {
            line.push_back(c);
        }
    }
    line = Trim(line);
    if (!line.empty()) lines.push_back(line);
    return lines;
}

static void AddOptionUnique(std::vector<OptionItem>& items, const std::wstring& value, const std::wstring& display=L"") {
    if (value.empty()) return;
    for (const auto& item : items) if (item.value == value) return;
    items.push_back({value, display.empty() ? value : display});
}

static std::wstring ComboValue(HWND combo, const std::vector<OptionItem>& options) {
    std::wstring text = GetText(combo);
    LRESULT sel = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < (LRESULT)options.size() && text == options[(size_t)sel].display) {
        return options[(size_t)sel].value;
    }
    return text;
}

static std::wstring QuoteArg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    bool need = arg.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!need) return arg;
    std::wstring out = L"\"";
    int slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { slashes++; continue; }
        if (c == L'\"') {
            out.append(slashes * 2 + 1, L'\\');
            out.push_back(L'\"');
            slashes = 0;
        } else {
            out.append(slashes, L'\\');
            slashes = 0;
            out.push_back(c);
        }
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

static void ReplaceAll(std::wstring& s, const std::wstring& from, const std::wstring& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

static std::wstring BuildArgs(const CommandDef& c, bool validate, bool* ok = nullptr) {
    bool good = true;
    std::wstring a = c.args;
    auto apply = [&](const wchar_t* token, HWND edit, const std::vector<OptionItem>& options, const std::wstring& label) {
        if (label.empty()) return;
        std::wstring v = ComboValue(edit, options);
        if (validate && v.empty()) good = false;
        ReplaceAll(a, token, QuoteArg(v));
    };
    apply(L"{1}", g_p1, g_p1Options, c.p1Label);
    apply(L"{2}", g_p2, g_p2Options, c.p2Label);
    apply(L"{3}", g_p3, g_p3Options, c.p3Label);
    if (ok) *ok = good;
    return a;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (n > 0) {
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }
    n = MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    if (n) MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static void AppendOutput(const std::wstring& text) {
    int len = GetWindowTextLengthW(g_output);
    SendMessageW(g_output, EM_SETSEL, len, len);
    SendMessageW(g_output, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    SendMessageW(g_output, EM_SCROLLCARET, 0, 0);
}

static std::vector<std::wstring> Categories() {
    return {L"Tất cả", L"Thông tin", L"Đồng bộ", L"Branch", L"Stage & Commit", L"Diff & Lịch sử", L"Khôi phục", L"Stash", L"Merge & Rebase", L"Remote", L"Tag", L"Submodule", L"Worktree", L"Cấu hình", L"Bảo trì", L"Nâng cao"};
}

static void Add(std::wstring cat, std::wstring title, std::wstring args, std::wstring desc,
                int risk=0, std::wstring l1=L"", std::wstring h1=L"", std::wstring l2=L"", std::wstring h2=L"", std::wstring l3=L"", std::wstring h3=L"") {
    g_commands.push_back({cat,title,args,desc,l1,h1,l2,h2,l3,h3,risk});
}

static void BuildCatalog() {
    // THÔNG TIN
    Add(L"Thông tin", L"Xem nhánh hiện tại", L"branch --show-current", L"Cho biết working tree hiện đang ở branch nào. Chỉ đọc.");
    Add(L"Thông tin", L"Trạng thái repo", L"status", L"Xem branch, file đã sửa, file stage, untracked và trạng thái tổng quát.");
    Add(L"Thông tin", L"Trạng thái ngắn", L"status --short --branch", L"Phiên bản ngắn gọn của git status, tiện nhìn nhanh.");
    Add(L"Thông tin", L"Xem thư mục gốc repo", L"rev-parse --show-toplevel", L"In đường dẫn thư mục root của repository hiện tại.");
    Add(L"Thông tin", L"Xem commit HEAD", L"rev-parse HEAD", L"In SHA đầy đủ của commit hiện tại.");
    Add(L"Thông tin", L"Xem SHA ngắn HEAD", L"rev-parse --short HEAD", L"In SHA rút gọn của commit hiện tại.");
    Add(L"Thông tin", L"Xem remote", L"remote -v", L"Liệt kê remote và URL fetch/push.");
    Add(L"Thông tin", L"Xem branch local", L"branch", L"Liệt kê branch local.");
    Add(L"Thông tin", L"Xem tất cả branch", L"branch -a", L"Liệt kê branch local và remote-tracking.");
    Add(L"Thông tin", L"Xem branch remote", L"branch -r", L"Chỉ liệt kê remote-tracking branch.");
    Add(L"Thông tin", L"Xem branch + tracking", L"branch -vv", L"Cho biết upstream và commit gần nhất của từng branch local.");
    Add(L"Thông tin", L"Xem tag", L"tag --list", L"Liệt kê tất cả tag.");
    Add(L"Thông tin", L"Xem stash", L"stash list", L"Liệt kê các stash hiện có.");
    Add(L"Thông tin", L"Xem config", L"config --list --show-origin", L"Hiện toàn bộ cấu hình Git kèm nguồn file cấu hình.");
    Add(L"Thông tin", L"Xem file đang được Git theo dõi", L"ls-files", L"Liệt kê file tracked trong index.");
    Add(L"Thông tin", L"Kiểm tra repo hợp lệ", L"rev-parse --is-inside-work-tree", L"Trả true nếu thư mục hiện tại nằm trong working tree Git.");
    Add(L"Thông tin", L"Xem HEAD symbolic", L"symbolic-ref --short HEAD", L"Hiện tên branch mà HEAD đang trỏ tới, nếu không detached.");
    Add(L"Thông tin", L"Xem object database", L"count-objects -vH", L"Xem số lượng object và dung lượng Git object database.");

    // ĐỒNG BỘ
    Add(L"Đồng bộ", L"Fetch remote hiện tại", L"fetch", L"Lấy metadata/commit mới từ remote mà không đổi working tree.",1);
    Add(L"Đồng bộ", L"Fetch tất cả remote", L"fetch --all --prune", L"Fetch mọi remote và dọn remote-tracking ref đã bị xóa.",1);
    Add(L"Đồng bộ", L"Fetch + prune", L"fetch --prune", L"Lấy dữ liệu mới từ upstream/default remote và xóa remote-tracking ref không còn tồn tại. Đây là lệnh git fetch --prune.",1);
    Add(L"Đồng bộ", L"Fetch origin", L"fetch origin", L"Lấy dữ liệu mới từ origin, không merge.",1);
    Add(L"Đồng bộ", L"Pull", L"pull", L"Fetch rồi tích hợp branch upstream vào branch hiện tại.",1);
    Add(L"Đồng bộ", L"Pull --rebase", L"pull --rebase", L"Fetch rồi rebase commit local lên trên upstream thay vì merge.",1);
    Add(L"Đồng bộ", L"Pull chỉ fast-forward", L"pull --ff-only", L"Chỉ pull nếu có thể fast-forward; từ chối nếu cần merge commit.",1);
    Add(L"Đồng bộ", L"Push", L"push", L"Đẩy commit local lên upstream đã cấu hình.",1);
    Add(L"Đồng bộ", L"Push origin branch hiện tại", L"push origin HEAD", L"Đẩy HEAD hiện tại lên origin.",1);
    Add(L"Đồng bộ", L"Push và đặt upstream", L"push -u origin {1}", L"Push branch lên origin và thiết lập upstream cho các lần push/pull sau.",1,L"Tên branch",L"ví dụ: test/direct-webcast-v11");
    Add(L"Đồng bộ", L"So local với origin", L"status -sb", L"Hiện branch ahead/behind upstream nếu đã tracking.");
    Add(L"Đồng bộ", L"Xem commit local chưa push", L"log @{u}..HEAD --oneline", L"Liệt kê commit có ở local nhưng chưa có trên upstream.");
    Add(L"Đồng bộ", L"Xem commit remote chưa pull", L"log HEAD..@{u} --oneline", L"Liệt kê commit upstream có nhưng local chưa có.");
    Add(L"Đồng bộ", L"Prune origin", L"remote prune origin", L"Xóa remote-tracking ref origin không còn tồn tại trên server.",1);

    // BRANCH
    Add(L"Branch", L"Chuyển branch", L"switch {1}", L"Chuyển working tree sang branch đã có.",1,L"Tên branch",L"ví dụ: main");
    Add(L"Branch", L"Tạo và chuyển branch", L"switch -c {1}", L"Tạo branch mới từ HEAD rồi chuyển sang branch đó.",1,L"Tên branch mới",L"ví dụ: feature/test");
    Add(L"Branch", L"Tạo branch không chuyển", L"branch {1}", L"Tạo branch mới tại HEAD nhưng vẫn ở branch hiện tại.",1,L"Tên branch mới",L"feature/test");
    Add(L"Branch", L"Tạo branch từ commit", L"branch {1} {2}", L"Tạo branch mới bắt đầu tại commit/tag/branch chỉ định.",1,L"Tên branch mới",L"rescue",L"Commit/tag/ref",L"abc123 hoặc v1.0");
    Add(L"Branch", L"Chuyển về branch trước", L"switch -", L"Quay lại branch đã checkout/switch trước đó.",1);
    Add(L"Branch", L"Đổi tên branch hiện tại", L"branch -m {1}", L"Đổi tên branch hiện tại.",1,L"Tên mới",L"new-name");
    Add(L"Branch", L"Đổi tên branch bất kỳ", L"branch -m {1} {2}", L"Đổi tên một branch local.",1,L"Tên cũ",L"old",L"Tên mới",L"new");
    Add(L"Branch", L"Xóa branch đã merge", L"branch -d {1}", L"Xóa branch local nếu Git xác nhận branch đã được merge.",2,L"Tên branch",L"feature/test");
    Add(L"Branch", L"Ép xóa branch local", L"branch -D {1}", L"Xóa branch local kể cả chưa merge. Có thể làm mất commit khó tìm lại.",2,L"Tên branch",L"feature/test");
    Add(L"Branch", L"Xóa nhiều branch remote", L"__MULTI_DELETE_ORIGIN_BRANCHES__", L"Chọn nhiều branch trên origin bằng danh sách rồi xóa trong một lệnh. Branch mặc định của origin (thường là main) được bảo vệ và không cho chọn.",2);
    Add(L"Branch", L"Xóa branch remote", L"push origin --delete {1}", L"Xóa một branch trên origin.",2,L"Tên branch remote",L"feature/test");
    Add(L"Branch", L"Đặt upstream", L"branch --set-upstream-to=origin/{1}", L"Đặt branch hiện tại tracking origin/<branch>.",1,L"Tên branch remote",L"main");
    Add(L"Branch", L"Bỏ upstream", L"branch --unset-upstream", L"Gỡ cấu hình upstream của branch hiện tại.",1);
    Add(L"Branch", L"Branch đã merge vào HEAD", L"branch --merged", L"Liệt kê branch đã merge vào HEAD.");
    Add(L"Branch", L"Branch chưa merge vào HEAD", L"branch --no-merged", L"Liệt kê branch chưa merge vào HEAD.");
    Add(L"Branch", L"Xem branch chứa commit", L"branch --contains {1}", L"Tìm branch local có chứa commit chỉ định.",0,L"Commit",L"abc123");

    // STAGE & COMMIT
    Add(L"Stage & Commit", L"Stage tất cả", L"add .", L"Đưa thay đổi trong thư mục hiện tại vào staging area.",1);
    Add(L"Stage & Commit", L"Stage tất cả kể cả xóa", L"add -A", L"Stage thêm/sửa/xóa trên toàn working tree.",1);
    Add(L"Stage & Commit", L"Stage một file", L"add -- {1}", L"Stage một file/path cụ thể.",1,L"Đường dẫn file",L"src/app.cpp");
    Add(L"Stage & Commit", L"Stage tương tác", L"add -p", L"Chọn từng hunk để stage. Lệnh này tương tác trong console nên không lý tưởng trong GUI; output vẫn được hiển thị.",1);
    Add(L"Stage & Commit", L"Unstage một file", L"restore --staged -- {1}", L"Bỏ file khỏi staging nhưng giữ thay đổi trong working tree.",1,L"Đường dẫn file",L"src/app.cpp");
    Add(L"Stage & Commit", L"Unstage tất cả", L"restore --staged .", L"Bỏ toàn bộ thay đổi khỏi staging, không xóa nội dung chỉnh sửa.",1);
    Add(L"Stage & Commit", L"Commit", L"commit -m {1}", L"Tạo commit từ nội dung đã stage với message nhập vào.",1,L"Commit message",L"Fix reconnect logic");
    Add(L"Stage & Commit", L"Commit tracked (-a)", L"commit -am {1}", L"Tự stage file tracked đã sửa/xóa rồi commit; không thêm file mới untracked.",1,L"Commit message",L"Fix bug");
    Add(L"Stage & Commit", L"Amend message + staged", L"commit --amend -m {1}", L"Thay commit cuối bằng commit mới gồm staged changes và message mới. Nếu đã push có thể gây lệch lịch sử.",2,L"Commit message mới",L"Correct message");
    Add(L"Stage & Commit", L"Commit rỗng", L"commit --allow-empty -m {1}", L"Tạo commit không có thay đổi file, đôi khi dùng kích hoạt CI.",1,L"Commit message",L"Trigger CI");
    Add(L"Stage & Commit", L"Xem nội dung staged", L"diff --cached", L"Xem chính xác thay đổi sẽ đi vào commit tiếp theo.");

    // DIFF & HISTORY
    Add(L"Diff & Lịch sử", L"Diff chưa stage", L"diff", L"Xem thay đổi working tree chưa stage.");
    Add(L"Diff & Lịch sử", L"Diff đã stage", L"diff --staged", L"Xem thay đổi đã stage so với HEAD.");
    Add(L"Diff & Lịch sử", L"Diff hai branch/ref", L"diff {1}..{2}", L"So sánh nội dung giữa hai ref/branch/commit.",0,L"Ref A",L"main",L"Ref B",L"feature/test");
    Add(L"Diff & Lịch sử", L"Diff một file", L"diff -- {1}", L"Xem thay đổi chưa stage của một file.",0,L"Đường dẫn file",L"src/app.cpp");
    Add(L"Diff & Lịch sử", L"Log ngắn", L"log --oneline --decorate -30", L"Hiện 30 commit gần nhất dạng gọn.");
    Add(L"Diff & Lịch sử", L"Log graph", L"log --oneline --graph --decorate --all -50", L"Hiện graph branch/commit dạng text.");
    Add(L"Diff & Lịch sử", L"Log chi tiết commit", L"show {1}", L"Xem metadata và diff của commit/tag/ref.",0,L"Commit/ref",L"HEAD hoặc abc123");
    Add(L"Diff & Lịch sử", L"Log của một file", L"log --oneline --follow -- {1}", L"Xem lịch sử commit của file, theo dõi rename khi có thể.",0,L"Đường dẫn file",L"src/app.cpp");
    Add(L"Diff & Lịch sử", L"Tìm commit theo message", L"log --oneline --grep={1}", L"Tìm commit có message khớp chuỗi.",0,L"Chuỗi tìm",L"reconnect");
    Add(L"Diff & Lịch sử", L"Reflog", L"reflog --date=local -30", L"Xem 30 lần di chuyển HEAD/ref gần nhất; rất hữu ích để cứu commit.");
    Add(L"Diff & Lịch sử", L"Blame file", L"blame -- {1}", L"Xem commit/tác giả cho từng dòng của file.",0,L"Đường dẫn file",L"src/app.cpp");
    Add(L"Diff & Lịch sử", L"Tên file thay đổi ở commit", L"show --name-status --oneline {1}", L"Xem danh sách file thêm/sửa/xóa trong commit.",0,L"Commit/ref",L"HEAD");
    Add(L"Diff & Lịch sử", L"Commit giữa hai ref", L"log --oneline {1}..{2}", L"Liệt kê commit có ở ref B nhưng không có ở ref A.",0,L"Ref A",L"main",L"Ref B",L"feature/test");

    // RESTORE
    Add(L"Khôi phục", L"Bỏ sửa một file", L"restore -- {1}", L"Khôi phục file tracked về nội dung trong index. Mất thay đổi chưa stage của file.",2,L"Đường dẫn file",L"src/app.cpp");
    Add(L"Khôi phục", L"Bỏ sửa tất cả file tracked", L"restore .", L"Xóa toàn bộ thay đổi chưa stage của file tracked trong thư mục hiện tại.",2);
    Add(L"Khôi phục", L"Khôi phục file từ commit", L"restore --source={1} -- {2}", L"Lấy nội dung file từ commit/ref chỉ định vào working tree.",2,L"Commit/ref",L"HEAD~1",L"Đường dẫn file",L"src/app.cpp");
    Add(L"Khôi phục", L"Revert một commit", L"revert {1}", L"Tạo commit mới đảo ngược thay đổi của commit chỉ định; phù hợp khi lịch sử đã push.",1,L"Commit",L"abc123");
    Add(L"Khôi phục", L"Revert không commit ngay", L"revert --no-commit {1}", L"Áp dụng đảo ngược vào working tree/index nhưng chưa tạo commit.",1,L"Commit",L"abc123");
    Add(L"Khôi phục", L"Hủy revert đang xung đột", L"revert --abort", L"Hủy một quá trình revert đang dở.",1);
    Add(L"Khôi phục", L"Reset soft", L"reset --soft {1}", L"Di chuyển HEAD nhưng giữ index và working tree. Hữu ích để gộp/làm lại commit.",2,L"Commit/ref",L"HEAD~1");
    Add(L"Khôi phục", L"Reset mixed", L"reset --mixed {1}", L"Di chuyển HEAD, reset index nhưng giữ working tree. Đây là mode mặc định của reset.",2,L"Commit/ref",L"HEAD~1");
    Add(L"Khôi phục", L"Reset HARD", L"reset --hard {1}", L"NGUY HIỂM: di chuyển HEAD và xóa thay đổi tracked trong index/working tree để khớp commit.",2,L"Commit/ref",L"HEAD~1 hoặc origin/main");
    Add(L"Khôi phục", L"Reset branch về origin", L"reset --hard origin/{1}", L"NGUY HIỂM: làm branch local khớp hoàn toàn origin/<branch>, bỏ commit/thay đổi local không được bảo vệ.",2,L"Tên branch",L"main");

    // STASH
    Add(L"Stash", L"Stash thay đổi", L"stash push -m {1}", L"Cất tạm thay đổi tracked với tên mô tả.",1,L"Tên stash",L"work in progress");
    Add(L"Stash", L"Stash cả untracked", L"stash push -u -m {1}", L"Cất tạm cả file untracked.",1,L"Tên stash",L"wip");
    Add(L"Stash", L"Stash tất cả kể cả ignored", L"stash push -a -m {1}", L"Cất cả tracked, untracked và ignored; có thể rất lớn.",1,L"Tên stash",L"full backup");
    Add(L"Stash", L"Xem chi tiết stash", L"stash show -p {1}", L"Xem diff của stash chỉ định.",0,L"Stash",L"stash@{0}");
    Add(L"Stash", L"Apply stash", L"stash apply {1}", L"Áp dụng stash nhưng vẫn giữ stash trong danh sách.",1,L"Stash",L"stash@{0}");
    Add(L"Stash", L"Pop stash", L"stash pop {1}", L"Áp dụng stash và xóa khỏi danh sách nếu thành công.",1,L"Stash",L"stash@{0}");
    Add(L"Stash", L"Drop stash", L"stash drop {1}", L"Xóa một stash khỏi danh sách.",2,L"Stash",L"stash@{0}");
    Add(L"Stash", L"Clear toàn bộ stash", L"stash clear", L"NGUY HIỂM: xóa toàn bộ stash.",2);
    Add(L"Stash", L"Tạo branch từ stash", L"stash branch {1} {2}", L"Tạo branch tại commit gốc của stash rồi apply stash.",1,L"Tên branch",L"recover-wip",L"Stash",L"stash@{0}");

    // MERGE & REBASE
    Add(L"Merge & Rebase", L"Merge branch", L"merge {1}", L"Merge branch/ref vào branch hiện tại.",1,L"Branch/ref",L"feature/test");
    Add(L"Merge & Rebase", L"Merge --no-ff", L"merge --no-ff {1}", L"Merge và luôn tạo merge commit khi merge thành công.",1,L"Branch/ref",L"feature/test");
    Add(L"Merge & Rebase", L"Merge squash", L"merge --squash {1}", L"Đưa thay đổi từ branch vào index/working tree nhưng không tạo merge commit; cần commit sau.",1,L"Branch/ref",L"feature/test");
    Add(L"Merge & Rebase", L"Hủy merge", L"merge --abort", L"Hủy quá trình merge đang xung đột và cố khôi phục trạng thái trước merge.",1);
    Add(L"Merge & Rebase", L"Rebase lên branch", L"rebase {1}", L"Đặt các commit branch hiện tại lên trên branch/ref chỉ định. Viết lại lịch sử commit local.",2,L"Branch/ref",L"main");
    Add(L"Merge & Rebase", L"Rebase continue", L"rebase --continue", L"Tiếp tục rebase sau khi đã giải quyết conflict.",1);
    Add(L"Merge & Rebase", L"Rebase skip", L"rebase --skip", L"Bỏ commit đang gây vấn đề trong rebase.",2);
    Add(L"Merge & Rebase", L"Rebase abort", L"rebase --abort", L"Hủy rebase và quay về trạng thái trước khi rebase.",1);
    Add(L"Merge & Rebase", L"Cherry-pick commit", L"cherry-pick {1}", L"Áp dụng thay đổi của một commit lên branch hiện tại và tạo commit mới.",1,L"Commit",L"abc123");
    Add(L"Merge & Rebase", L"Cherry-pick không commit", L"cherry-pick -n {1}", L"Áp dụng commit vào index/working tree nhưng chưa commit.",1,L"Commit",L"abc123");
    Add(L"Merge & Rebase", L"Cherry-pick continue", L"cherry-pick --continue", L"Tiếp tục cherry-pick sau khi giải quyết conflict.",1);
    Add(L"Merge & Rebase", L"Cherry-pick abort", L"cherry-pick --abort", L"Hủy chuỗi cherry-pick đang dở.",1);

    // REMOTE
    Add(L"Remote", L"Xem remote chi tiết origin", L"remote show origin", L"Hiện branch tracking, push/pull URL và trạng thái origin.");
    Add(L"Remote", L"Thêm remote", L"remote add {1} {2}", L"Thêm một remote mới.",1,L"Tên remote",L"upstream",L"URL",L"https://github.com/owner/repo.git");
    Add(L"Remote", L"Đổi URL remote", L"remote set-url {1} {2}", L"Thay URL của remote.",1,L"Tên remote",L"origin",L"URL mới",L"https://github.com/owner/repo.git");
    Add(L"Remote", L"Đổi tên remote", L"remote rename {1} {2}", L"Đổi tên remote local.",1,L"Tên cũ",L"origin",L"Tên mới",L"github");
    Add(L"Remote", L"Xóa remote", L"remote remove {1}", L"Xóa cấu hình remote local; không xóa repo trên server.",2,L"Tên remote",L"upstream");
    Add(L"Remote", L"Xem URL remote", L"remote get-url {1}", L"In URL của remote.",0,L"Tên remote",L"origin");
    Add(L"Remote", L"Xem remote refs", L"ls-remote {1}", L"Liệt kê ref trên remote mà không fetch.",0,L"Tên/URL remote",L"origin");

    // TAG
    Add(L"Tag", L"Tạo lightweight tag", L"tag {1}", L"Tạo tag nhẹ tại HEAD.",1,L"Tên tag",L"v1.0.0");
    Add(L"Tag", L"Tạo annotated tag", L"tag -a {1} -m {2}", L"Tạo annotated tag có message tại HEAD.",1,L"Tên tag",L"v1.0.0",L"Message",L"Release 1.0");
    Add(L"Tag", L"Tạo tag tại commit", L"tag {1} {2}", L"Tạo tag tại commit/ref chỉ định.",1,L"Tên tag",L"v1.0.0",L"Commit/ref",L"abc123");
    Add(L"Tag", L"Xem tag chi tiết", L"show {1}", L"Xem commit/object mà tag trỏ tới.",0,L"Tên tag",L"v1.0.0");
    Add(L"Tag", L"Push một tag", L"push origin {1}", L"Đẩy tag cụ thể lên origin.",1,L"Tên tag",L"v1.0.0");
    Add(L"Tag", L"Push tất cả tag", L"push origin --tags", L"Đẩy tất cả tag local chưa có lên origin.",1);
    Add(L"Tag", L"Xóa tag local", L"tag -d {1}", L"Xóa tag local.",2,L"Tên tag",L"v1.0.0");
    Add(L"Tag", L"Xóa tag remote", L"push origin --delete {1}", L"Xóa ref tag/branch trùng tên trên origin; hãy chắc tên cần xóa.",2,L"Tên tag",L"v1.0.0");

    // SUBMODULE
    Add(L"Submodule", L"Xem trạng thái submodule", L"submodule status", L"Liệt kê submodule và commit đang checkout.");
    Add(L"Submodule", L"Khởi tạo submodule", L"submodule init", L"Khởi tạo cấu hình submodule từ .gitmodules.",1);
    Add(L"Submodule", L"Update submodule", L"submodule update --init --recursive", L"Clone/init và checkout submodule đúng commit được repo cha ghi nhận.",1);
    Add(L"Submodule", L"Update submodule remote", L"submodule update --remote --recursive", L"Cập nhật submodule theo remote branch cấu hình, không chỉ commit pin hiện tại.",1);
    Add(L"Submodule", L"Sync URL submodule", L"submodule sync --recursive", L"Đồng bộ URL submodule từ .gitmodules vào config local.",1);
    Add(L"Submodule", L"Thêm submodule", L"submodule add {1} {2}", L"Thêm repository khác làm submodule tại path chỉ định.",1,L"URL repo",L"https://github.com/owner/lib.git",L"Path",L"vendor/lib");

    // WORKTREE
    Add(L"Worktree", L"Xem worktree", L"worktree list", L"Liệt kê các working tree liên kết với repository.");
    Add(L"Worktree", L"Thêm worktree branch có sẵn", L"worktree add {1} {2}", L"Tạo working tree mới tại path, checkout branch/ref chỉ định.",1,L"Path",L"C:\\work\\repo-test",L"Branch/ref",L"test");
    Add(L"Worktree", L"Thêm worktree + branch mới", L"worktree add -b {1} {2}", L"Tạo branch mới và working tree mới tại path.",1,L"Tên branch mới",L"test2",L"Path",L"C:\\work\\repo-test2");
    Add(L"Worktree", L"Xóa worktree", L"worktree remove {1}", L"Xóa working tree đã đăng ký nếu sạch.",2,L"Path",L"C:\\work\\repo-test");
    Add(L"Worktree", L"Prune worktree", L"worktree prune -v", L"Dọn metadata worktree không còn hợp lệ.",1);

    // CONFIG
    Add(L"Cấu hình", L"Xem user.name", L"config user.name", L"Xem tên Git của repo/user hiện tại.");
    Add(L"Cấu hình", L"Xem user.email", L"config user.email", L"Xem email Git của repo/user hiện tại.");
    Add(L"Cấu hình", L"Đặt user.name repo", L"config user.name {1}", L"Đặt tên tác giả chỉ cho repository hiện tại.",1,L"Tên",L"Duong");
    Add(L"Cấu hình", L"Đặt user.email repo", L"config user.email {1}", L"Đặt email tác giả chỉ cho repository hiện tại.",1,L"Email",L"name@example.com");
    Add(L"Cấu hình", L"Đặt user.name global", L"config --global user.name {1}", L"Đặt tên tác giả Git toàn máy/user.",1,L"Tên",L"Duong");
    Add(L"Cấu hình", L"Đặt user.email global", L"config --global user.email {1}", L"Đặt email tác giả Git toàn máy/user.",1,L"Email",L"name@example.com");
    Add(L"Cấu hình", L"Xem autocrlf", L"config --get core.autocrlf", L"Xem cấu hình chuyển đổi line ending.");
    Add(L"Cấu hình", L"Đặt default branch global", L"config --global init.defaultBranch {1}", L"Đặt tên branch mặc định cho repo git init mới.",1,L"Tên branch",L"main");
    Add(L"Cấu hình", L"Xem credential helper", L"config --global --get credential.helper", L"Xem helper quản lý credential Git.");

    // MAINTENANCE
    Add(L"Bảo trì", L"Git fsck", L"fsck --full", L"Kiểm tra tính toàn vẹn object database. Chỉ đọc nhưng có thể chạy lâu.");
    Add(L"Bảo trì", L"Git gc", L"gc", L"Dọn và tối ưu object database theo cấu hình Git.",1);
    Add(L"Bảo trì", L"Git gc aggressive", L"gc --aggressive --prune=now", L"Tối ưu mạnh, có thể rất chậm; prune object unreachable ngay.",2);
    Add(L"Bảo trì", L"Prune object", L"prune --expire now", L"NGUY HIỂM: xóa object unreachable ngay, làm giảm khả năng cứu dữ liệu bằng reflog/object dangling.",2);
    Add(L"Bảo trì", L"Verify commit", L"verify-commit {1}", L"Kiểm tra chữ ký GPG/SSH của commit nếu có.",0,L"Commit",L"HEAD");
    Add(L"Bảo trì", L"Verify tag", L"verify-tag {1}", L"Kiểm tra chữ ký annotated tag nếu có.",0,L"Tag",L"v1.0.0");

    // ADVANCED
    Add(L"Nâng cao", L"Clean preview", L"clean -nd", L"AN TOÀN: chỉ xem file/thư mục untracked sẽ bị xóa bởi git clean -fd.");
    Add(L"Nâng cao", L"Clean untracked", L"clean -fd", L"NGUY HIỂM: xóa file và thư mục untracked. Hãy chạy Clean preview trước.",2);
    Add(L"Nâng cao", L"Clean ignored preview", L"clean -ndx", L"Chỉ xem cả file ignored + untracked sẽ bị xóa bởi clean -fdx.");
    Add(L"Nâng cao", L"Clean cả ignored", L"clean -fdx", L"RẤT NGUY HIỂM: xóa cả untracked và ignored, ví dụ build output, config local, secret ignored.",2);
    Add(L"Nâng cao", L"Force push an toàn hơn", L"push --force-with-lease", L"Viết lại remote branch nhưng từ chối nếu remote đã thay đổi ngoài trạng thái local biết. Vẫn nguy hiểm.",2);
    Add(L"Nâng cao", L"Force push", L"push --force", L"RẤT NGUY HIỂM: cưỡng bức cập nhật remote branch và có thể ghi đè lịch sử người khác.",2);
    Add(L"Nâng cao", L"Detached checkout commit", L"switch --detach {1}", L"Checkout commit/ref ở detached HEAD để xem/test mà không ở branch.",1,L"Commit/ref",L"abc123");
    Add(L"Nâng cao", L"Tạo archive ZIP từ HEAD", L"archive --format=zip --output={1} HEAD", L"Xuất snapshot file tracked ở HEAD thành ZIP.",1,L"File ZIP",L"source.zip");
    Add(L"Nâng cao", L"Bisect start", L"bisect start", L"Bắt đầu git bisect để tìm commit gây lỗi.",1);
    Add(L"Nâng cao", L"Bisect đánh dấu bad", L"bisect bad {1}", L"Đánh dấu commit hiện tại/chỉ định là bad trong phiên bisect.",1,L"Commit/ref",L"HEAD");
    Add(L"Nâng cao", L"Bisect đánh dấu good", L"bisect good {1}", L"Đánh dấu commit là good trong phiên bisect.",1,L"Commit/ref",L"abc123");
    Add(L"Nâng cao", L"Bisect reset", L"bisect reset", L"Kết thúc bisect và quay lại trạng thái trước bisect.",1);
}


struct MultiBranchDialogState {
    const std::vector<OptionItem>* items{};
    std::wstring protectedBranch;
    std::vector<std::wstring> selected;
    HWND list{};
    bool done{false};
    bool accepted{false};
};

static LRESULT CALLBACK MultiBranchWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<MultiBranchDialogState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            state = reinterpret_cast<MultiBranchDialogState*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

            HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            std::wstring note = L"Giữ Ctrl/Shift để chọn nhiều branch. Branch mặc định được bảo vệ: " +
                (state->protectedBranch.empty() ? L"main" : state->protectedBranch);

            HWND info = CreateWindowW(L"STATIC", note.c_str(), WS_CHILD|WS_VISIBLE,
                                      12, 10, 560, 34, h, nullptr, g_hInst, nullptr);
            state->list = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_EXTENDEDSEL|LBS_NOINTEGRALHEIGHT,
                12, 48, 560, 285, h, (HMENU)2100, g_hInst, nullptr
            );
            HWND selectAll = CreateWindowW(L"BUTTON", L"Chọn tất cả", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                                           12, 342, 110, 30, h, (HMENU)2101, g_hInst, nullptr);
            HWND clearAll = CreateWindowW(L"BUTTON", L"Bỏ chọn", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                                          130, 342, 100, 30, h, (HMENU)2102, g_hInst, nullptr);
            HWND ok = CreateWindowW(L"BUTTON", L"XÓA ĐÃ CHỌN", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
                                    352, 342, 120, 30, h, (HMENU)IDOK, g_hInst, nullptr);
            HWND cancel = CreateWindowW(L"BUTTON", L"Hủy", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                                        480, 342, 92, 30, h, (HMENU)IDCANCEL, g_hInst, nullptr);

            for (HWND c : {info, state->list, selectAll, clearAll, ok, cancel})
                SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);

            if (state->items) {
                for (const auto& item : *state->items) {
                    if (!state->protectedBranch.empty() && item.value == state->protectedBranch) continue;
                    SendMessageW(state->list, LB_ADDSTRING, 0, (LPARAM)item.display.c_str());
                }
            }
            return 0;
        }
        case WM_COMMAND: {
            if (!state) return 0;
            int id = LOWORD(wp);
            if (id == 2101 && HIWORD(wp) == BN_CLICKED) {
                SendMessageW(state->list, LB_SETSEL, TRUE, -1);
                return 0;
            }
            if (id == 2102 && HIWORD(wp) == BN_CLICKED) {
                SendMessageW(state->list, LB_SETSEL, FALSE, -1);
                return 0;
            }
            if (id == IDOK && HIWORD(wp) == BN_CLICKED) {
                int count = (int)SendMessageW(state->list, LB_GETSELCOUNT, 0, 0);
                if (count <= 0) {
                    MessageBoxW(h, L"Hãy chọn ít nhất một branch remote.", L"Chưa chọn branch", MB_OK|MB_ICONINFORMATION);
                    return 0;
                }
                std::vector<int> indices((size_t)count);
                SendMessageW(state->list, LB_GETSELITEMS, count, (LPARAM)indices.data());
                state->selected.clear();
                for (int idx : indices) {
                    int len = (int)SendMessageW(state->list, LB_GETTEXTLEN, idx, 0);
                    std::wstring display((size_t)len + 1, L'\0');
                    SendMessageW(state->list, LB_GETTEXT, idx, (LPARAM)display.data());
                    display.resize((size_t)len);
                    if (!state->items) continue;
                    for (const auto& item : *state->items) {
                        if (item.display == display) {
                            state->selected.push_back(item.value);
                            break;
                        }
                    }
                }
                state->accepted = !state->selected.empty();
                state->done = true;
                DestroyWindow(h);
                return 0;
            }
            if (id == IDCANCEL && HIWORD(wp) == BN_CLICKED) {
                state->done = true;
                state->accepted = false;
                DestroyWindow(h);
                return 0;
            }
            return 0;
        }
        case WM_CLOSE:
            if (state) {
                state->done = true;
                state->accepted = false;
            }
            DestroyWindow(h);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static std::vector<std::wstring> ChooseOriginBranchesToDelete() {
    MultiBranchDialogState state;
    state.items = &g_cache.originBranches;
    state.protectedBranch = g_cache.originDefaultBranch.empty() ? L"main" : g_cache.originDefaultBranch;

    if (g_cache.originBranches.empty()) {
        MessageBoxW(g_main,
            L"Cache chưa có branch của origin.\r\n\r\nHãy bấm Làm mới hoặc chạy Fetch trước.",
            L"Không có branch remote", MB_OK|MB_ICONINFORMATION);
        return {};
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = MultiBranchWndProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"GitToolboxVNMultiBranch";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            MessageBoxW(g_main, L"Không tạo được cửa sổ chọn branch.", L"GitToolboxVN", MB_OK|MB_ICONERROR);
            return {};
        }
        registered = true;
    }

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"GitToolboxVNMultiBranch",
        L"Xóa nhiều branch remote - origin",
        WS_CAPTION|WS_SYSMENU|WS_POPUP|WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 420,
        g_main, nullptr, g_hInst, &state
    );
    if (!dlg) return {};

    RECT parent{}, rc{};
    GetWindowRect(g_main, &parent);
    GetWindowRect(dlg, &rc);
    int x = parent.left + ((parent.right - parent.left) - (rc.right - rc.left)) / 2;
    int y = parent.top + ((parent.bottom - parent.top) - (rc.bottom - rc.top)) / 2;
    SetWindowPos(dlg, HWND_TOP, max(0, x), max(0, y), 0, 0, SWP_NOSIZE|SWP_SHOWWINDOW);

    EnableWindow(g_main, FALSE);
    MSG msg{};
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(g_main, TRUE);
    SetForegroundWindow(g_main);

    return state.accepted ? state.selected : std::vector<std::wstring>{};
}

static std::vector<OptionItem> OptionsForLabel(const std::wstring& label) {
    std::wstring l = Lower(label);
    if (l.empty()) return {};
    if (l.find(L"branch mới") != std::wstring::npos || l.find(L"tên mới") != std::wstring::npos) return {};
    if (l.find(L"commit message") != std::wstring::npos || l == L"message" || l.find(L"chuỗi") != std::wstring::npos) return {};
    if (l.find(L"stash") != std::wstring::npos) return g_cache.stashes;
    if (l.find(L"tag") != std::wstring::npos) return g_cache.tags;
    if (l.find(L"đường dẫn file") != std::wstring::npos || l == L"file") return g_cache.changedFiles;
    if (l.find(L"commit/ref") != std::wstring::npos || l.find(L"branch/ref") != std::wstring::npos || l == L"ref" || l.find(L"ref a") != std::wstring::npos || l.find(L"ref b") != std::wstring::npos) return g_cache.refs;
    if (l.find(L"commit") != std::wstring::npos) return g_cache.commits;
    if (l.find(L"branch remote") != std::wstring::npos) return g_cache.originBranches;
    if (l.find(L"branch") != std::wstring::npos) return g_cache.branches;
    if (l.find(L"remote") != std::wstring::npos) return g_cache.remotes;
    return {};
}

static void SetupParamCombo(HWND labelH, HWND combo, std::vector<OptionItem>& target, const std::wstring& name, const std::wstring& hint) {
    bool show = !name.empty();
    ShowWindow(labelH, show ? SW_SHOW : SW_HIDE);
    ShowWindow(combo, show ? SW_SHOW : SW_HIDE);
    target.clear();
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    if (!show) return;

    SetText(labelH, name + L":");
    SetText(combo, L"");
    target = OptionsForLabel(name);
    for (const auto& option : target) {
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)option.display.c_str());
    }
#ifdef CB_SETCUEBANNER
    SendMessageW(combo, CB_SETCUEBANNER, 0, (LPARAM)hint.c_str());
#endif
}

static void UpdatePreviewAndFields() {
    if (g_selected < 0 || g_selected >= (int)g_commands.size()) return;
    const auto& c = g_commands[g_selected];
    std::wstring safety = c.risk == 0 ? L"✓ Chỉ đọc / an toàn" : (c.risk == 1 ? L"● Có thay đổi repo" : L"⚠ Nguy hiểm / cần xác nhận");
    SetText(g_desc, c.title + L"\r\n\r\n" + c.description + L"\r\n\r\nMức độ: " + safety);

    SetupParamCombo(g_p1Label, g_p1, g_p1Options, c.p1Label, c.p1Hint);
    SetupParamCombo(g_p2Label, g_p2, g_p2Options, c.p2Label, c.p2Hint);
    SetupParamCombo(g_p3Label, g_p3, g_p3Options, c.p3Label, c.p3Hint);
    if (!g_p1Options.empty() || !g_p2Options.empty() || !g_p3Options.empty()) {
        SetText(g_desc, c.title + L"\r\n\r\n" + c.description + L"\r\n\r\nMức độ: " + safety +
            L"\r\n\r\n▼ Tham số có danh sách lấy từ bộ nhớ tạm Git. Chỉ cần chọn; vẫn có thể tự gõ giá trị khác.");
    }
    if (c.args == L"__MULTI_DELETE_ORIGIN_BRANCHES__") {
        SetText(g_preview, L"git push origin --delete <chọn nhiều branch>");
    } else {
        SetText(g_preview, L"git " + BuildArgs(c, false));
    }
}

static void Refilter() {
    std::wstring q = Lower(GetText(g_search));
    int catIndex = (int)SendMessageW(g_category, CB_GETCURSEL, 0, 0);
    std::wstring cat = L"Tất cả";
    if (catIndex >= 0) {
        wchar_t buf[128]{};
        SendMessageW(g_category, CB_GETLBTEXT, catIndex, (LPARAM)buf);
        cat = buf;
    }
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    g_filtered.clear();
    for (int i = 0; i < (int)g_commands.size(); ++i) {
        const auto& c = g_commands[i];
        if (cat != L"Tất cả" && c.category != cat) continue;
        std::wstring hay = Lower(c.category + L" " + c.title + L" " + c.args + L" " + c.description);
        if (!q.empty() && hay.find(q) == std::wstring::npos) continue;
        g_filtered.push_back(i);
        std::wstring prefix = c.risk == 2 ? L"⚠ " : (c.risk == 1 ? L"● " : L"✓ ");
        SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)(prefix + c.title).c_str());
    }
    if (!g_filtered.empty()) {
        SendMessageW(g_list, LB_SETCURSEL, 0, 0);
        g_selected = g_filtered[0];
        UpdatePreviewAndFields();
    } else {
        g_selected = -1;
        SetText(g_desc, L"Không có lệnh phù hợp.");
        SetText(g_preview, L"");
    }
    std::wstringstream ss;
    ss << L"Hiển thị " << g_filtered.size() << L" / " << g_commands.size() << L" lệnh";
    if (!g_cache.currentBranch.empty()) {
        ss << L"   |   Nhánh: " << g_cache.currentBranch
           << L"   |   Cache: " << g_cache.branches.size() << L" branch, "
           << g_cache.commits.size() << L" commit, " << g_cache.changedFiles.size() << L" file đổi";
    }
    SetText(g_status, ss.str());
}

static std::wstring BrowseFolder(HWND owner) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Chọn thư mục repository Git";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pid = SHBrowseForFolderW(&bi);
    if (!pid) return {};
    wchar_t path[MAX_PATH]{};
    std::wstring result;
    if (SHGetPathFromIDListW(pid, path)) result = path;
    CoTaskMemFree(pid);
    return result;
}

static bool CaptureGit(const std::wstring& repo, const std::wstring& args, std::wstring& output, DWORD& code) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE r = nullptr, w = nullptr;
    if (!CreatePipe(&r, &w, &sa, 0)) return false;
    SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = w;
    si.hStdError = w;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"git.exe -c core.quotepath=false -c i18n.logOutputEncoding=utf-8 " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);

    BOOL created = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                  repo.empty() ? nullptr : repo.c_str(), &si, &pi);
    CloseHandle(w);
    if (!created) {
        CloseHandle(r);
        code = GetLastError();
        return false;
    }

    std::string bytes;
    char tmp[4096];
    DWORD got = 0;
    while (ReadFile(r, tmp, sizeof(tmp), &got, nullptr) && got) bytes.append(tmp, tmp + got);
    CloseHandle(r);
    WaitForSingleObject(pi.hProcess, INFINITE);
    code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    output = Utf8ToWide(bytes);
    return true;
}

static void RefreshCacheAsync(bool announce = true) {
    std::wstring repo = GetText(g_repo);
    if (repo.empty()) return;
    EnableWindow(g_refresh, FALSE);
    SetWindowTextW(g_refresh, L"Đang nạp...");
    if (announce) AppendOutput(L"\r\n[Cache] Đang đọc branch / remote / commit / tag / stash / file thay đổi...\r\n");

    std::thread([repo=std::move(repo)]() {
        auto* result = new CacheResult();
        result->repo = repo;
        std::wstring out;
        DWORD code = 0;
        auto run = [&](const std::wstring& args, std::wstring& dst) -> bool {
            dst.clear();
            if (!CaptureGit(repo, args, dst, code)) {
                result->error = L"Không chạy được git.exe khi nạp dữ liệu tạm.";
                return false;
            }
            return code == 0;
        };

        if (!run(L"rev-parse --is-inside-work-tree", out) || Lower(Trim(out)) != L"true") {
            result->error = L"Thư mục hiện tại không phải Git working tree hợp lệ.";
            PostMessageW(g_main, WM_APP_CACHE_READY, 0, (LPARAM)result);
            return;
        }

        if (run(L"branch --show-current", out)) result->currentBranch = Trim(out);

        if (run(L"symbolic-ref --quiet --short refs/remotes/origin/HEAD", out)) {
            std::wstring def = Trim(out);
            const std::wstring prefix = L"origin/";
            if (def.rfind(prefix, 0) == 0) def = def.substr(prefix.size());
            result->originDefaultBranch = def;
        }

        if (run(L"for-each-ref --format=%(refname:short) refs/heads", out)) {
            for (const auto& line : SplitLines(out)) {
                AddOptionUnique(result->localBranches, line);
                AddOptionUnique(result->branches, line);
                AddOptionUnique(result->refs, line);
            }
        }

        std::vector<std::wstring> remoteNames;
        if (run(L"remote", out)) {
            for (const auto& line : SplitLines(out)) {
                remoteNames.push_back(line);
                AddOptionUnique(result->remotes, line);
            }
        }

        if (run(L"for-each-ref --format=%(refname:short) refs/remotes", out)) {
            for (const auto& full : SplitLines(out)) {
                if (full.size() >= 5 && full.substr(full.size()-5) == L"/HEAD") continue;
                AddOptionUnique(result->refs, full);
                std::wstring shortName = full;
                size_t slash = full.find(L'/');
                std::wstring remoteName;
                if (slash != std::wstring::npos && slash + 1 < full.size()) {
                    remoteName = full.substr(0, slash);
                    shortName = full.substr(slash + 1);
                }
                AddOptionUnique(result->branches, shortName, shortName + L"   [remote: " + remoteName + L"]");
                if (remoteName == L"origin") {
                    AddOptionUnique(result->originBranches, shortName);
                }
            }
        }

        if (run(L"log --all --format=%h%x09%s -80", out)) {
            for (const auto& line : SplitLines(out)) {
                size_t tab = line.find(L'\t');
                std::wstring sha = tab == std::wstring::npos ? line : line.substr(0, tab);
                std::wstring subject = tab == std::wstring::npos ? L"" : line.substr(tab + 1);
                AddOptionUnique(result->commits, sha, subject.empty() ? sha : sha + L"  —  " + subject);
                AddOptionUnique(result->refs, sha, subject.empty() ? sha : sha + L"  —  " + subject);
            }
        }

        if (run(L"tag --list", out)) {
            for (const auto& line : SplitLines(out)) {
                AddOptionUnique(result->tags, line);
                AddOptionUnique(result->refs, line, line + L"   [tag]");
            }
        }

        if (run(L"stash list --format=%gd%x09%s", out)) {
            for (const auto& line : SplitLines(out)) {
                size_t tab = line.find(L'\t');
                std::wstring id = tab == std::wstring::npos ? line : line.substr(0, tab);
                std::wstring subject = tab == std::wstring::npos ? L"" : line.substr(tab + 1);
                AddOptionUnique(result->stashes, id, subject.empty() ? id : id + L"  —  " + subject);
            }
        }

        if (run(L"status --porcelain=v1 -uall", out)) {
            for (const auto& line0 : SplitLines(out)) {
                std::wstring line = line0;
                if (line.size() <= 3) continue;
                std::wstring path = Trim(line.substr(3));
                size_t arrow = path.find(L" -> ");
                if (arrow != std::wstring::npos) path = path.substr(arrow + 4);
                if (!path.empty() && path.front() == L'"' && path.back() == L'"' && path.size() >= 2) {
                    path = path.substr(1, path.size() - 2);
                }
                AddOptionUnique(result->changedFiles, path);
            }
        }

        PostMessageW(g_main, WM_APP_CACHE_READY, 0, (LPARAM)result);
    }).detach();
}

static void RunGitAsync(std::wstring repo, std::wstring args) {
    EnableWindow(g_run, FALSE);
    AppendOutput(L"\r\n> git " + args + L"\r\n");
    std::thread([repo=std::move(repo), args=std::move(args)]() {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE r = nullptr, w = nullptr;
        if (!CreatePipe(&r, &w, &sa, 0)) {
            PostMessageW(g_main, WM_APP_DONE, 1, (LPARAM)new std::wstring(L"Không tạo được pipe.\r\n"));
            return;
        }
        SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.hStdOutput = w; si.hStdError = w; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"git.exe -c core.quotepath=false -c i18n.logOutputEncoding=utf-8 " + args;
        std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);

        BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                 repo.empty() ? nullptr : repo.c_str(), &si, &pi);
        CloseHandle(w);
        if (!ok) {
            DWORD e = GetLastError(); CloseHandle(r);
            std::wstringstream ss; ss << L"Không chạy được git.exe. Win32 error=" << e << L"\r\nHãy kiểm tra Git đã cài và có trong PATH.\r\n";
            PostMessageW(g_main, WM_APP_DONE, 1, (LPARAM)new std::wstring(ss.str()));
            return;
        }

        std::string bytes;
        char tmp[4096]; DWORD got = 0;
        while (ReadFile(r, tmp, sizeof(tmp), &got, nullptr) && got) bytes.append(tmp, tmp + got);
        CloseHandle(r);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);

        std::wstring text = Utf8ToWide(bytes);
        if (text.empty()) text = L"(Git không trả output)\r\n";
        if (text.size() >= 1 && text.back() != L'\n') text += L"\r\n";
        std::wstringstream ss; ss << text << L"[exit code: " << code << L"]\r\n";
        PostMessageW(g_main, WM_APP_DONE, code == 0 ? 0 : 1, (LPARAM)new std::wstring(ss.str()));
    }).detach();
}

static void OnRun() {
    if (g_selected < 0) return;
    std::wstring repo = GetText(g_repo);
    if (repo.empty()) {
        MessageBoxW(g_main, L"Hãy chọn thư mục repo trước.", L"GitToolboxVN", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const auto& c = g_commands[g_selected];

    if (c.args == L"__MULTI_DELETE_ORIGIN_BRANCHES__") {
        auto branches = ChooseOriginBranchesToDelete();
        if (branches.empty()) return;

        std::wstring args = L"push origin --delete";
        std::wstring branchList;
        for (const auto& branch : branches) {
            args += L" " + QuoteArg(branch);
            branchList += L"  - " + branch + L"\r\n";
        }

        std::wstring msg =
            L"XÓA NHIỀU BRANCH REMOTE\r\n\r\n"
            L"Các branch sau sẽ bị xóa khỏi origin:\r\n" + branchList +
            L"\r\nLệnh sẽ chạy:\r\ngit " + args +
            L"\r\n\r\nBranch mặc định được bảo vệ và không nằm trong danh sách.\r\n"
            L"Bạn chắc chắn muốn tiếp tục?";

        if (MessageBoxW(g_main, msg.c_str(), L"Xác nhận xóa branch remote",
                        MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) != IDYES) return;
        RunGitAsync(repo, args);
        return;
    }

    bool valid = false;
    std::wstring args = BuildArgs(c, true, &valid);
    if (!valid) {
        MessageBoxW(g_main, L"Hãy nhập đủ tham số của lệnh.", L"Thiếu tham số", MB_OK | MB_ICONWARNING);
        return;
    }
    if (c.risk == 2) {
        std::wstring msg = L"LỆNH NGUY HIỂM\r\n\r\n" + c.description + L"\r\n\r\nSẽ chạy:\r\ngit " + args + L"\r\n\r\nBạn chắc chắn muốn tiếp tục?";
        if (MessageBoxW(g_main, msg.c_str(), L"Xác nhận thao tác Git", MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) != IDYES) return;
    }
    RunGitAsync(repo, args);
}

static void Layout(int w, int h) {
    const int m=10, top=10, repoH=26, searchH=26, statusH=22;
    int leftW = max(300, w * 36 / 100);
    int rightX = m + leftW + 10;
    int rightW = max(300, w - rightX - m);

    MoveWindow(g_repo, m, top, leftW-180, repoH, TRUE);
    HWND browse = GetDlgItem(g_main, 1001); MoveWindow(browse, m+leftW-175, top, 85, repoH, TRUE);
    MoveWindow(g_refresh, m+leftW-85, top, 85, repoH, TRUE);
    MoveWindow(g_search, m, top+34, leftW*2/3-5, searchH, TRUE);
    MoveWindow(g_category, m+leftW*2/3, top+34, leftW-leftW*2/3, searchH, TRUE);
    MoveWindow(g_list, m, top+68, leftW, h-top-68-statusH-15, TRUE);
    MoveWindow(g_status, m, h-statusH-5, leftW, statusH, TRUE);

    int y=top;
    MoveWindow(g_desc, rightX, y, rightW, 120, TRUE); y+=128;
    MoveWindow(g_p1Label, rightX, y, 120, 22, TRUE); MoveWindow(g_p1, rightX+125, y, rightW-125, 220, TRUE); y+=30;
    MoveWindow(g_p2Label, rightX, y, 120, 22, TRUE); MoveWindow(g_p2, rightX+125, y, rightW-125, 220, TRUE); y+=30;
    MoveWindow(g_p3Label, rightX, y, 120, 22, TRUE); MoveWindow(g_p3, rightX+125, y, rightW-125, 220, TRUE); y+=34;
    MoveWindow(g_preview, rightX, y, rightW-100, 28, TRUE); MoveWindow(g_run, rightX+rightW-95, y, 95, 28, TRUE); y+=36;
    MoveWindow(g_output, rightX, y, rightW, max(100, h-y-m), TRUE);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
        case WM_CREATE: {
            g_main = h;
            g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            g_mono = CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
            g_repo = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,h,(HMENU)1000,g_hInst,nullptr);
            CreateWindowW(L"BUTTON",L"Chọn repo...",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,0,0,h,(HMENU)1001,g_hInst,nullptr);
            g_refresh = CreateWindowW(L"BUTTON",L"Làm mới",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,0,0,h,(HMENU)1012,g_hInst,nullptr);
            g_search = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,h,(HMENU)1002,g_hInst,nullptr);
            SendMessageW(g_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Tìm lệnh: branch, pull, reset, stash...");
            g_category = CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,0,0,h,(HMENU)1003,g_hInst,nullptr);
            for(auto& c: Categories()) SendMessageW(g_category,CB_ADDSTRING,0,(LPARAM)c.c_str());
            SendMessageW(g_category,CB_SETCURSEL,0,0);
            g_list = CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|LBS_NOTIFY|WS_VSCROLL|LBS_NOINTEGRALHEIGHT,0,0,0,0,h,(HMENU)1004,g_hInst,nullptr);
            g_desc = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,0,0,0,0,h,(HMENU)1005,g_hInst,nullptr);
            g_p1Label = CreateWindowW(L"STATIC",L"",WS_CHILD,0,0,0,0,h,nullptr,g_hInst,nullptr);
            g_p1 = CreateWindowExW(WS_EX_CLIENTEDGE,L"COMBOBOX",L"",WS_CHILD|CBS_DROPDOWN|WS_VSCROLL|CBS_AUTOHSCROLL,0,0,0,240,h,(HMENU)1006,g_hInst,nullptr);
            g_p2Label = CreateWindowW(L"STATIC",L"",WS_CHILD,0,0,0,0,h,nullptr,g_hInst,nullptr);
            g_p2 = CreateWindowExW(WS_EX_CLIENTEDGE,L"COMBOBOX",L"",WS_CHILD|CBS_DROPDOWN|WS_VSCROLL|CBS_AUTOHSCROLL,0,0,0,240,h,(HMENU)1007,g_hInst,nullptr);
            g_p3Label = CreateWindowW(L"STATIC",L"",WS_CHILD,0,0,0,0,h,nullptr,g_hInst,nullptr);
            g_p3 = CreateWindowExW(WS_EX_CLIENTEDGE,L"COMBOBOX",L"",WS_CHILD|CBS_DROPDOWN|WS_VSCROLL|CBS_AUTOHSCROLL,0,0,0,240,h,(HMENU)1008,g_hInst,nullptr);
            g_preview = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_READONLY|ES_AUTOHSCROLL,0,0,0,0,h,(HMENU)1009,g_hInst,nullptr);
            g_run = CreateWindowW(L"BUTTON",L"CHẠY",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,0,0,0,0,h,(HMENU)1010,g_hInst,nullptr);
            g_output = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|ES_AUTOHSCROLL|WS_VSCROLL|WS_HSCROLL,0,0,0,0,h,(HMENU)1011,g_hInst,nullptr);
            g_status = CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,0,0,h,nullptr,g_hInst,nullptr);

            for(HWND c : {g_repo,g_search,g_category,g_list,g_desc,g_p1Label,g_p1,g_p2Label,g_p2,g_p3Label,g_p3,g_preview,g_run,g_status,g_refresh,GetDlgItem(h,1001)}) SendMessageW(c,WM_SETFONT,(WPARAM)g_font,TRUE);
            SendMessageW(g_output,WM_SETFONT,(WPARAM)g_mono,TRUE);
            SendMessageW(g_preview,WM_SETFONT,(WPARAM)g_mono,TRUE);

            wchar_t cwd[MAX_PATH]{}; GetCurrentDirectoryW(MAX_PATH,cwd); SetText(g_repo,cwd);
            BuildCatalog(); Refilter();
            AppendOutput(L"GitToolboxVN v1.3 sẵn sàng. Dữ liệu branch/commit/tag/stash/file sẽ được lưu tạm trong RAM để chọn bằng danh sách.\r\n");
            RefreshCacheAsync(false);
            return 0;
        }
        case WM_SIZE: Layout(LOWORD(lp), HIWORD(lp)); return 0;
        case WM_COMMAND: {
            int id = LOWORD(wp), code = HIWORD(wp);
            if(id==1001 && code==BN_CLICKED) {
                auto p=BrowseFolder(h);
                if(!p.empty()) { SetText(g_repo,p); RefreshCacheAsync(true); }
            }
            else if(id==1012 && code==BN_CLICKED) RefreshCacheAsync(true);
            else if(id==1002 && code==EN_CHANGE) Refilter();
            else if(id==1003 && code==CBN_SELCHANGE) Refilter();
            else if(id==1004 && code==LBN_SELCHANGE) {
                int sel=(int)SendMessageW(g_list,LB_GETCURSEL,0,0); if(sel>=0 && sel<(int)g_filtered.size()) { g_selected=g_filtered[sel]; UpdatePreviewAndFields(); }
            }
            else if((id==1006||id==1007||id==1008) && (code==CBN_EDITCHANGE || code==CBN_SELCHANGE)) {
                if(g_selected>=0) {
                    const auto& c = g_commands[g_selected];
                    SetText(g_preview, c.args == L"__MULTI_DELETE_ORIGIN_BRANCHES__"
                        ? L"git push origin --delete <chọn nhiều branch>"
                        : L"git " + BuildArgs(c,false));
                }
            }
            else if(id==1010 && code==BN_CLICKED) OnRun();
            return 0;
        }
        case WM_APP_DONE: {
            auto* s=(std::wstring*)lp;
            if(s){AppendOutput(*s); delete s;}
            EnableWindow(g_run,TRUE);
            if (wp == 0) RefreshCacheAsync(false);
            return 0;
        }
        case WM_APP_CACHE_READY: {
            auto* cache = (CacheResult*)lp;
            if (cache) {
                if (cache->repo == GetText(g_repo)) {
                    g_cache = std::move(*cache);
                    if (!g_cache.error.empty()) {
                        AppendOutput(L"[Cache] " + g_cache.error + L"\r\n");
                    } else {
                        std::wstringstream ss;
                        ss << L"[Cache] Nhánh hiện tại: " << (g_cache.currentBranch.empty() ? L"(detached/không rõ)" : g_cache.currentBranch)
                           << L" | " << g_cache.branches.size() << L" branch | "
                           << g_cache.originBranches.size() << L" origin branch | "
                           << g_cache.commits.size() << L" commit | "
                           << g_cache.stashes.size() << L" stash | "
                           << g_cache.changedFiles.size() << L" file thay đổi\r\n";
                        AppendOutput(ss.str());
                    }
                    UpdatePreviewAndFields();
                    Refilter();
                }
                delete cache;
            }
            SetWindowTextW(g_refresh, L"Làm mới");
            EnableWindow(g_refresh, TRUE);
            return 0;
        }
        case WM_DESTROY: if(g_mono) DeleteObject(g_mono); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int show) {
    g_hInst=hInst;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=hInst; wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION); wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName=L"GitToolboxVNWindow";
    RegisterClassW(&wc);
    HWND h=CreateWindowExW(0,wc.lpszClassName,L"GitToolboxVN v1.3 - Git Command Toolbox tiếng Việt",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1120,720,nullptr,nullptr,hInst,nullptr);
    if(!h) return 1;
    ShowWindow(h,show); UpdateWindow(h);
    MSG m{}; while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);} CoUninitialize(); return (int)m.wParam;
}

GitToolboxVN v1.3
=================

Mục tiêu
--------
Một Git Command Toolbox native cho Windows: muốn làm gì thì chọn lệnh, xem giải nghĩa tiếng Việt, chọn tham số rồi bấm CHẠY.
Không phải phần mềm quản lý dự án và không thay thế Git. Chương trình gọi git.exe đã cài trên máy.

Mới trong v1.3
--------------
1. Xóa nhiều branch remote chỉ bằng click:
   - chọn lệnh "Xóa nhiều branch remote"
   - app lấy danh sách branch của origin từ cache
   - giữ Ctrl/Shift hoặc bấm "Chọn tất cả"
   - app tạo đúng một lệnh:
       git push origin --delete branch1 branch2 branch3
   - trước khi chạy luôn hiện hộp xác nhận cuối cùng.

2. Bảo vệ branch mặc định của origin.
   App đọc:
       refs/remotes/origin/HEAD
   để biết branch mặc định (thường là main).
   Branch này không xuất hiện trong danh sách xóa nhiều.
   Nếu không xác định được, app mặc định bảo vệ main.

3. Thêm đúng lệnh "Fetch + prune":
       git fetch --prune
   Ngoài ra vẫn giữ:
       git fetch --all --prune
       git remote prune origin

4. Dropdown "Tên branch remote" giờ ưu tiên đúng các branch trên origin,
   thay vì trộn branch local và remote.

5. Giữ toàn bộ cơ chế v1.2:
   - cache branch / remote / commit / tag / stash / file thay đổi trong RAM
   - dropdown chọn tham số
   - tự refresh cache sau lệnh thành công
   - UTF-8 tiếng Việt
   - BUILD.cmd tự tìm Visual Studio Build Tools bằng vswhere.exe

Ví dụ xóa 3 branch cũ
---------------------
Chọn:
  Branch -> Xóa nhiều branch remote

Tick/chọn:
  agent/direct-comment-elements
  agent/separate-live-events
  test/direct-webcast-v11

App sẽ preview và xác nhận:

  git push origin --delete agent/direct-comment-elements agent/separate-live-events test/direct-webcast-v11

Sau đó có thể chạy:
  Đồng bộ -> Fetch + prune

tương đương:
  git fetch --prune

Bộ nhớ tạm
----------
Khi mở repo hoặc bấm "Làm mới", app tự đọc:
- branch local
- branch remote của origin
- branch mặc định origin
- remote
- 80 commit gần nhất trên mọi branch
- tag
- stash
- file đang thay đổi/untracked
- branch hiện tại

Cache chỉ nằm trong RAM; đóng app là mất.

Build
-----
Chỉ cần chạy:

  BUILD.cmd

BUILD.cmd sẽ:
1. dùng cl.exe nếu CMD hiện tại đã có MSVC;
2. nếu chưa có, tự tìm Visual Studio/Build Tools bằng vswhere.exe và gọi VsDevCmd.bat -arch=x64;
3. nếu vẫn không có MSVC, thử g++.exe của MinGW-w64 trong PATH.

Source và compiler đều dùng UTF-8 để giữ tiếng Việt đúng.

Chạy
----
  GitToolboxVN.exe

Hoặc:
  RUN.cmd

Lưu ý an toàn
-------------
Các lệnh reset --hard, clean, force push, xóa branch/tag/stash... được đánh dấu nguy hiểm và yêu cầu xác nhận trước khi chạy.
"Xóa nhiều branch remote" có hai lớp bảo vệ:
- branch mặc định origin không cho chọn;
- danh sách branch + lệnh hoàn chỉnh được hiển thị lại trước khi xóa.

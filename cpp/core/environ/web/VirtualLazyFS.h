//---------------------------------------------------------------------------
// VirtualLazyFS（VLFS）C ABI — 平台边界（Web 独有）
//
// 游戏文件不再复制进 Emscripten MEMFS，而是由 JS 侧懒加载虚拟文件系统
// （platforms/web/vlfs.js，主线程单例）按需供数：
//   读  = Blob 切片 / HTTP Range / ZIP stored 切片 / deflate 解压落 OPFS
//   写  = 内存 overlay（存档级小文件），关闭时由 JS 钩子做 IDB 持久化
//
// 线程模型（调用方无须关心，本层内部分发）：
//   主线程  : 元数据走同步 EM_JS；读未命中块缓存时走 EM_ASYNC_JS（JSPI 挂起，
//             要求调用栈源自 promising 导出 —— main() 或主循环 tick）。
//   pthread : 经 emscripten proxying 系统队列投递到主线程执行，worker 在
//             futex 上阻塞等待完成（JS 异步完成后 emscripten_proxy_finish）。
//---------------------------------------------------------------------------
#ifndef VirtualLazyFSH
#define VirtualLazyFSH

#ifdef __EMSCRIPTEN__

#include <cstdint>
#include <functional>
#include <string>

namespace VLFS {

// JS 侧 VLFS 是否就绪（vlfs.js 已加载并 init）
bool Enabled();

// 0=不存在 1=文件 2=目录（大小写不敏感回退解析）
int Has(const char *path);

// 存在时返回 true 并填充 size/isDir
bool Stat(const char *path, uint64_t *size, int *isDir);

// 大小写不敏感解析为规范路径；解析失败返回 false
bool ResolveCase(const char *path, std::string &out);

// 枚举目录孩子；目录不存在返回 false
bool ListDir(const char *path,
             const std::function<void(const char *name, bool isDir,
                                      uint64_t size)> &cb);

// writeMode: 0=读 1=写(截断,新建 overlay)。失败返回 <0
int Open(const char *path, int writeMode);
int Close(int fd);

// whence: SEEK_SET/SEEK_CUR/SEEK_END 语义（0/1/2）；返回新位置或 <0
int64_t Seek(int fd, int64_t offset, int whence);

// 文件尺寸；未知（FSA 懒元数据）时内部先异步补全
int64_t Size(int fd);

// 返回实际读取字节数；0=EOF；<0=错误
int Read(int fd, void *buf, int len);

// 原生 callback 异步读。通常在提交调用返回后于主线程完成；上层必须把
// CPU continuation 提交到自己的 executor。buffer 必须保持有效。
using AsyncReadCallback = std::function<void(int)>;
void ReadAsync(int fd, void *buf, int len, AsyncReadCallback completion);

// overlay 写入；返回写入字节数或 <0
int Write(int fd, const void *buf, int len);

int Unlink(const char *path);
int MkDir(const char *path);

} // namespace VLFS

#endif // __EMSCRIPTEN__
#endif // VirtualLazyFSH

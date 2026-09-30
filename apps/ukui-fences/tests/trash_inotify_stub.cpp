// Isolated fault injection: unavailable watch API or a lost event queue.
#include <sys/inotify.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <atomic>

static std::atomic<int> trashFd{-1};
static std::atomic<bool> injected{false};
extern "C" int inotify_init1(int flags)
{
    const char *mode = std::getenv("UKUI_TRASH_TEST_INOTIFY");
    if (mode && std::strcmp(mode, "unavailable") == 0) { errno = ENOSYS; return -1; }
    return ::syscall(SYS_inotify_init1, flags);
}
extern "C" int inotify_add_watch(int fd, const char *path, uint32_t mask)
{
    const int result = ::syscall(SYS_inotify_add_watch, fd, path, mask);
    if (result >= 0 && std::strstr(path, "/Trash/info")) trashFd.store(fd);
    return result;
}
extern "C" int close(int fd)
{
    int expected = fd;
    trashFd.compare_exchange_strong(expected, -1);
    return ::syscall(SYS_close, fd);
}
extern "C" ssize_t read(int fd, void *buffer, size_t size)
{
    const ssize_t count = ::syscall(SYS_read, fd, buffer, size);
    const char *mode = std::getenv("UKUI_TRASH_TEST_INOTIFY");
    if (count > 0 && fd == trashFd.load() && size >= sizeof(inotify_event)
        && mode && std::strcmp(mode, "overflow") == 0 && !injected.exchange(true)) {
        // Drop the real create/write events and deliver the kernel overflow marker.
        const inotify_event event{-1, IN_Q_OVERFLOW, 0, 0};
        std::memcpy(buffer, &event, sizeof(event));
        const char message[] = "TRASH_INOTIFY_TEST overflow injected\n";
        ::syscall(SYS_write, STDERR_FILENO, message, sizeof(message) - 1);
        return sizeof(event);
    }
    return count;
}

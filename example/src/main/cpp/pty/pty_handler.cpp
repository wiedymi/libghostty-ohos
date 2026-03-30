#include "pty_handler.h"
#include <hilog/log.h>
#ifdef __OHOS__
#include <pty.h>
#else
#include <util.h>  // macOS
#endif
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <termios.h>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <string>
#include <vector>

#undef LOG_TAG
#define LOG_TAG "PTYHandler"

static std::string s_appBinDir;
static std::string s_homeDir;
static std::string s_shellInitPath;
static std::string s_preferredShellPath;

void PTYHandler::setAppBinDir(const char* path) {
    s_appBinDir = path ? path : "";
}

void PTYHandler::setHomeDir(const char* path) {
    s_homeDir = path ? path : "";
}

void PTYHandler::setPreferredShellPath(const char* path) {
    s_preferredShellPath = path ? path : "";
}

void PTYHandler::setShellInitPath(const char* path) {
    s_shellInitPath = path ? path : "";
}

const char* PTYHandler::findShell() {
    // Try shells in order of preference for HarmonyOS/OpenHarmony
    std::vector<std::string> shells;
    if (!s_preferredShellPath.empty()) {
        shells.emplace_back(s_preferredShellPath);
    }
    shells.emplace_back("/system/bin/sh");
    shells.emplace_back("/system/bin/mksh");
    shells.emplace_back("/vendor/bin/sh");
    shells.emplace_back("/bin/sh");
    shells.emplace_back("/bin/bash");

    for (const std::string& shell : shells) {
        if (access(shell.c_str(), X_OK) == 0) {
            OH_LOG_INFO(LOG_APP, "Found shell: %s", shell.c_str());
            return strdup(shell.c_str());
        }
        OH_LOG_INFO(LOG_APP, "Shell not found: %s", shell.c_str());
    }

    OH_LOG_ERROR(LOG_APP, "No shell found, using fallback /system/bin/sh");
    return "/system/bin/sh";  // Fallback - most likely on HarmonyOS
}

bool PTYHandler::openPTY(int& masterFd, int& writeFd, pid_t& childPid, int cols, int rows) {
    int master = -1;
    char slaveName[256] = {0};
    bool usePipes = false;
    int pipeToChild[2] = {-1, -1};
    int pipeFromChild[2] = {-1, -1};
    masterFd = -1;
    writeFd = -1;
    childPid = -1;

    struct winsize ws;
    ws.ws_col = cols;
    ws.ws_row = rows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    pid_t pid = forkpty(&master, slaveName, nullptr, &ws);
    if (pid < 0) {
        OH_LOG_ERROR(LOG_APP, "forkpty failed: %s - falling back to pipes", strerror(errno));
        usePipes = true;
    }

    if (usePipes) {
        // Fallback: use pipes instead of PTY
        OH_LOG_INFO(LOG_APP, "Using pipe-based fallback (limited terminal features)");

        if (pipe(pipeToChild) < 0) {
            OH_LOG_ERROR(LOG_APP, "pipe(toChild) failed: %s", strerror(errno));
            return false;
        }
        if (pipe(pipeFromChild) < 0) {
            OH_LOG_ERROR(LOG_APP, "pipe(fromChild) failed: %s", strerror(errno));
            ::close(pipeToChild[0]);
            ::close(pipeToChild[1]);
            return false;
        }
        pid = fork();
        if (pid < 0) {
            OH_LOG_ERROR(LOG_APP, "fork failed: %s", strerror(errno));
            ::close(pipeToChild[0]);
            ::close(pipeToChild[1]);
            ::close(pipeFromChild[0]);
            ::close(pipeFromChild[1]);
            return false;
        }
    } else {
        OH_LOG_INFO(LOG_APP, "PTY opened: master=%d, slave=%s", master, slaveName);
    }

    if (pid == 0) {
        // Child process
        if (usePipes) {
            // Close parent ends
            ::close(pipeToChild[1]);
            ::close(pipeFromChild[0]);

            // Redirect stdio
            dup2(pipeToChild[0], STDIN_FILENO);
            dup2(pipeFromChild[1], STDOUT_FILENO);
            dup2(pipeFromChild[1], STDERR_FILENO);

            ::close(pipeToChild[0]);
            ::close(pipeFromChild[1]);
        }

        // Set environment
        setenv("TERM", usePipes ? "dumb" : "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        unsetenv("TERM_PROGRAM");
        unsetenv("TERM_PROGRAM_VERSION");
        setenv("LANG", "en_US.UTF-8", 1);

        if (!s_homeDir.empty()) {
            setenv("HOME", s_homeDir.c_str(), 1);
            setenv("ZDOTDIR", s_homeDir.c_str(), 1);
            if (chdir(s_homeDir.c_str()) != 0) {
                OH_LOG_WARN(LOG_APP, "Failed to chdir to HOME %s: %s", s_homeDir.c_str(), strerror(errno));
            }
        } else {
            const char* home = getenv("HOME");
            if (!home || home[0] == '\0') {
                setenv("HOME", "/data/local/tmp", 1);
                if (chdir("/data/local/tmp") != 0) {
                    OH_LOG_WARN(LOG_APP, "Failed to chdir to fallback HOME: %s", strerror(errno));
                }
            }
        }

        const char* path = getenv("PATH");
        if (!path || path[0] == '\0') {
            setenv("PATH", "/system/bin:/bin:/usr/bin", 1);
        }
        if (!s_appBinDir.empty()) {
            std::string nextPath = s_appBinDir;
            if (path && path[0] != '\0') {
                nextPath += ":";
                nextPath += path;
            } else {
                nextPath += ":/system/bin:/bin:/usr/bin";
            }
            setenv("PATH", nextPath.c_str(), 1);
        }

        if (!s_shellInitPath.empty() && access(s_shellInitPath.c_str(), R_OK) == 0) {
            setenv("ENV", s_shellInitPath.c_str(), 1);
        }

        // Find and execute shell
        const char* shell = findShell();
        const char* shellName = strrchr(shell, '/');
        shellName = shellName ? shellName + 1 : shell;

        OH_LOG_INFO(LOG_APP, "Executing shell: %s", shell);
        if (std::strcmp(shellName, "fish") == 0) {
            const char* fishInit =
                "status job-control none; "
                "if test -r \"$HOME/.config/fish/config.fish\"; "
                "source \"$HOME/.config/fish/config.fish\"; "
                "end";
            execl(
                shell,
                shellName,
                "--interactive",
                "--private",
                "--no-config",
                "-C",
                fishInit,
                nullptr);
        } else {
            execl(shell, shellName, "-i", nullptr);
        }

        // If exec fails, try simple sh
        OH_LOG_ERROR(LOG_APP, "exec failed: %s, trying /system/bin/sh", strerror(errno));
        execl("/system/bin/sh", "sh", nullptr);

        // Still failed
        OH_LOG_ERROR(LOG_APP, "All exec attempts failed: %s", strerror(errno));
        _exit(1);
    }

    // Parent process
    if (usePipes) {
        ::close(pipeToChild[0]);
        ::close(pipeFromChild[1]);
        masterFd = pipeFromChild[0];
        writeFd = pipeToChild[1];

        // Set non-blocking
        int flags = fcntl(masterFd, F_GETFL);
        fcntl(masterFd, F_SETFL, flags | O_NONBLOCK);

        flags = fcntl(writeFd, F_GETFL);
        fcntl(writeFd, F_SETFL, flags | O_NONBLOCK);
    } else {
        // Set non-blocking
        int flags = fcntl(master, F_GETFL);
        fcntl(master, F_SETFL, flags | O_NONBLOCK);

        masterFd = master;
        writeFd = master;
    }

    childPid = pid;

    OH_LOG_INFO(LOG_APP, "Child process started: pid=%d, usePipes=%d", pid, usePipes);
    return true;
}

void PTYHandler::resize(int masterFd, int cols, int rows) {
    struct winsize ws;
    ws.ws_col = cols;
    ws.ws_row = rows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    OH_LOG_INFO(LOG_APP, "Resizing PTY master=%d to %d x %d", masterFd, cols, rows);
    if (ioctl(masterFd, TIOCSWINSZ, &ws) < 0) {
        OH_LOG_ERROR(LOG_APP, "TIOCSWINSZ failed: %s", strerror(errno));
    }
}

void PTYHandler::close(int masterFd, int writeFd, pid_t childPid) {
    if (masterFd >= 0) {
        ::close(masterFd);
    }
    if (writeFd >= 0 && writeFd != masterFd) {
        ::close(writeFd);
    }

    if (childPid > 0) {
        kill(childPid, SIGHUP);

        int status;
        int ret = waitpid(childPid, &status, WNOHANG);
        if (ret == 0) {
            // Process still running, give it a moment
            usleep(100000);  // 100ms
            ret = waitpid(childPid, &status, WNOHANG);
            if (ret == 0) {
                // Still running, force kill
                kill(childPid, SIGKILL);
                waitpid(childPid, &status, 0);
            }
        }
    }
}

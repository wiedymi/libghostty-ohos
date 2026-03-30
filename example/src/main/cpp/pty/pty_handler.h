#pragma once

#include <sys/types.h>

class PTYHandler {
public:
    static void setAppBinDir(const char* path);
    static void setHomeDir(const char* path);
    static void setPreferredShellPath(const char* path);
    static void setShellInitPath(const char* path);
    static bool openPTY(int& masterFd, int& writeFd, pid_t& childPid, int cols, int rows);
    static void resize(int masterFd, int cols, int rows);
    static void close(int masterFd, int writeFd, pid_t childPid);

private:
    static const char* findShell();
};

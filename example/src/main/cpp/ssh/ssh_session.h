#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>

struct _LIBSSH2_SESSION;
struct _LIBSSH2_CHANNEL;

class SSHSession {
public:
    SSHSession();
    ~SSHSession();

    bool connect(const std::string& host, int port, const std::string& user, const std::string& password,
                 int cols, int rows, std::string& error);
    void disconnect();

    bool isConnected() const { return m_connected.load(); }
    bool write(const char* data, size_t len);
    void resize(int cols, int rows);

    void setOutputCallback(std::function<void(const std::string&)> callback) {
        m_outputCallback = callback;
    }

private:
    void readLoop();

    int m_socketFd;
    _LIBSSH2_SESSION* m_session;
    _LIBSSH2_CHANNEL* m_channel;
    std::atomic<bool> m_connected;
    std::atomic<bool> m_running;
    std::thread m_readThread;
    std::mutex m_ioMutex;
    std::function<void(const std::string&)> m_outputCallback;
};

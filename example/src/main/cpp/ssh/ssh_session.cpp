#include "ssh_session.h"
#include <libssh2.h>
#include <hilog/log.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <sys/select.h>

#undef LOG_TAG
#define LOG_TAG "SSHSession"

namespace {
bool SetRemoteEnv(_LIBSSH2_CHANNEL* channel, const char* key, const char* value)
{
    if (!channel || !key || !value) {
        return false;
    }
    const int rc = libssh2_channel_setenv_ex(
        channel,
        key,
        static_cast<unsigned int>(std::strlen(key)),
        value,
        static_cast<unsigned int>(std::strlen(value)));
    return rc == 0;
}
}

SSHSession::SSHSession()
    : m_socketFd(-1), m_session(nullptr), m_channel(nullptr),
      m_connected(false), m_running(false) {}

SSHSession::~SSHSession() {
    disconnect();
}

bool SSHSession::connect(const std::string& host, int port, const std::string& user,
                         const std::string& password, int cols, int rows, std::string& error) {
    disconnect();

    if (libssh2_init(0) != 0) {
        error = "libssh2_init failed";
        return false;
    }

    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = nullptr;
    const std::string portString = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), portString.c_str(), &hints, &result);
    if (rc != 0 || !result) {
        error = "Failed to resolve host";
        libssh2_exit();
        return false;
    }

    for (struct addrinfo* it = result; it; it = it->ai_next) {
        m_socketFd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (m_socketFd < 0) {
            continue;
        }
        if (::connect(m_socketFd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }
        ::close(m_socketFd);
        m_socketFd = -1;
    }
    freeaddrinfo(result);

    if (m_socketFd < 0) {
        error = "Failed to connect socket";
        libssh2_exit();
        return false;
    }

    m_session = libssh2_session_init();
    if (!m_session) {
        error = "Failed to create SSH session";
        ::close(m_socketFd);
        m_socketFd = -1;
        libssh2_exit();
        return false;
    }

    libssh2_session_set_blocking(m_session, 1);

    rc = libssh2_session_handshake(m_session, m_socketFd);
    if (rc != 0) {
        error = "SSH handshake failed";
        disconnect();
        return false;
    }

    // Initial implementation skips host-key verification.
    rc = libssh2_userauth_password(m_session, user.c_str(), password.c_str());
    if (rc != 0) {
        error = "SSH password authentication failed";
        disconnect();
        return false;
    }

    m_channel = libssh2_channel_open_session(m_session);
    if (!m_channel) {
        error = "Failed to open SSH channel";
        disconnect();
        return false;
    }

    rc = libssh2_channel_request_pty_ex(m_channel, "xterm-256color", 14, nullptr, 0,
                                        cols, rows, 0, 0);
    if (rc != 0) {
        error = "Failed to request remote PTY";
        disconnect();
        return false;
    }

    SetRemoteEnv(m_channel, "TERM", "xterm-256color");
    SetRemoteEnv(m_channel, "COLORTERM", "truecolor");
    SetRemoteEnv(m_channel, "TERM_PROGRAM", "ghostty");
    SetRemoteEnv(m_channel, "TERM_PROGRAM_VERSION", "1.0.0");

    const char* loginShellCommand =
        "env TERM=xterm-256color "
        "COLORTERM=truecolor "
        "TERM_PROGRAM=ghostty "
        "TERM_PROGRAM_VERSION=1.0.0 "
        "/bin/sh -lc 'exec \"${SHELL:-/bin/sh}\" -l'";
    rc = libssh2_channel_exec(m_channel, loginShellCommand);
    if (rc != 0) {
        error = "Failed to start remote shell";
        disconnect();
        return false;
    }

    libssh2_session_set_blocking(m_session, 0);

    m_connected = true;
    m_running = true;
    m_readThread = std::thread(&SSHSession::readLoop, this);
    return true;
}

void SSHSession::disconnect() {
    m_running = false;

    if (m_readThread.joinable()) {
        m_readThread.join();
    }

    std::lock_guard<std::mutex> lock(m_ioMutex);

    if (m_channel) {
        libssh2_channel_close(m_channel);
        libssh2_channel_free(m_channel);
        m_channel = nullptr;
    }

    if (m_session) {
        libssh2_session_disconnect(m_session, "Normal Shutdown");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }

    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }

    if (m_connected.load()) {
        libssh2_exit();
    }
    m_connected = false;
}

bool SSHSession::write(const char* data, size_t len) {
    if (!m_connected || !m_channel || len == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_ioMutex);
    size_t offset = 0;
    while (offset < len) {
        ssize_t written = libssh2_channel_write(m_channel, data + offset, len - offset);
        if (written == LIBSSH2_ERROR_EAGAIN) {
            break;
        }
        if (written < 0) {
            return false;
        }
        offset += static_cast<size_t>(written);
    }
    return true;
}

void SSHSession::resize(int cols, int rows) {
    if (!m_connected || !m_channel) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_ioMutex);
    libssh2_channel_request_pty_size_ex(m_channel, cols, rows, 0, 0);
}

void SSHSession::readLoop() {
    char buffer[4096];

    while (m_running && m_connected && m_channel) {
        std::string outputChunk;
        bool reachedEof = false;
        {
            std::lock_guard<std::mutex> lock(m_ioMutex);
            ssize_t n = libssh2_channel_read(m_channel, buffer, sizeof(buffer));
            if (n > 0) {
                outputChunk.assign(buffer, static_cast<size_t>(n));
            } else if (n == 0 && libssh2_channel_eof(m_channel)) {
                reachedEof = true;
            }
        }

        if (!outputChunk.empty()) {
            if (m_outputCallback) {
                m_outputCallback(outputChunk);
            }
            continue;
        }

        if (reachedEof) {
            break;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(m_socketFd, &readfds);
        struct timeval tv = {0, 16000};
        select(m_socketFd + 1, &readfds, nullptr, nullptr, &tv);
    }

    m_connected = false;
}

#include <hilog/log.h>
#include <napi/native_api.h>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sstream>
#include <vector>
#include "pty/pty_handler.h"
#include "ssh/ssh_session.h"

#undef LOG_TAG
#define LOG_TAG "example_driver"

namespace {

constexpr int DEFAULT_COLS = 120;
constexpr int DEFAULT_ROWS = 40;
constexpr const char* SSH_TRIGGER_PREFIX = "\x1b]633;example-ssh;";
constexpr const char* PROMPT_TRIGGER_PREFIX = "\x1b]633;example-prompt;";
constexpr const char* APP_BIN_DIR = "/data/app/bin";
constexpr const char* BUNDLED_SHELL_NAME = "fish";
constexpr const char* BUNDLED_PROMPT_NAME = "starship";
constexpr int MAX_SEARCH_DEPTH = 6;
constexpr size_t STARTUP_LOG_CHUNK_LIMIT = 8;

std::string SanitizeLogChunk(const std::string& chunk)
{
    std::string sanitized;
    sanitized.reserve(chunk.size());
    for (unsigned char ch : chunk) {
        if (ch == '\r') {
            sanitized += "\\r";
            continue;
        }
        if (ch == '\n') {
            sanitized += "\\n";
            continue;
        }
        if (ch == '\x1b') {
            sanitized += "\\e";
            continue;
        }
        if (ch < 0x20 || ch == 0x7f) {
            char buffer[5];
            std::snprintf(buffer, sizeof(buffer), "\\x%02X", ch);
            sanitized += buffer;
            continue;
        }
        sanitized.push_back(static_cast<char>(ch));
    }
    return sanitized;
}

std::string Trim(const std::string& value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

bool StartsWith(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

napi_value Initialize(napi_env env, napi_callback_info info);
extern "C" void RegisterExampleDriverModule(void);

std::string Dirname(const std::string& path)
{
    const size_t slash = path.rfind('/');
    if (slash == std::string::npos) {
        return {};
    }
    return path.substr(0, slash);
}

std::string ResolveLoadedLibraryDir(const void* symbol)
{
    Dl_info info {};
    if (dladdr(symbol, &info) == 0 || info.dli_fname == nullptr) {
        return {};
    }
    return Dirname(info.dli_fname);
}

bool IsExecutableFile(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(path.c_str(), X_OK) == 0;
}

std::string JoinPath(const std::string& left, const std::string& right)
{
    if (left.empty()) {
        return right;
    }
    if (left.back() == '/') {
        return left + right;
    }
    return left + "/" + right;
}

std::string SearchForExecutable(const std::string& root, const std::string& fileName, int depthRemaining)
{
    if (depthRemaining < 0) {
        return {};
    }

    DIR* dir = opendir(root.c_str());
    if (dir == nullptr) {
        return {};
    }

    std::string found;
    while (dirent* entry = readdir(dir)) {
        const char* name = entry->d_name;
        if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) {
            continue;
        }

        const std::string childPath = JoinPath(root, name);
        struct stat st {};
        if (lstat(childPath.c_str(), &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            found = SearchForExecutable(childPath, fileName, depthRemaining - 1);
            if (!found.empty()) {
                break;
            }
            continue;
        }

        if (std::strcmp(name, fileName.c_str()) == 0 && access(childPath.c_str(), X_OK) == 0) {
            found = childPath;
            break;
        }
    }

    closedir(dir);
    return found;
}

std::string ResolveBundledExecutablePath(
    const std::string& loadedLibraryDir, const std::string& executableName)
{
    const std::vector<std::string> directCandidates = {
        "/data/app/bin/" + executableName,
        "/data/app/el1/bundle/bin/" + executableName,
        "/data/storage/el1/bundle/bin/" + executableName,
        JoinPath(loadedLibraryDir, executableName)
    };
    for (const auto& candidate : directCandidates) {
        if (!candidate.empty() && access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
    }

    const std::vector<std::string> searchRoots = {
        "/data/app",
        "/data/storage/el1/bundle",
        "/data/storage/el2/base",
        "/data/service/hnp",
        loadedLibraryDir
    };
    for (const auto& root : searchRoots) {
        if (root.empty()) {
            continue;
        }
        const std::string found = SearchForExecutable(root, executableName, MAX_SEARCH_DEPTH);
        if (!found.empty()) {
            return found;
        }
    }

    return {};
}

struct SSHRequest {
    std::string user;
    std::string host;
    int port = 22;
};

bool ParseSshCommand(const std::string& line, SSHRequest& request)
{
    const std::string trimmed = Trim(line);
    if (!StartsWith(trimmed, "ssh ")) {
        return false;
    }

    std::string target = Trim(trimmed.substr(4));
    if (target.empty()) {
        return false;
    }

    const size_t atPos = target.find('@');
    if (atPos != std::string::npos) {
        request.user = target.substr(0, atPos);
        target = target.substr(atPos + 1);
    } else {
        request.user = "root";
    }

    size_t portPos = target.rfind(':');
    if (portPos != std::string::npos && portPos + 1 < target.size()) {
        const std::string portString = target.substr(portPos + 1);
        bool numeric = !portString.empty();
        for (char ch : portString) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                numeric = false;
                break;
            }
        }
        if (numeric) {
            request.port = std::stoi(portString);
            target = target.substr(0, portPos);
        }
    }

    request.host = Trim(target);
    return !request.host.empty();
}

class ExampleDriver {
public:
    ExampleDriver() = default;

    ~ExampleDriver()
    {
        ClearOutputCallback();
        Stop();
    }

    void Initialize(const std::string& filesDir)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_filesDir = filesDir;
        const std::string fallbackBinDir =
            ResolveLoadedLibraryDir(reinterpret_cast<const void*>(&RegisterExampleDriverModule));
        m_shellPath = ResolveBundledExecutablePath(fallbackBinDir, BUNDLED_SHELL_NAME);
        m_promptPath = ResolveBundledExecutablePath(fallbackBinDir, BUNDLED_PROMPT_NAME);
        if (!m_shellPath.empty()) {
            m_binDir = Dirname(m_shellPath);
        } else if (!fallbackBinDir.empty()) {
            m_binDir = fallbackBinDir;
            m_shellPath = JoinPath(m_binDir, BUNDLED_SHELL_NAME);
            m_promptPath = JoinPath(m_binDir, BUNDLED_PROMPT_NAME);
        } else {
            m_binDir = APP_BIN_DIR;
            m_shellPath = JoinPath(m_binDir, BUNDLED_SHELL_NAME);
            m_promptPath = JoinPath(m_binDir, BUNDLED_PROMPT_NAME);
        }
    }

    bool Start(int cols, int rows)
    {
        Stop();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cols = cols > 0 ? cols : DEFAULT_COLS;
            m_rows = rows > 0 ? rows : DEFAULT_ROWS;
            m_mode = Mode::LocalShell;
            m_currentLine.clear();
            m_pendingPassword.clear();
            m_startupLogChunks = 0;
        }

        WriteShellInitFile();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_workingDir = !m_filesDir.empty() ? m_filesDir : "/data/local/tmp";
        }

        PTYHandler::setAppBinDir(m_commandPath.c_str());
        PTYHandler::setHomeDir(m_filesDir.c_str());
        PTYHandler::setPreferredShellPath(m_shellPath.c_str());
        PTYHandler::setShellInitPath(m_shellInitPath.c_str());

        int masterFd = -1;
        int writeFd = -1;
        pid_t childPid = -1;
        if (!PTYHandler::openPTY(masterFd, writeFd, childPid, m_cols, m_rows)) {
            PushOutput("Failed to start local shell.\r\n");
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_masterFd = masterFd;
            m_writeFd = writeFd;
            m_childPid = childPid;
            m_running = true;
            m_lastExitStatus = 0;
        }
        m_localReadThread = std::thread(&ExampleDriver::LocalReadLoop, this);

        if (access(m_shellPath.c_str(), X_OK) == 0) {
            PushOutput("Example driver ready. Bundled fish is active at " + m_shellPath + ".\r\n");
        } else {
            PushOutput("Example driver ready. Bundled fish not found at " + m_shellPath +
                ", using fallback shell.\r\n");
        }
        return true;
    }

    void Stop()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
        }

        if (m_sshSession) {
            m_sshSession->disconnect();
            m_sshSession.reset();
        }

        if (m_localReadThread.joinable()) {
            m_localReadThread.join();
        }

        int masterFd = -1;
        int writeFd = -1;
        pid_t childPid = -1;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            masterFd = m_masterFd;
            writeFd = m_writeFd;
            childPid = m_childPid;
            m_masterFd = -1;
            m_writeFd = -1;
            m_childPid = -1;
            m_mode = Mode::LocalShell;
            m_currentLine.clear();
            m_pendingPassword.clear();
            m_startupLogChunks = 0;
        }
        PTYHandler::close(masterFd, writeFd, childPid);
    }

    void Write(const std::string& data)
    {
        if (data.empty()) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        switch (m_mode) {
            case Mode::LocalShell:
                WriteToLocalShellLocked(data.c_str(), data.size());
                break;
            case Mode::AwaitingPassword:
                HandlePasswordInputLocked(data);
                break;
            case Mode::RemoteSsh:
                if (m_sshSession) {
                    m_sshSession->write(data.c_str(), data.size());
                }
                break;
        }
    }

    bool WriteRaw(const char* data, size_t length)
    {
        if (!data || length == 0 || !m_running.load()) {
            return false;
        }

        Write(std::string(data, length));
        return true;
    }

    std::string DrainOutput()
    {
        std::lock_guard<std::mutex> lock(m_outputMutex);
        std::string result;
        result.swap(m_outputBuffer);
        return result;
    }

    void SetOutputCallback(napi_env env, napi_value callback)
    {
        napi_threadsafe_function previous = nullptr;
        napi_threadsafe_function next = nullptr;

        if (callback != nullptr) {
            napi_value resourceName;
            napi_create_string_utf8(env, "exampleDriverOutput", NAPI_AUTO_LENGTH, &resourceName);
            napi_create_threadsafe_function(
                env,
                callback,
                nullptr,
                resourceName,
                0,
                1,
                nullptr,
                nullptr,
                nullptr,
                [](napi_env callbackEnv, napi_value jsCallback, void*, void* data) {
                    std::unique_ptr<std::string> output(static_cast<std::string*>(data));
                    if (!callbackEnv || !jsCallback || !output || output->empty()) {
                        return;
                    }

                    napi_value undefined;
                    napi_get_undefined(callbackEnv, &undefined);
                    napi_value argv[1];
                    napi_create_string_utf8(
                        callbackEnv,
                        output->c_str(),
                        output->size(),
                        &argv[0]);
                    napi_call_function(callbackEnv, undefined, jsCallback, 1, argv, nullptr);
                },
                &next);
        }

        std::string buffered;
        {
            std::lock_guard<std::mutex> lock(m_outputMutex);
            previous = m_outputTsfn;
            m_outputTsfn = next;
            if (m_outputTsfn != nullptr && !m_outputBuffer.empty()) {
                buffered.swap(m_outputBuffer);
            }
        }

        if (previous != nullptr) {
            napi_release_threadsafe_function(previous, napi_tsfn_abort);
        }
        if (m_outputTsfn != nullptr && !buffered.empty()) {
            EmitOutput(buffered);
        }
    }

    void ClearOutputCallback()
    {
        napi_threadsafe_function previous = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_outputMutex);
            previous = m_outputTsfn;
            m_outputTsfn = nullptr;
        }
        if (previous != nullptr) {
            napi_release_threadsafe_function(previous, napi_tsfn_abort);
        }
    }

    void Resize(int cols, int rows)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (cols > 0) {
            m_cols = cols;
        }
        if (rows > 0) {
            m_rows = rows;
        }
        OH_LOG_INFO(LOG_APP, "Example driver resize to %d x %d", m_cols, m_rows);
        if (m_masterFd >= 0) {
            PTYHandler::resize(m_masterFd, m_cols, m_rows);
            if (m_childPid > 0) {
                kill(m_childPid, SIGWINCH);
            }
        }
        if (m_sshSession && m_sshSession->isConnected()) {
            m_sshSession->resize(m_cols, m_rows);
        }
    }

private:
    enum class Mode {
        LocalShell,
        AwaitingPassword,
        RemoteSsh,
    };

    void LocalReadLoop()
    {
        char buffer[4096];
        while (true) {
            int masterFd = -1;
            Mode mode = Mode::LocalShell;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_running || m_masterFd < 0) {
                    break;
                }
                masterFd = m_masterFd;
                mode = m_mode;
            }

            const ssize_t count = read(masterFd, buffer, sizeof(buffer));
            if (count > 0) {
                MaybeLogStartupChunk(std::string(buffer, static_cast<size_t>(count)));
                if (mode == Mode::LocalShell) {
                    PushProcessedLocalOutput(std::string(buffer, static_cast<size_t>(count)));
                }
                continue;
            }

            if (count == 0) {
                ReportLocalShellExit();
                break;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(16000);
                continue;
            }

            break;
        }
    }

    void ReportLocalShellExit()
    {
        pid_t childPid = -1;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            childPid = m_childPid;
        }

        if (childPid <= 0) {
            PushOutput("\r\n[local shell disconnected]\r\n");
            return;
        }

        int status = 0;
        const pid_t waited = waitpid(childPid, &status, WNOHANG);
        if (waited == 0) {
            PushOutput("\r\n[local shell stopped producing output]\r\n");
            return;
        }
        if (waited < 0) {
            PushOutput("\r\n[local shell ended; waitpid failed]\r\n");
            return;
        }

        if (WIFEXITED(status)) {
            PushOutput("\r\n[local shell exited with code " + std::to_string(WEXITSTATUS(status)) + "]\r\n");
            return;
        }
        if (WIFSIGNALED(status)) {
            PushOutput("\r\n[local shell terminated by signal " + std::to_string(WTERMSIG(status)) + "]\r\n");
            return;
        }

        PushOutput("\r\n[local shell ended]\r\n");
    }

    void HandleLocalInputLocked(const std::string& data)
    {
        for (char ch : data) {
            if (ch == '\r' || ch == '\n') {
                SubmitLocalLineLocked();
                continue;
            }
            if (ch == '\x7f' || ch == '\b') {
                if (!m_currentLine.empty()) {
                    m_currentLine.pop_back();
                    PushOutput("\b \b");
                }
                continue;
            }
            if (static_cast<unsigned char>(ch) < 0x20) {
                continue;
            }
            m_currentLine.push_back(ch);
            PushOutput(std::string(1, ch));
        }
    }

    void SubmitLocalLineLocked()
    {
        SSHRequest request;
        if (ParseSshCommand(m_currentLine, request)) {
            PushOutput("\r\nPassword: ");
            m_pendingRequest = request;
            m_pendingPassword.clear();
            m_currentLine.clear();
            m_mode = Mode::AwaitingPassword;
            return;
        }

        const std::string command = m_currentLine;
        m_currentLine.clear();
        PushOutput("\r\n");
        if (command.empty()) {
            RenderPromptLocked();
            return;
        }
        ExecuteLocalCommandLocked(command);
        RenderPromptLocked();
    }

    void HandlePasswordInputLocked(const std::string& data)
    {
        for (char ch : data) {
            if (ch == '\x03') {
                m_pendingPassword.clear();
                m_currentLine.clear();
                m_mode = Mode::LocalShell;
                PushOutput("^C\r\n");
                WriteToLocalShellLocked("\n", 1);
                continue;
            }
            if (ch == '\r' || ch == '\n') {
                PushOutput("\r\n");
                FinishSshConnectLocked();
                continue;
            }
            if (ch == '\x7f' || ch == '\b') {
                if (!m_pendingPassword.empty()) {
                    m_pendingPassword.pop_back();
                    PushOutput("\b \b");
                }
                continue;
            }
            if (static_cast<unsigned char>(ch) < 0x20) {
                continue;
            }
            m_pendingPassword.push_back(ch);
            PushOutput("*");
        }
    }

    void FinishSshConnectLocked()
    {
        auto session = std::make_unique<SSHSession>();
        session->setOutputCallback([this](const std::string& output) {
            PushOutput(output);
        });

        std::string error;
        if (!session->connect(
                m_pendingRequest.host,
                m_pendingRequest.port,
                m_pendingRequest.user,
                m_pendingPassword,
                m_cols,
                m_rows,
                error)) {
            PushOutput("SSH connection failed: " + error + "\r\n");
            m_mode = Mode::LocalShell;
            m_pendingPassword.clear();
            m_currentLine.clear();
            WriteToLocalShellLocked("\n", 1);
            return;
        }

        m_sshSession = std::move(session);
        m_mode = Mode::RemoteSsh;
        m_pendingPassword.clear();
        m_currentLine.clear();
    }

    void WriteToLocalShellLocked(const char* data, size_t length)
    {
        if (m_writeFd < 0 || !data || length == 0) {
            return;
        }
        size_t offset = 0;
        while (offset < length) {
            const ssize_t written = write(m_writeFd, data + offset, length - offset);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            offset += static_cast<size_t>(written);
        }
    }

    void PushOutput(const std::string& data)
    {
        if (data.empty()) {
            return;
        }
        EmitOutput(data);
    }

    void EmitOutput(const std::string& data)
    {
        napi_threadsafe_function tsfn = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_outputMutex);
            if (m_outputTsfn == nullptr) {
                m_outputBuffer += data;
                return;
            }
            tsfn = m_outputTsfn;
        }

        auto* output = new std::string(data);
        const napi_status status = napi_call_threadsafe_function(tsfn, output, napi_tsfn_nonblocking);
        if (status != napi_ok) {
            delete output;
            std::lock_guard<std::mutex> lock(m_outputMutex);
            m_outputBuffer += data;
        }
    }

    void MaybeLogStartupChunk(const std::string& chunk)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_startupLogChunks >= STARTUP_LOG_CHUNK_LIMIT || chunk.empty()) {
            return;
        }

        ++m_startupLogChunks;
        const std::string sanitized = SanitizeLogChunk(chunk);
        OH_LOG_INFO(
            LOG_APP,
            "Local PTY chunk[%{public}zu]: %{public}s",
            m_startupLogChunks,
            sanitized.c_str());
    }

    void PushProcessedLocalOutput(const std::string& chunk)
    {
        if (chunk.empty()) {
            return;
        }

        std::string output = m_pendingEscapeBuffer + chunk;
        m_pendingEscapeBuffer.clear();
        std::string visible;
        size_t cursor = 0;

        while (cursor < output.size()) {
            const size_t trigger = output.find(SSH_TRIGGER_PREFIX, cursor);
            if (trigger == std::string::npos) {
                visible.append(output, cursor, std::string::npos);
                break;
            }

            visible.append(output, cursor, trigger - cursor);
            const size_t payloadStart = trigger + std::strlen(SSH_TRIGGER_PREFIX);
            const size_t terminator = output.find('\a', payloadStart);
            if (terminator == std::string::npos) {
                m_pendingEscapeBuffer = output.substr(trigger);
                break;
            }

            HandleSshTrigger(output.substr(payloadStart, terminator - payloadStart));
            if (IsAwaitingPassword()) {
                cursor = terminator + 1;
                break;
            }
            cursor = terminator + 1;
        }

        output.swap(visible);
        visible.clear();
        cursor = 0;

        while (cursor < output.size()) {
            const size_t trigger = output.find(PROMPT_TRIGGER_PREFIX, cursor);
            if (trigger == std::string::npos) {
                visible.append(output, cursor, std::string::npos);
                break;
            }

            visible.append(output, cursor, trigger - cursor);
            const size_t payloadStart = trigger + std::strlen(PROMPT_TRIGGER_PREFIX);
            const size_t terminator = output.find('\a', payloadStart);
            if (terminator == std::string::npos) {
                m_pendingEscapeBuffer = output.substr(trigger);
                break;
            }

            HandlePromptTrigger();
            cursor = terminator + 1;
        }

        PushOutput(visible);
    }

    bool IsAwaitingPassword()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_mode == Mode::AwaitingPassword;
    }

    void HandleSshTrigger(const std::string& payload)
    {
        SSHRequest request;
        if (!ParseSshCommand("ssh " + Trim(payload), request)) {
            PushOutput("Invalid SSH target.\r\n");
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_mode != Mode::LocalShell) {
            return;
        }
        m_pendingRequest = request;
        m_pendingPassword.clear();
        m_currentLine.clear();
        m_mode = Mode::AwaitingPassword;
        PushOutput("\r\nPassword: ");
    }

    void HandlePromptTrigger()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_mode == Mode::LocalShell) {
            m_currentLine.clear();
            RenderPromptLocked();
        }
    }

    void ExecuteLocalCommandLocked(const std::string& command)
    {
        const std::string trimmed = Trim(command);
        if (trimmed == "clear") {
            PushOutput("\033c[fish-start]\r\n");
            m_lastExitStatus = 0;
            return;
        }
        if (trimmed == "cd" || StartsWith(trimmed, "cd ")) {
            std::string target = trimmed == "cd" ? m_filesDir : Trim(trimmed.substr(3));
            if (target.empty()) {
                target = m_filesDir;
            }
            if (!target.empty() && chdir(target.c_str()) == 0) {
                char cwd[PATH_MAX];
                if (getcwd(cwd, sizeof(cwd)) != nullptr) {
                    m_workingDir = cwd;
                } else {
                    m_workingDir = target;
                }
                m_lastExitStatus = 0;
            } else {
                PushOutput("cd: " + target + ": " + std::strerror(errno) + "\r\n");
                m_lastExitStatus = 1;
            }
            return;
        }

        int outputPipe[2] = {-1, -1};
        if (pipe(outputPipe) != 0) {
            PushOutput("pipe failed\r\n");
            m_lastExitStatus = 1;
            return;
        }

        pid_t pid = fork();
        if (pid < 0) {
            ::close(outputPipe[0]);
            ::close(outputPipe[1]);
            PushOutput("fork failed\r\n");
            m_lastExitStatus = 1;
            return;
        }

        if (pid == 0) {
            dup2(outputPipe[1], STDOUT_FILENO);
            dup2(outputPipe[1], STDERR_FILENO);
            ::close(outputPipe[0]);
            ::close(outputPipe[1]);

            if (!m_workingDir.empty()) {
                chdir(m_workingDir.c_str());
            }
            if (!m_filesDir.empty()) {
                setenv("HOME", m_filesDir.c_str(), 1);
            }
            if (!m_commandPath.empty()) {
                setenv("PATH", m_commandPath.c_str(), 1);
            }
            setenv("TERM", "xterm-256color", 1);
            setenv("COLORTERM", "truecolor", 1);

            const char* shell = access(m_shellPath.c_str(), X_OK) == 0 ? m_shellPath.c_str() : "/system/bin/sh";
            const char* shellName = strrchr(shell, '/');
            shellName = shellName ? shellName + 1 : shell;

            if (std::strcmp(shellName, "fish") == 0) {
                const std::string script =
                    "function uname; echo HarmonyOS; end; "
                    "function fastfetch; command fastfetch --logo-type builtin --logo harmonyos $argv; end; "
                    "function ssh; printf '\\033]633;example-ssh;%s\\a' \"$argv\"; end; "
                    "alias ll='ls -l'; "
                    "alias clear='printf \"\\\\033c\"'; "
                    + command;
                execl(shell, shellName, "--private", "--no-config", "-c", script.c_str(), nullptr);
            } else {
                execl(shell, shellName, "-lc", command.c_str(), nullptr);
            }
            _exit(127);
        }

        ::close(outputPipe[1]);
        std::string result;
        char buffer[4096];
        while (true) {
            const ssize_t count = read(outputPipe[0], buffer, sizeof(buffer));
            if (count > 0) {
                result.append(buffer, static_cast<size_t>(count));
                continue;
            }
            if (count == 0) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        ::close(outputPipe[0]);

        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            m_lastExitStatus = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            m_lastExitStatus = 128 + WTERMSIG(status);
        } else {
            m_lastExitStatus = 1;
        }
        PushProcessedLocalOutput(result);
    }

    void RenderPromptLocked()
    {
        if (m_mode != Mode::LocalShell) {
            return;
        }
        if (access(m_promptPath.c_str(), X_OK) != 0) {
            PushOutput("> ");
            return;
        }

        int outputPipe[2] = {-1, -1};
        if (pipe(outputPipe) != 0) {
            PushOutput("> ");
            return;
        }

        pid_t pid = fork();
        if (pid < 0) {
            ::close(outputPipe[0]);
            ::close(outputPipe[1]);
            PushOutput("> ");
            return;
        }

        if (pid == 0) {
            dup2(outputPipe[1], STDOUT_FILENO);
            dup2(outputPipe[1], STDERR_FILENO);
            ::close(outputPipe[0]);
            ::close(outputPipe[1]);

            if (!m_workingDir.empty()) {
                chdir(m_workingDir.c_str());
            }
            if (!m_filesDir.empty()) {
                setenv("HOME", m_filesDir.c_str(), 1);
            }
            if (!m_commandPath.empty()) {
                setenv("PATH", m_commandPath.c_str(), 1);
            }
            setenv("TERM", "xterm-256color", 1);
            setenv("COLORTERM", "truecolor", 1);
            setenv("STARSHIP_SHELL", "fish", 1);

            const std::string statusArg = "--status=" + std::to_string(m_lastExitStatus);
            const std::string widthArg = "--terminal-width=" + std::to_string(m_cols > 0 ? m_cols : DEFAULT_COLS);
            const std::string pathArg = !m_workingDir.empty() ? m_workingDir : ".";
            const std::string shlvlArg = "--shlvl=1";

            execl(
                m_promptPath.c_str(),
                "starship",
                "prompt",
                statusArg.c_str(),
                widthArg.c_str(),
                "--path",
                pathArg.c_str(),
                "--logical-path",
                pathArg.c_str(),
                shlvlArg.c_str(),
                nullptr);
            _exit(127);
        }

        ::close(outputPipe[1]);
        std::string prompt;
        char buffer[1024];
        while (true) {
            const ssize_t count = read(outputPipe[0], buffer, sizeof(buffer));
            if (count > 0) {
                prompt.append(buffer, static_cast<size_t>(count));
                continue;
            }
            if (count == 0) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        ::close(outputPipe[0]);

        int status = 0;
        waitpid(pid, &status, 0);
        if (prompt.empty()) {
            PushOutput("> ");
            return;
        }
        PushOutput(prompt);
    }

    void WriteShellInitFile()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_filesDir.empty()) {
            m_shellInitPath.clear();
            return;
        }

        m_shellInitPath = m_filesDir + "/example_shell_init.sh";
        FILE* file = std::fopen(m_shellInitPath.c_str(), "w");
        if (!file) {
            OH_LOG_WARN(LOG_APP, "Failed to write shell init file");
            m_shellInitPath.clear();
            return;
        }

        m_commandPath =
            m_binDir + ":/system/bin:/system/xbin:/vendor/bin:/bin:/usr/bin";

        const std::string script =
            "export PATH=\"" + m_commandPath + "\"\n"
            "export TERM=xterm-256color\n"
            "export COLORTERM=truecolor\n";
        std::fwrite(script.data(), 1, script.size(), file);
        std::fclose(file);
        chmod(m_shellInitPath.c_str(), 0700);

        const std::string fishConfigDir = m_filesDir + "/.config/fish";
        const std::string configDir = m_filesDir + "/.config";
        mkdir(configDir.c_str(), 0700);
        mkdir(fishConfigDir.c_str(), 0700);
        const std::string fishConfigPath = fishConfigDir + "/config.fish";
        const std::string starshipConfigPath = configDir + "/starship.toml";
        file = std::fopen(fishConfigPath.c_str(), "w");
        if (!file) {
            OH_LOG_WARN(LOG_APP, "Failed to write fish config");
            return;
        }

        std::string fishConfig =
            "set -gx PATH \"" + m_binDir + "\" /system/bin /system/xbin /vendor/bin /bin /usr/bin\n"
            "set -gx SHELL \"" + m_shellPath + "\"\n"
            "set -gx TERM xterm-256color\n"
            "set -gx COLORTERM truecolor\n"
            "set -g fish_greeting\n"
            "status job-control none\n"
            "function uname\n"
            "  echo HarmonyOS\n"
            "end\n"
            "function fastfetch\n"
            "  command fastfetch --logo-type builtin --logo harmonyos $argv\n"
            "end\n"
            "alias ll='ls -l'\n"
            "alias clear='printf \"\\033c\"'\n"
            "function ssh\n"
            "  printf '\\033]633;example-ssh;%s\\a' \"$argv\"\n"
            "end\n";

        const std::string builtinPrompt =
            "function fish_prompt\n"
            "  set -l last_status $status\n"
            "  set -l cwd (prompt_pwd)\n"
            "  if test $last_status -ne 0\n"
            "    set_color red\n"
            "    printf '[%s] ' $last_status\n"
            "  end\n"
            "  set_color cyan\n"
            "  printf '%s ' $cwd\n"
            "  set_color green\n"
            "  printf '❯ '\n"
            "  set_color normal\n"
            "end\n";

        if (!m_promptPath.empty()) {
            fishConfig += "if test -x \"" + m_promptPath + "\"\n";
            fishConfig += "  set -gx STARSHIP_CONFIG \"" + starshipConfigPath + "\"\n";
            fishConfig += "  " + m_promptPath + " init fish | source\n";
            fishConfig += "else\n";
            fishConfig += builtinPrompt;
            fishConfig += "end\n";
            fishConfig += "function use_starship\n";
            fishConfig += "  if test -x \"" + m_promptPath + "\"\n";
            fishConfig += "    set -gx STARSHIP_CONFIG \"" + starshipConfigPath + "\"\n";
            fishConfig += "    " + m_promptPath + " init fish | source\n";
            fishConfig += "  else\n";
            fishConfig += "    echo 'starship not found at " + m_promptPath + "'\n";
            fishConfig += "  end\n";
            fishConfig += "end\n";
        } else {
            fishConfig += builtinPrompt;
        }
        fishConfig +=
            "function __example_repaint_prompt --on-signal WINCH\n"
            "  printf '\\r\\033[2K'\n"
            "  fish_prompt\n"
            "  commandline -f repaint\n"
            "end\n";
        std::fwrite(fishConfig.data(), 1, fishConfig.size(), file);
        std::fclose(file);
        chmod(fishConfigPath.c_str(), 0700);

        file = std::fopen(starshipConfigPath.c_str(), "w");
        if (!file) {
            OH_LOG_WARN(LOG_APP, "Failed to write starship config");
            return;
        }

        const std::string starshipConfig =
            "\"$schema\" = 'https://starship.rs/config-schema.json'\n"
            "\n"
            "format = \"\"\"\n"
            "[░▒▓](#a3aed2)\\\n"
            "[ HarmonyOS ](bg:#a3aed2 fg:#090c0c)\\\n"
            "[](bg:#769ff0 fg:#a3aed2)\\\n"
            "$directory\\\n"
            "[](fg:#769ff0 bg:#394260)\\\n"
            "$git_branch\\\n"
            "$git_status\\\n"
            "[](fg:#394260 bg:#212736)\\\n"
            "$nodejs\\\n"
            "$rust\\\n"
            "$golang\\\n"
            "$php\\\n"
            "[](fg:#212736 bg:#1d2230)\\\n"
            "$time\\\n"
            "[ ](fg:#1d2230)\\\n"
            "\\n$character\"\"\"\n"
            "\n"
            "[directory]\n"
            "style = \"fg:#e3e5e5 bg:#769ff0\"\n"
            "format = \"[ $path ]($style)\"\n"
            "truncation_length = 3\n"
            "truncation_symbol = \"…/\"\n"
            "\n"
            "[directory.substitutions]\n"
            "\"Documents\" = \"󰈙 \"\n"
            "\"Downloads\" = \" \"\n"
            "\"Music\" = \" \"\n"
            "\"Pictures\" = \" \"\n"
            "\n"
            "[git_branch]\n"
            "symbol = \"\"\n"
            "style = \"bg:#394260\"\n"
            "format = '[[ $symbol $branch ](fg:#769ff0 bg:#394260)]($style)'\n"
            "\n"
            "[git_status]\n"
            "style = \"bg:#394260\"\n"
            "format = '[[($all_status$ahead_behind )](fg:#769ff0 bg:#394260)]($style)'\n"
            "\n"
            "[nodejs]\n"
            "symbol = \"\"\n"
            "style = \"bg:#212736\"\n"
            "format = '[[ $symbol ($version) ](fg:#769ff0 bg:#212736)]($style)'\n"
            "\n"
            "[rust]\n"
            "symbol = \"\"\n"
            "style = \"bg:#212736\"\n"
            "format = '[[ $symbol ($version) ](fg:#769ff0 bg:#212736)]($style)'\n"
            "\n"
            "[golang]\n"
            "symbol = \"\"\n"
            "style = \"bg:#212736\"\n"
            "format = '[[ $symbol ($version) ](fg:#769ff0 bg:#212736)]($style)'\n"
            "\n"
            "[php]\n"
            "symbol = \"\"\n"
            "style = \"bg:#212736\"\n"
            "format = '[[ $symbol ($version) ](fg:#769ff0 bg:#212736)]($style)'\n"
            "\n"
            "[time]\n"
            "disabled = false\n"
            "time_format = \"%R\"\n"
            "style = \"bg:#1d2230\"\n"
            "format = '[[  $time ](fg:#a0a9cb bg:#1d2230)]($style)'\n";
        std::fwrite(starshipConfig.data(), 1, starshipConfig.size(), file);
        std::fclose(file);
        chmod(starshipConfigPath.c_str(), 0700);
    }

    std::mutex m_mutex;
    std::mutex m_outputMutex;
    std::string m_outputBuffer;
    napi_threadsafe_function m_outputTsfn = nullptr;
    std::string m_filesDir;
    std::string m_binDir;
    std::string m_commandPath;
    std::string m_shellPath;
    std::string m_promptPath;
    std::string m_shellInitPath;
    std::string m_pendingEscapeBuffer;
    std::string m_workingDir;
    int m_lastExitStatus = 0;
    size_t m_startupLogChunks = 0;
    std::thread m_localReadThread;
    std::unique_ptr<SSHSession> m_sshSession;
    std::atomic<bool> m_running {false};
    int m_masterFd = -1;
    int m_writeFd = -1;
    pid_t m_childPid = -1;
    int m_cols = DEFAULT_COLS;
    int m_rows = DEFAULT_ROWS;
    Mode m_mode = Mode::LocalShell;
    std::string m_currentLine;
    SSHRequest m_pendingRequest;
    std::string m_pendingPassword;
};

ExampleDriver& GetDriver()
{
    static ExampleDriver driver;
    return driver;
}

void SetNamedFunction(napi_env env, napi_value exports, const char* name, napi_callback callback)
{
    napi_property_descriptor descriptor {
        name, nullptr, callback, nullptr, nullptr, nullptr, napi_default, nullptr
    };
    napi_define_properties(env, exports, 1, &descriptor);
}

napi_value ReturnUndefined(napi_env env)
{
    napi_value result;
    napi_get_undefined(env, &result);
    return result;
}

std::string ReadStringArg(napi_env env, napi_value value)
{
    size_t length = 0;
    napi_get_value_string_utf8(env, value, nullptr, 0, &length);
    std::string result(length + 1, '\0');
    napi_get_value_string_utf8(env, value, result.data(), result.size(), &length);
    result.resize(length);
    return result;
}

napi_value Initialize(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = {nullptr, nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc >= 1) {
        GetDriver().Initialize(ReadStringArg(env, args[0]));
    }
    return ReturnUndefined(env);
}

napi_value Start(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = {nullptr, nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int32_t cols = DEFAULT_COLS;
    int32_t rows = DEFAULT_ROWS;
    if (argc >= 1) {
        napi_get_value_int32(env, args[0], &cols);
    }
    if (argc >= 2) {
        napi_get_value_int32(env, args[1], &rows);
    }
    const bool started = GetDriver().Start(cols, rows);
    napi_value result;
    napi_get_boolean(env, started, &result);
    return result;
}

napi_value Stop(napi_env env, napi_callback_info info)
{
    GetDriver().Stop();
    return ReturnUndefined(env);
}

napi_value WriteInput(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc >= 1) {
        GetDriver().Write(ReadStringArg(env, args[0]));
    }
    return ReturnUndefined(env);
}

napi_value DrainOutput(napi_env env, napi_callback_info info)
{
    const std::string output = GetDriver().DrainOutput();
    napi_value result;
    napi_create_string_utf8(env, output.c_str(), output.length(), &result);
    return result;
}

napi_value SetOutputCallback(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        GetDriver().ClearOutputCallback();
        return ReturnUndefined(env);
    }

    napi_valuetype valueType = napi_undefined;
    napi_typeof(env, args[0], &valueType);
    if (valueType == napi_function) {
        GetDriver().SetOutputCallback(env, args[0]);
    } else {
        GetDriver().ClearOutputCallback();
    }
    return ReturnUndefined(env);
}

napi_value Resize(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = {nullptr, nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int32_t cols = DEFAULT_COLS;
    int32_t rows = DEFAULT_ROWS;
    if (argc >= 1) {
        napi_get_value_int32(env, args[0], &cols);
    }
    if (argc >= 2) {
        napi_get_value_int32(env, args[1], &rows);
    }
    GetDriver().Resize(cols, rows);
    return ReturnUndefined(env);
}

napi_value Init(napi_env env, napi_value exports)
{
    SetNamedFunction(env, exports, "initialize", Initialize);
    SetNamedFunction(env, exports, "start", Start);
    SetNamedFunction(env, exports, "stop", Stop);
    SetNamedFunction(env, exports, "writeInput", WriteInput);
    SetNamedFunction(env, exports, "drainOutput", DrainOutput);
    SetNamedFunction(env, exports, "setOutputCallback", SetOutputCallback);
    SetNamedFunction(env, exports, "resize", Resize);
    return exports;
}

} // namespace

EXTERN_C_START
static napi_module g_exampleDriverModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "example_driver",
    .nm_priv = nullptr,
    .reserved = {0},
};
EXTERN_C_END

extern "C" __attribute__((constructor)) void RegisterExampleDriverModule(void)
{
    napi_module_register(&g_exampleDriverModule);
}

extern "C" __attribute__((visibility("default"))) bool ExampleDriverWriteInputUtf8(
    const char* data, size_t length)
{
    return GetDriver().WriteRaw(data, length);
}

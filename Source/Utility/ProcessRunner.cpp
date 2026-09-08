#include "ProcessRunner.h"

#include <Windows.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstring>

// Windows.h defines max/min as macros and this project does not set NOMINMAX.
#undef max
#undef min

namespace
{
	// CreateProcess takes one command line string, so every argument containing a
	// space has to be quoted by hand. "D:\Projects\Game Engine\" contains one, so
	// this is not hypothetical.
	std::string quoteArg(const std::string& arg)
	{
		if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos)
			return arg;

		std::string out = "\"";
		size_t backslashes = 0;
		for (char c : arg)
		{
			if (c == '\\')
			{
				++backslashes;
				out += c;
				continue;
			}
			if (c == '"')
			{
				// Backslashes immediately before a quote must be doubled, and the
				// quote itself escaped, per the CRT's command line parsing rules.
				out.append(backslashes + 1, '\\');
				out += '"';
			}
			else
			{
				out += c;
			}
			backslashes = 0;
		}
		out.append(backslashes, '\\');   // trailing backslashes double before the closing quote
		out += '"';
		return out;
	}

	bool startsWith(const std::string& s, const char* prefix)
	{
		const size_t n = strlen(prefix);
		return s.size() >= n && s.compare(0, n, prefix) == 0;
	}

	// Returns the full path to `exe`, searching PATH when it is a bare name.
	// Empty when it could not be found.
	std::string resolveExecutable(const std::string& exe)
	{
		if (GetFileAttributesA(exe.c_str()) != INVALID_FILE_ATTRIBUTES)
		{
			char full[MAX_PATH] = {};
			if (GetFullPathNameA(exe.c_str(), MAX_PATH, full, nullptr))
				return std::string(full);
			return exe;
		}

		char  found[MAX_PATH] = {};
		char* filePart        = nullptr;
		// A bare name needs the .exe extension appended before PATH is searched.
		const char* ext = (exe.find('.') == std::string::npos) ? ".exe" : nullptr;
		if (SearchPathA(nullptr, exe.c_str(), ext, MAX_PATH, found, &filePart))
			return std::string(found);

		return std::string();
	}
}

ProcessRunner::~ProcessRunner()
{
	cancel();
}

std::string ProcessRunner::stage() const
{
	std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(m_mutex));
	return m_stage;
}

std::string ProcessRunner::lastError() const
{
	std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(m_mutex));
	return m_error;
}

std::vector<std::string> ProcessRunner::drainOutput()
{
	std::lock_guard<std::mutex> lk(m_mutex);
	std::vector<std::string> out;
	out.swap(m_lines);
	return out;
}

void ProcessRunner::closeHandles()
{
	if (m_readPipe)
	{
		CloseHandle(static_cast<HANDLE>(m_readPipe));
		m_readPipe = nullptr;
	}
	if (m_process)
	{
		CloseHandle(static_cast<HANDLE>(m_process));
		m_process = nullptr;
	}
}

bool ProcessRunner::start(const std::string&              exe,
                          const std::vector<std::string>& args,
                          const std::string&              workingDir)
{
	if (m_running.load())
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_error = "a process is already running";
		return false;
	}

	// Join the previous run's thread before reusing the members it wrote.
	if (m_thread.joinable())
		m_thread.join();

	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_lines.clear();
		m_stage.clear();
		m_error.clear();
	}
	m_exitCode.store(-1);
	m_progress.store(0.0f);
	m_hasProgress.store(false);
	m_cancelled.store(false);
	m_started = false;
	closeHandles();

	const std::string exePath = resolveExecutable(exe);
	if (exePath.empty())
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_error = "could not find executable: " + exe;
		spdlog::error("ProcessRunner: {}", m_error);
		return false;
	}

	std::string cmdLine = quoteArg(exePath);
	for (const std::string& a : args)
		cmdLine += " " + quoteArg(a);

	// The child inherits the write end; the read end must NOT be inheritable or
	// the pipe never reports EOF once the child exits.
	SECURITY_ATTRIBUTES sa = {};
	sa.nLength              = sizeof(sa);
	sa.bInheritHandle       = TRUE;
	sa.lpSecurityDescriptor = nullptr;

	HANDLE readPipe = nullptr, writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_error = "CreatePipe failed";
		spdlog::error("ProcessRunner: {}", m_error);
		return false;
	}
	SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOA si = {};
	si.cb         = sizeof(si);
	si.dwFlags    = STARTF_USESTDHANDLES;
	si.hStdInput  = nullptr;
	si.hStdOutput = writePipe;
	si.hStdError  = writePipe;   // tools log warnings to stderr; same stream is fine

	PROCESS_INFORMATION pi = {};

	std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
	mutableCmd.push_back('\0');   // CreateProcessA may write to lpCommandLine

	const BOOL ok = CreateProcessA(
		nullptr,
		mutableCmd.data(),
		nullptr, nullptr,
		TRUE,                       // inherit handles, so the child gets the pipe
		CREATE_NO_WINDOW,           // otherwise a console flashes over the editor
		nullptr,
		workingDir.empty() ? nullptr : workingDir.c_str(),
		&si, &pi);

	// The parent's copy of the write end must close now. While it stays open the
	// pipe has a live writer, so ReadFile blocks forever instead of seeing EOF.
	CloseHandle(writePipe);

	if (!ok)
	{
		const DWORD err = GetLastError();
		CloseHandle(readPipe);
		std::lock_guard<std::mutex> lk(m_mutex);
		m_error = "CreateProcess failed (error " + std::to_string(err) + "): " + cmdLine;
		spdlog::error("ProcessRunner: {}", m_error);
		return false;
	}

	CloseHandle(pi.hThread);
	m_process  = pi.hProcess;
	m_readPipe = readPipe;
	m_started  = true;
	m_running.store(true);

	spdlog::info("ProcessRunner: {}", cmdLine);

	m_thread = std::thread(&ProcessRunner::readerLoop, this);
	return true;
}

// Runs on the reader thread. Drains the pipe continuously: a Windows anonymous
// pipe buffers about 4 KB, and a verbose child (Blender fills that within a
// second) blocks on write once it is full. Waiting on the process before
// reading would deadlock every time.
void ProcessRunner::readerLoop()
{
	std::string pending;
	char        buffer[4096];

	for (;;)
	{
		DWORD read = 0;
		const BOOL ok = ReadFile(static_cast<HANDLE>(m_readPipe), buffer,
		                         static_cast<DWORD>(sizeof(buffer)), &read, nullptr);
		if (!ok || read == 0)
			break;   // child closed its end, or the pipe broke

		pending.append(buffer, read);

		size_t start = 0;
		for (;;)
		{
			const size_t nl = pending.find('\n', start);
			if (nl == std::string::npos)
				break;

			std::string line = pending.substr(start, nl - start);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			start = nl + 1;

			// Machine-readable markers the tool scripts emit. Consumed here rather
			// than shown verbatim, so the panel gets a progress value and a status
			// line without parsing text itself.
			if (startsWith(line, "@@@PROGRESS "))
			{
				const float v = static_cast<float>(std::atof(line.c_str() + 12));
				m_progress.store(v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
				m_hasProgress.store(true);
				continue;
			}
			if (startsWith(line, "@@@STAGE "))
			{
				std::lock_guard<std::mutex> lk(m_mutex);
				m_stage = line.substr(9);
				continue;
			}

			std::lock_guard<std::mutex> lk(m_mutex);
			m_lines.push_back(std::move(line));
		}
		pending.erase(0, start);
	}

	if (!pending.empty())   // last line with no trailing newline
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_lines.push_back(pending);
	}

	// Only safe to wait now that the pipe is drained and EOF has been seen.
	WaitForSingleObject(static_cast<HANDLE>(m_process), INFINITE);

	DWORD code = 0;
	if (GetExitCodeProcess(static_cast<HANDLE>(m_process), &code))
		m_exitCode.store(static_cast<int>(code));

	m_running.store(false);
}

void ProcessRunner::cancel()
{
	if (m_running.load() && m_process)
	{
		m_cancelled.store(true);
		TerminateProcess(static_cast<HANDLE>(m_process), 1);
		// The child's pipe handles close with it, so readerLoop hits EOF and exits.
	}

	if (m_thread.joinable())
		m_thread.join();

	m_running.store(false);
	closeHandles();
}

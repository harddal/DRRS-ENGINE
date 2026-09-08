#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// ProcessRunner — launch an external command line tool and stream its output
// back to the main thread without blocking the editor.
//
// Nothing here is specific to any one tool; the Mixamo importer is the first
// caller, but any asset cooker can use it.
//
// Usage from an ImGui panel:
//
//     if (!s_proc.isRunning() && ImGui::Button("Run"))
//         s_proc.start(exe, args, workingDir);
//
//     for (const std::string& line : s_proc.drainOutput())   // every frame
//         s_log.push_back(line);
//
// All public methods are safe to call from the main thread while the child
// process is running. No method touches Irrlicht, GL or ImGui, so the reader
// thread never reaches engine state.
// ---------------------------------------------------------------------------
class ProcessRunner
{
public:
	ProcessRunner() = default;
	~ProcessRunner();

	// The reader thread captures `this`, so the object must not be copied or moved.
	ProcessRunner(const ProcessRunner&)            = delete;
	ProcessRunner& operator=(const ProcessRunner&) = delete;

	// Resolves `exe` against PATH when it is not already a full path, then launches
	// it with `args` (quoted for you) in `workingDir` (empty = inherit the editor's).
	// Returns false and writes the reason to lastError() if the launch failed;
	// a false return means no thread was started and no state changed.
	bool start(const std::string&              exe,
	           const std::vector<std::string>& args,
	           const std::string&              workingDir = std::string());

	bool        isRunning() const { return m_running.load(); }
	int         exitCode()  const { return m_exitCode.load(); }   // valid once !isRunning()
	bool        succeeded() const { return !isRunning() && m_started && m_exitCode.load() == 0; }
	bool        started()   const { return m_started; }
	// True when cancel() killed the child, so callers can report "cancelled"
	// rather than a spurious non-zero exit code.
	bool        wasCancelled() const { return m_cancelled.load(); }

	// 0..1, parsed from "@@@PROGRESS <float>" lines. Stays at 0 for tools that
	// do not report it — check hasProgress() before drawing a determinate bar.
	float       progress()    const { return m_progress.load(); }
	bool        hasProgress() const { return m_hasProgress.load(); }

	// Last "@@@STAGE <text>" line, for a one-line status label.
	std::string stage() const;

	// Why start() returned false, or why the run failed.
	std::string lastError() const;

	// Moves completed stdout/stderr lines out of the queue. Call once per frame
	// from the main thread; returns empty when there is nothing new.
	std::vector<std::string> drainOutput();

	// TerminateProcess + join. Safe to call when nothing is running.
	void cancel();

private:
	void readerLoop();
	void closeHandles();

	std::thread              m_thread;
	std::mutex               m_mutex;          // guards m_lines, m_stage, m_error
	std::vector<std::string> m_lines;
	std::string              m_stage;
	std::string              m_error;

	std::atomic<bool>  m_running{false};
	std::atomic<int>   m_exitCode{-1};
	std::atomic<float> m_progress{0.0f};
	std::atomic<bool>  m_hasProgress{false};
	std::atomic<bool>  m_cancelled{false};
	bool               m_started = false;      // main thread only

	// Win32 HANDLEs, kept as void* so <Windows.h> stays out of this header.
	void* m_process   = nullptr;
	void* m_readPipe  = nullptr;
};

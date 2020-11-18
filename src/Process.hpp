#ifndef PROCESS_HPP
#define PROCESS_HPP
namespace Process
{

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using Data = /*TODO*/;

#else
#include <sys/types.h>

using Data = pid_t;

#endif // _WIN32

} // namespace Process

#endif // PROCESS_HPP

#ifdef PROCESS_IMPLEMENTATION
#ifndef PROCESS_IMPL_HPP
#define PROCESS_IMPL_HPP
namespace Process
{

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Detail
{

inline void AppendArgs([[maybe_unused]] std::string& out)
{}

template<typename T, typename... Args>
void AppendArgs(std::string& out, T&& arg, Args&& ...args)
{
	out.append(" \"").append(arg).append("\"");
	AppendArgs(out, std::forward<Args>(args)...);
}

} // namespace Detail

template<typename... Args>
void Launch(const char* program, Args&& ...args)
{
	std::string param(program);
	Detail::AppendArgs(param, std::forward<Args>(args)...);
	STARTUPINFOA si{};
	PROCESS_INFORMATION pi{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	if(CreateProcessA(nullptr, param.data(), nullptr, nullptr,
	   FALSE, 0, nullptr, nullptr, &si, &pi))
	{
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
}

bool IsRunning(const Data& data)
{
	// TODO
}

void CleanUp(const Data& data)
{
	// TODO
}

#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

bool IsRunning(const Data& data);

template<typename... Args>
std::pair<Data, bool> Launch(const char* program, Args&& ...args)
{
	pid_t id = vfork();
	if(id == -1)
		return std::pair<Data, bool>(0, false);
	else if(id > 0)
		return std::pair<Data, bool>(id, IsRunning(id));
	// Child continues execution...
	constexpr const char* NULL_CHAR_PTR = nullptr;
	execlp(program, program, std::forward<Args>(args)..., NULL_CHAR_PTR);
	// Immediately die if unable to change process image.
	std::abort();
}

bool IsRunning(const Data& data)
{
	return waitpid(data, NULL, WNOHANG) == 0;
}

void CleanUp(const Data& data)
{
	waitpid(data, NULL, 0);
}

#endif // _WIN32

} // namespace Process

#endif // PROCESS_IMPL_HPP
#endif // PROCESS_IMPLEMENTATION

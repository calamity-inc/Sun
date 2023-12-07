#include "Compiler.hpp"

#include "base.hpp"

#include "os.hpp"

namespace soup
{
	Compiler::Compiler()
		: prog("clang"),
#if SOUP_WINDOWS
		prog_ar("llvm-ar"),
		lang("c++20")
#else
		// Debian's at clang 11 right now, which does support C++20, but not enough to compile Soup without modifications.
		prog_ar("ar"),
		lang("c++17")
#endif
	{
	}

	bool Compiler::isEmscripten() const
	{
		return prog == "em++";
	}

	std::vector<std::string> Compiler::getArgs() const
	{
		std::vector<std::string> args{
#if SOUP_WINDOWS
			"-D", "_CRT_SECURE_NO_WARNINGS",
#else
			"-pthreads", "-Wno-unused-command-line-argument",
#endif
			"-std="
		};
		args.back().append(lang);
		if (!rtti)
		{
			args.emplace_back("-fno-rtti");
		}
		args.insert(args.end(), extra_args.begin(), extra_args.end());
		return args;
	}

	void Compiler::addLinkerArgs(std::vector<std::string>& args) const
	{
#if SOUP_WINDOWS
		if (!isEmscripten())
		{
			args.emplace_back("-luser32");
			args.emplace_back("-lgdi32");
		}
#else
		args.emplace_back("-fuse-ld=lld");
		args.emplace_back("-lstdc++");
		if (!isEmscripten())
		{
			args.emplace_back("-lstdc++fs");
			args.emplace_back("-lresolv");
		}
		args.emplace_back("-lm");
		args.emplace_back("-ldl");
#endif
		args.insert(args.end(), extra_linker_args.begin(), extra_linker_args.end());
	}

	std::string Compiler::makeObject(const std::string& in, const std::string& out) const
	{
		auto args = getArgs();
		args.emplace_back("-x");
		args.emplace_back("c++");
		args.emplace_back("-o");
		args.emplace_back(out);
		args.emplace_back("-c");
		args.emplace_back(in);
		return os::executeLong(prog, std::move(args));
	}

	const char* Compiler::getExecutableExtension() noexcept
	{
#if SOUP_WINDOWS
		return ".exe";
#else
		return "";
#endif
	}

	std::string Compiler::makeExecutable(const std::string& in, const std::string& out) const
	{
		auto args = getArgs();
		args.emplace_back("-o");
		args.emplace_back(out);
		args.emplace_back(in);
		addLinkerArgs(args);
		return os::executeLong(prog, std::move(args));
	}

	std::string Compiler::makeExecutable(const std::vector<std::string>& objects, const std::string& out) const
	{
		auto args = getArgs();
		args.emplace_back("-o");
		args.emplace_back(out);
		args.insert(args.end(), objects.begin(), objects.end());
		addLinkerArgs(args);
		return os::executeLong(prog, std::move(args));
	}

	const char* Compiler::getStaticLibraryExtension() noexcept
	{
#if SOUP_WINDOWS
		return ".lib";
#else
		return ".a";
#endif
	}

	std::string Compiler::makeStaticLibrary(const std::vector<std::string>& objects, const std::string& out) const
	{
		std::vector<std::string> args = { "rc", out };
		args.insert(args.end(), objects.begin(), objects.end());
		return os::executeLong(prog_ar, std::move(args));
	}

	const char* Compiler::getDynamicLibraryExtension() const
	{
		if (isEmscripten())
		{
			return ".js";
		}
#if SOUP_WINDOWS
		return ".dll";
#else
		return ".so";
#endif
	}

	std::string Compiler::makeDynamicLibrary(const std::string& in, const std::string& out) const
	{
		auto args = getArgs();
#if !SOUP_WINDOWS
		args.emplace_back("-fPIC");
		args.emplace_back("-fvisibility=hidden");
#endif
		args.emplace_back("--shared");
		args.emplace_back("-o");
		args.emplace_back(out);
		args.emplace_back(in);
		addLinkerArgs(args);
		return os::executeLong(prog, std::move(args));
	}

	std::string Compiler::makeDynamicLibrary(const std::vector<std::string>& objects, const std::string& out) const
	{
		auto args = getArgs();
#if !SOUP_WINDOWS
		// -fPIC and -fvisibility=hidden need to be set per object
#endif
		args.emplace_back("--shared");
		args.emplace_back("-o");
		args.emplace_back(out);
		args.insert(args.end(), objects.begin(), objects.end());
		addLinkerArgs(args);
		return os::executeLong(prog, std::move(args));
	}
}

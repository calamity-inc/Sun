#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>

#include <soup/AtomicStack.hpp>
#include <soup/Compiler.hpp>
#include <soup/main.hpp>
#include <soup/Thread.hpp>

[[nodiscard]] static std::string remove_extension(const std::string& file)
{
	auto ext = std::filesystem::path(file).extension().string();
	auto name = file.substr(0, file.length() - ext.length());
	return name;
}

struct SharedCompileData
{
	const soup::Compiler* compiler;
	soup::AtomicStack<std::string> cpps;
	soup::AtomicStack<std::string> objects;
	std::mutex output_mutex;
};

[[nodiscard]] static std::vector<std::string> compile(const soup::Compiler& compiler, const std::vector<std::string>& cpps)
{
	if (!std::filesystem::is_directory("int"))
	{
		std::filesystem::create_directory("int");
	}

	SharedCompileData data;
	data.compiler = &compiler;
	for (auto i = cpps.crbegin(); i != cpps.crend(); ++i)
	{
		data.cpps.emplace_front(std::string(*i));
	}

	size_t threads_to_spin_up = (std::thread::hardware_concurrency() - 1);
	if (threads_to_spin_up < 1)
	{
		threads_to_spin_up = 1;
	}
	if (threads_to_spin_up > cpps.size())
	{
		threads_to_spin_up = cpps.size();
	}

	std::vector<soup::UniquePtr<soup::Thread>> threads{};
	while (threads_to_spin_up-- != 0)
	{
		threads.emplace_back(soup::make_unique<soup::Thread>([](soup::Capture&& cap)
		{
			SharedCompileData& data = *cap.get<SharedCompileData*>();
			while (true)
			{
				auto node = data.cpps.pop_front();
				if (!node)
				{
					break;
				}
				auto cpp = *node;

				auto name = remove_extension(cpp);

				data.output_mutex.lock();
				std::cout << name << std::endl;
				data.output_mutex.unlock();

				std::string o = "int/";
				o.append(name);
				o.append(".o");

				std::cout << data.compiler->makeObject(cpp, o);

				data.objects.emplace_front(std::move(o));
			}
		}, &data));
	}
	soup::Thread::awaitCompletion(threads);

	std::vector<std::string> objects{};
	while (true)
	{
		auto node = data.objects.pop_front();
		if (!node)
		{
			break;
		}
		objects.emplace_back(std::move(*node));
	}
	return objects;
}

int entry(std::vector<std::string>&& args, bool console)
{
	args.erase(args.begin());

	bool opt_static = false;
	for (auto i = args.begin(); i != args.end(); )
	{
		if(*i == "-static"
			|| *i == "--static"
			)
		{
			opt_static = true;
			i = args.erase(i);
		}
		else
		{
			++i;
		}
	}

	if (args.empty())
	{
		for (const auto& f : std::filesystem::directory_iterator("."))
		{
			if (f.is_regular_file() && f.path().extension() == ".cpp")
			{
				std::string path = f.path().string();
#if SOUP_WINDOWS
				if (path.substr(0, 2) == ".\\")
				{
					path.erase(0, 2);
				}
#else
				if (path.substr(0, 2) == "./")
				{
					path.erase(0, 2);
				}
#endif
				args.emplace_back(std::move(path));
			}
		}
	}

	soup::Compiler compiler;

	std::cout << "Compiling..." << std::endl;

	auto objects = compile(compiler, args);

	std::cout << "Linking..." << std::endl;

	std::string outname{};
	if (args.size() == 1)
	{
		outname = remove_extension(args.at(0));
	}
	else
	{
		outname = std::filesystem::current_path().filename().string();
	}

	std::string linkout;
	if (opt_static)
	{
		outname.append(soup::Compiler::getStaticLibraryExtension());
		linkout = compiler.makeStaticLibrary(objects, outname);
	}
	else
	{
		outname.append(soup::Compiler::getExecutableExtension());
		linkout = compiler.makeExecutable(objects, outname);
	}
	if (!linkout.empty())
	{
		std::cout << linkout;
		return 1;
	}
	return 0;
}

SOUP_MAIN_CLI(&entry);

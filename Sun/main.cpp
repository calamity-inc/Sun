#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

#include <soup/AtomicStack.hpp>
#include <soup/Compiler.hpp>
#include <soup/main.hpp>
#include <soup/StringMatch.hpp>
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
	std::mutex output_mutex;
	soup::AtomicStack<std::string> objects;
};

[[nodiscard]] static std::vector<std::string> compile(const soup::Compiler& compiler, soup::AtomicStack<std::string>&& cpps)
{
	if (!std::filesystem::is_directory("int"))
	{
		std::filesystem::create_directory("int");
	}

	SharedCompileData data;
	data.compiler = &compiler;
	data.cpps = std::move(cpps);

	size_t threads_to_spin_up = (std::thread::hardware_concurrency() - 1);
	if (threads_to_spin_up < 1)
	{
		threads_to_spin_up = 1;
	}
	if (threads_to_spin_up > data.cpps.size())
	{
		threads_to_spin_up = data.cpps.size();
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
				std::string& cpp = *node;

				auto name = remove_extension(cpp);

				std::string o = "int/";
				o.append(name);
				o.append(".o");

				if (!std::filesystem::exists(o)
					|| std::filesystem::last_write_time(cpp) > std::filesystem::last_write_time(o)
					)
				{
					data.output_mutex.lock();
					std::cout << name << "\n";
					data.output_mutex.unlock();

					std::cout << data.compiler->makeObject(cpp, o);
				}

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
	SOUP_IF_UNLIKELY (args.size() > 1)
	{
		SOUP_IF_UNLIKELY (args.at(1) != "set")
		{
			std::cout << "Unknown argument: " << args.at(1) << std::endl;
			return 1;
		}

		SOUP_IF_UNLIKELY (!std::filesystem::is_regular_file(".sun"))
		{
			std::cout << "No .sun file found in the working directory." << std::endl;
			return 1;
		}

		SOUP_IF_UNLIKELY (args.size() <= 2)
		{
			std::cout << "Set what?" << std::endl;
			return 1;
		}

		std::ofstream of(".sun", std::ios::app);
		of << "static\n";

		std::cout << "Done." << std::endl;

		return 0;
	}
	// Just 'sun' was used...

	// Config Data
	soup::AtomicStack<std::string> cpps{};
	bool opt_static = false;

	// Read Config
	SOUP_IF_UNLIKELY (!std::filesystem::is_regular_file(".sun"))
	{
		std::ofstream of(".sun");
		of << "+*.cpp\n";
		
		std::cout << "No .sun file found in the working directory; I've created one for you." << std::endl;
		std::cout << "Run 'sun' again to build your project." << std::endl;
		std::cout << "Run 'sun set static' to set this project to be a static library." << std::endl;
		return 2;
	}
	std::ifstream in(".sun");
	for (std::string line; std::getline(in, line); )
	{
		SOUP_IF_UNLIKELY (line.empty())
		{
			continue;
		}

		if (line.at(0) == '+')
		{
			line.erase(0, 1);
			if (line.find('*') == std::string::npos)
			{
				cpps.emplace_front(std::move(line));
			}
			else
			{
				for (const auto& f : std::filesystem::directory_iterator("."))
				{
					if (f.is_regular_file())
					{
						std::string fn = f.path().filename().string();
						if (soup::StringMatch::wildcard(line, fn, 1))
						{
							cpps.emplace_front(std::move(fn));
						}
					}
				}
			}
			continue;
		}

		if (line == "static")
		{
			opt_static = true;
			continue;
		}

		std::cout << "Ignoring line with unknown data: " << line << "\n";
	}

	// Guess Project Name
	std::string outname{};
	if (cpps.size() == 1)
	{
		outname = remove_extension(cpps.head.load()->data);
	}
	else
	{
		outname = std::filesystem::current_path().filename().string();
	}

	// Compile
	soup::Compiler compiler;
	auto objects = compile(compiler, std::move(cpps));

	// Link
	std::cout << "Linking..." << "\n";
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

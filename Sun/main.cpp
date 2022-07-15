#include <filesystem>
#include <iostream>

#include <soup/Compiler.hpp>
#include <soup/main.hpp>

[[nodiscard]] static std::string remove_extension(const std::string& file)
{
	auto ext = std::filesystem::path(file).extension().string();
	auto name = file.substr(0, file.length() - ext.length());
	return name;
}

static bool make_executable(const std::vector<std::string>& cpps)
{
	std::vector<std::string> objects{};

	if (!std::filesystem::is_directory("int"))
	{
		std::filesystem::create_directory("int");
	}

	std::cout << "Compiling..." << std::endl;
	for (const auto& cpp : cpps)
	{
		auto name = remove_extension(cpp);

		std::cout << name << std::endl;

		std::string o = "int/";
		o.append(name);
		o.append(".o");

		std::cout << soup::Compiler::makeObject(cpp, o);

		objects.emplace_back(o);
	}

	std::cout << "Linking..." << std::endl;

	std::string exe{};
	if (cpps.size() == 1)
	{
		exe = remove_extension(cpps.at(0));
	}
	else
	{
		exe = std::filesystem::current_path().filename().string();
	}
#if SOUP_WINDOWS
	exe.append(".exe");
#endif

	auto linkout = soup::Compiler::makeExecutable(objects, exe);
	if (!linkout.empty())
	{
		std::cout << linkout;
		return false;
	}

	return true;
}

int entry(std::vector<std::string>&& args, bool console)
{
	args.erase(args.begin());

	if (args.empty())
	{
		for (const auto& f : std::filesystem::directory_iterator("."))
		{
			if (f.is_regular_file() && f.path().extension() == ".cpp")
			{
				std::string path = f.path().string();
				if (path.substr(0, 2) == ".\\")
				{
					path.erase(0, 2);
				}
				args.emplace_back(std::move(path));
			}
		}
	}

	if (!make_executable(args))
	{
		return 1;
	}

	return 0;
}

SOUP_MAIN_CLI(&entry);

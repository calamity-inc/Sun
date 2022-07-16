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

[[nodiscard]] static std::vector<std::string> compile(const std::vector<std::string>& cpps)
{
	std::vector<std::string> objects{};

	if (!std::filesystem::is_directory("int"))
	{
		std::filesystem::create_directory("int");
	}

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
				if (path.substr(0, 2) == ".\\")
				{
					path.erase(0, 2);
				}
				args.emplace_back(std::move(path));
			}
		}
	}

	std::cout << "Compiling..." << std::endl;

	auto objects = compile(args);

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
		linkout = soup::Compiler::makeStaticLibrary(objects, outname);
	}
	else
	{
		outname.append(soup::Compiler::getExecutableExtension());
		linkout = soup::Compiler::makeExecutable(objects, outname);
	}
	if (!linkout.empty())
	{
		std::cout << linkout;
		return 1;
	}
	return 0;
}

SOUP_MAIN_CLI(&entry);

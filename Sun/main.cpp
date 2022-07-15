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

int entry(std::vector<std::string>&& args, bool console)
{
	if (args.size() < 2)
	{
		std::cout << "Syntax: sun <.cpp file paths ...>" << std::endl;
		return 1;
	}

	std::vector<std::string> objects{};

	if (!std::filesystem::is_directory("int"))
	{
		std::filesystem::create_directory("int");
	}

	std::cout << "Compiling..." << std::endl;
	args.erase(args.begin());
	for (const auto& cpp : args)
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
	if (args.size() == 1)
	{
		exe = remove_extension(args.at(0));
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
		return 1;
	}

	return 0;
}

SOUP_MAIN_CLI(&entry);

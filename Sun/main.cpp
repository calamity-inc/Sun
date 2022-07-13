#include <filesystem>
#include <iostream>

#include <soup/Compiler.hpp>
#include <soup/main.hpp>

int entry(std::vector<std::string>&& args, bool console)
{
	if (args.size() < 2)
	{
		std::cout << "Syntax: sun <path>" << std::endl;
		return 1;
	}

	auto cpp = args.at(1);
	auto ext = std::filesystem::path(cpp).extension().string();
	auto name = cpp.substr(0, cpp.length() - ext.length());

	auto exe = name;
	exe.append(".exe");

	soup::Compiler::compileExecutable(cpp, exe);

	return 0;
}

SOUP_MAIN_CLI(&entry);

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

#define E_OK			0
#define E_BADARG		1
#define E_NOSUNFILE		2
#define E_NEWSUNFILE	3
#define E_LINKERR		4
#define E_BADDEPEND		5

[[nodiscard]] static std::string get_name_no_extension(const std::filesystem::path& p)
{
	auto name = p.filename().string();
	auto ext = p.extension().string();
	return name.substr(0, name.length() - ext.length());
}

struct Project
{
	std::filesystem::path dir;

	std::vector<std::filesystem::path> dependencies{};
	soup::AtomicStack<std::filesystem::path> cpps{};
	bool opt_static = false;

	Project(std::filesystem::path dir)
		: dir(std::move(dir))
	{
	}

	bool load()
	{
		auto sunfile = dir;
		sunfile /= ".sun";

		SOUP_IF_UNLIKELY(!std::filesystem::exists(sunfile))
		{
			return false;
		}

		std::ifstream in(sunfile);
		for (std::string line; std::getline(in, line); )
		{
			SOUP_IF_UNLIKELY(line.empty())
			{
				continue;
			}

			if (line.at(0) == '+')
			{
				line.erase(0, 1);
				matchFiles(std::move(line), cpps, [](soup::AtomicStack<std::filesystem::path>& cpps, std::filesystem::path file)
				{
					cpps.emplace_front(std::move(file));
				});
				continue;
			}

			if (line.at(0) == '-')
			{
				line.erase(0, 1);
				matchFiles(std::move(line), cpps, [](soup::AtomicStack<std::filesystem::path>& cpps, std::filesystem::path file)
				{
					for (auto node = cpps.head.load(); node != nullptr; node = node->next)
					{
						if (node->data == file)
						{
							cpps.erase(node);
							break;
						}
					}
				});
				continue;
			}

			if (line == "static")
			{
				opt_static = true;
				continue;
			}

			if (line.substr(0, 8) == "require ")
			{
				auto p = dir;
				p /= line.substr(8);
				dependencies.emplace_back(std::move(p));
				continue;
			}

			std::cout << "Ignoring line with unknown data: " << line << "\n";
		}
		return true;
	}

	[[nodiscard]] std::string getName() const
	{
		std::string outname{};
		if (cpps.size() == 1)
		{
			outname = get_name_no_extension(cpps.head.load()->data);
		}
		else
		{
			auto p = dir;
			if (p.filename().string() == "src")
			{
				p = p.parent_path();
			}
			outname = p.filename().string();
		}
		return outname;
	}

	void matchFiles(std::string&& query, soup::AtomicStack<std::filesystem::path>& cpps, void(*callback)(soup::AtomicStack<std::filesystem::path>&, std::filesystem::path)) const
	{
		if (query.find('*') == std::string::npos)
		{
			callback(cpps, std::move(query));
		}
		else
		{
			for (const auto& f : std::filesystem::directory_iterator(dir))
			{
				if (f.is_regular_file())
				{
					if (soup::StringMatch::wildcard(query, f.path().filename().string(), 1))
					{
						callback(cpps, f.path());
					}
				}
			}
		}
	}

	[[nodiscard]] soup::Compiler getCompiler() const
	{
		soup::Compiler compiler;
		return compiler;
	}

	struct SharedCompileData
	{
		Project* proj;
		const soup::Compiler* compiler;
		std::mutex output_mutex;
		soup::AtomicStack<std::string> objects;
	};

	[[nodiscard]] std::vector<std::string> compile(soup::Compiler& compiler)
	{
		if (!dependencies.empty())
		{
			for (const auto& dep : dependencies)
			{
				Project dep_proj(dep);
				SOUP_IF_UNLIKELY(!dep_proj.load())
				{
					std::cout << "Failed to load dependency: " << dep << "\n";
					exit(E_BADDEPEND);
				}
				auto dep_name = dep_proj.getName();
				std::cout << ">>> Processing dependency: " << dep_name << "\n";
				SOUP_IF_UNLIKELY(!dep_proj.opt_static)
				{
					std::cout << "Not a static library? Not sure how to sanely deal with that.\n";
					exit(E_BADDEPEND);
				}
				auto err = dep_proj.compileAndLink();
				SOUP_IF_UNLIKELY(err != E_OK)
				{
					exit(err);
				}
				// Add relevant args to our compiler
				{
					std::string arg_include = "-I";
					arg_include.append(dep.string());
					compiler.extra_args.emplace_back(std::move(arg_include));
				}
				{
					std::string arg_lib = "-l";
					arg_lib.append(dep_proj.getOutFile(dep_name).string());
					compiler.extra_linker_args.emplace_back(std::move(arg_lib));
				}
			}
			std::cout << ">>> Time for the main attraction.\n";
		}

		auto intdir = dir;
		intdir /= "int";
		if (!std::filesystem::is_directory(intdir))
		{
			std::filesystem::create_directory(intdir);
		}

		SharedCompileData data;
		data.proj = this;
		data.compiler = &compiler;

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
					auto node = data.proj->cpps.pop_front();
					if (!node)
					{
						break;
					}
					std::filesystem::path& cpp = *node;

					auto name = get_name_no_extension(cpp);

					auto op = data.proj->dir;
					op /= "int";
					op /= name;
					std::string o = op.string();
					o.append(".o");

					if (!std::filesystem::exists(o)
						|| std::filesystem::last_write_time(cpp) > std::filesystem::last_write_time(o)
						)
					{
						data.output_mutex.lock();
						std::cout << name << "\n";
						data.output_mutex.unlock();

						std::cout << data.compiler->makeObject(cpp.string(), o);
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

	[[nodiscard]] std::filesystem::path getOutFile() const
	{
		return getOutFile(getName());
	}

	[[nodiscard]] std::filesystem::path getOutFile(std::string name) const
	{
		if (opt_static)
		{
			name.append(soup::Compiler::getStaticLibraryExtension());
		}
		else
		{
			name.append(soup::Compiler::getExecutableExtension());
		}
		auto p = dir;
		p /= name;
		return p;
	}

	int compileAndLink()
	{
		auto compiler = getCompiler();
		auto outfile = getOutFile();

		auto objects = compile(compiler);

		std::cout << "Linking...\n";
		//std::cout << "Linking " << objects.size() << " objects...\n";
		std::string linkout{};
		if (opt_static)
		{
			linkout = compiler.makeStaticLibrary(objects, outfile.string());
		}
		else
		{
			linkout = compiler.makeExecutable(objects, outfile.string());
		}
		if (!linkout.empty())
		{
			std::cout << linkout;
			return E_LINKERR;
		}
		return E_OK;
	}
};

int entry(std::vector<std::string>&& args, bool console)
{
#if false
	std::cout << "Waiting 5 seconds. Attach debugger now.\n";
	std::this_thread::sleep_for(std::chrono::seconds(5));
	std::cout << "Time's up.\n";
#endif

	SOUP_IF_UNLIKELY (args.size() > 1)
	{
		SOUP_IF_UNLIKELY (args.at(1) != "set")
		{
			std::cout << "Unknown argument: " << args.at(1) << std::endl;
			return E_BADARG;
		}

		SOUP_IF_UNLIKELY (!std::filesystem::is_regular_file(".sun"))
		{
			std::cout << "No .sun file found in the working directory." << std::endl;
			return E_NOSUNFILE;
		}

		SOUP_IF_UNLIKELY (args.size() <= 2)
		{
			std::cout << "Set what?" << std::endl;
			return E_BADARG;
		}

		std::ofstream of(".sun", std::ios::app);
		of << "static\n";

		std::cout << "Done." << std::endl;

		return E_OK;
	}

	// Just 'sun' was used...

	Project proj(std::filesystem::current_path());
	SOUP_IF_UNLIKELY (!proj.load())
	{
		std::ofstream of(".sun");
		of << "+*.cpp\n";
		
		std::cout << "No .sun file found in the working directory; I've created one for you." << std::endl;
		std::cout << "Run 'sun' again to build your project." << std::endl;
		std::cout << "Run 'sun set static' to set this project to be a static library." << std::endl;
		return E_NEWSUNFILE;
	}
	return proj.compileAndLink();
}

SOUP_MAIN_CLI(&entry);

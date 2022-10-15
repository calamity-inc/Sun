#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

#include <soup/AtomicStack.hpp>
#include <soup/Compiler.hpp>
#include <soup/main.hpp>
#include <soup/os.hpp>
#include <soup/string.hpp>
#include <soup/StringMatch.hpp>
#include <soup/Thread.hpp>

#define E_OK			0
#define E_BADARG		1
#define E_LINKERR		2
#define E_BADDEPEND		3

[[nodiscard]] static std::string get_name_no_extension(const std::filesystem::path& p)
{
	auto name = p.filename().string();
	auto ext = p.extension().string();
	return name.substr(0, name.length() - ext.length());
}

struct Dependency
{
	std::filesystem::path dir;
	std::filesystem::path include_dir;
};

struct Project
{
	std::filesystem::path dir;
	std::filesystem::path sunfile;
	std::string name;
	std::vector<Dependency> dependencies{};
	soup::AtomicStack<std::filesystem::path> cpps{};
	bool opt_static = false;
	bool opt_dynamic = false;
	std::vector<std::string> extra_args{};
	std::vector<std::string> extra_linker_args{};

	Project(std::filesystem::path dir, std::string name = {})
		: dir(dir), sunfile(std::move(dir))
	{
		name.append(".sun");
		sunfile /= name;
	}

	bool load()
	{
		SOUP_IF_UNLIKELY(!std::filesystem::exists(sunfile))
		{
			return false;
		}

		std::ifstream in(sunfile);
		bool ifblk_active = false;
		bool ifblk_true;
		for (std::string line; std::getline(in, line); )
		{
			SOUP_IF_UNLIKELY(line.empty())
			{
				continue;
			}
			if (line.at(line.size() - 1) == '\r')
			{
				line.erase(line.size() - 1);
				SOUP_IF_UNLIKELY(line.empty())
				{
					continue;
				}
			}

			if (line == "endif")
			{
				if (!ifblk_active)
				{
					std::cout << "endif called while if-block is not active\n";
				}
				ifblk_active = false;
				continue;
			}

			if (ifblk_active
				&& !ifblk_true
				)
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
					file = std::filesystem::absolute(file);
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

			if (line.substr(0, 5) == "name ")
			{
				name = line.substr(5);
				continue;
			}

			if (line.substr(0, 8) == "require ")
			{
				Dependency dep;
				dep.dir = dir;
				auto sep = line.find(" include_dir=");
				if (sep == std::string::npos)
				{
					dep.dir /= line.substr(8);
					dep.include_dir = dep.dir;
				}
				else
				{
					dep.dir /= line.substr(8, sep - 8);
					dep.include_dir = dir;
					dep.include_dir = line.substr(sep + 13);
				}
				dep.dir = std::filesystem::absolute(dep.dir);
				dep.include_dir = std::filesystem::absolute(dep.include_dir);
				dependencies.emplace_back(std::move(dep));
				continue;
			}

			if (line.substr(0, 4) == "arg ")
			{
				extra_args.emplace_back(line.substr(4));
				continue;
			}

			if (line.substr(0, 11) == "linker_arg ")
			{
				extra_linker_args.emplace_back(line.substr(11));
				continue;
			}

			if (line.substr(0, 3) == "if ")
			{
				auto condition = line.substr(3);
				soup::string::lower(condition);
				bool invert = false;
				if (condition.substr(0, 4) == "not ")
				{
					invert = true;
					condition = condition.substr(4);
				}
				bool val = false;
				if (condition == "windows")
				{
					val = SOUP_WINDOWS;
				}
				else if (condition == "linux")
				{
					val = SOUP_LINUX;
				}
				else if (condition == "true")
				{
					val = true;
				}
				else if (condition == "false")
				{
					val = false;
				}
				else
				{
					std::cout << "Treating unknown condition \"" << condition << "\" as false\n";
				}
				ifblk_active = true;
				ifblk_true = (val ^ invert);
				continue;
			}

			if (line == "static")
			{
				opt_static = true;
				continue;
			}

			if (line == "dynamic" || line == "shared")
			{
				opt_dynamic = true;
#if !SOUP_WINDOWS
				extra_args.emplace_back("-fPIC");
				extra_args.emplace_back("-fvisibility=hidden");
#endif
				continue;
			}

			std::cout << "Ignoring line with unknown data: " << line << "\n";
		}
		return true;
	}

	[[nodiscard]] std::string getName() const
	{
		if (!name.empty())
		{
			return name;
		}
		if (cpps.size() == 1)
		{
			auto name = get_name_no_extension(cpps.head.load()->data);
			if (name != "main")
			{
				return name;
			}
		}
		auto p = dir;
		if (p.filename().string() == "src")
		{
			p = p.parent_path();
		}
		return p.filename().string();
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
		compiler.extra_args = extra_args;
		compiler.extra_linker_args = extra_linker_args;
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
				Project dep_proj(dep.dir);
				SOUP_IF_UNLIKELY(!dep_proj.load())
				{
					std::cout << "Failed to load dependency: " << dep.dir << "\n";
					exit(E_BADDEPEND);
				}
				auto dep_name = dep_proj.getName();
				std::cout << ">>> Processing dependency: " << dep_name << "\n";
				SOUP_IF_UNLIKELY(!dep_proj.opt_static && !dep_proj.opt_dynamic)
				{
					std::cout << "Dependency does not specify 'static' or 'dynamic'.\n";
					exit(E_BADDEPEND);
				}
				auto err = dep_proj.compileAndLink();
				SOUP_IF_UNLIKELY(err != E_OK)
				{
					exit(err);
				}
				// Add compiler include flag
				{
					std::string arg_include = "-I";
					arg_include.append(dep.include_dir.string());
					compiler.extra_args.emplace_back(std::move(arg_include));
				}
				if (dep_proj.opt_static)
				{
					// Tell linker to include the static library
					compiler.extra_linker_args.emplace_back(dep_proj.getOutFile(dep_name).string());
				}
				else //if (dep_proj.opt_dynamic)
				{
#if SOUP_WINDOWS
					// Tell linker to include the dynamic library
					compiler.extra_linker_args.emplace_back(dep_proj.getLibPath(dep_name).string());
#else
					// Add dependency directory to linker search path
					{
						std::string arg_libpath = "-L";
						arg_libpath.append(dep.dir.string());
						compiler.extra_linker_args.emplace_back(std::move(arg_libpath));
					}
					// Give dependency name to linker
					{
						std::string arg_libpath = "-l";
						arg_libpath.append(dep_name);
						compiler.extra_linker_args.emplace_back(std::move(arg_libpath));
					}
#endif
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

					std::error_code ec;
					if (!std::filesystem::exists(o)
						|| std::filesystem::last_write_time(cpp, ec) > std::filesystem::last_write_time(o, ec)
						|| ec
						)
					{
						data.output_mutex.lock();
						std::cout << name << "\n";
						data.output_mutex.unlock();

						auto msg = data.compiler->makeObject(cpp.string(), o);
						if (!msg.empty())
						{
							data.output_mutex.lock();
							std::cout << std::move(msg);
							data.output_mutex.unlock();
						}
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
		else if (opt_dynamic)
		{
#if SOUP_LINUX
			name.insert(0, "lib");
#endif
			name.append(soup::Compiler::getDynamicLibraryExtension());
		}
		else
		{
			name.append(soup::Compiler::getExecutableExtension());
		}
		auto p = dir;
		p /= name;
		return p;
	}

#if SOUP_WINDOWS
	[[nodiscard]] std::filesystem::path getLibPath(std::string name) const
	{
		name.append(".lib");
		auto p = dir;
		p /= name;
		return p;
	}
#endif

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
		else if (opt_dynamic)
		{
			linkout = compiler.makeDynamicLibrary(objects, outfile.string());
#if SOUP_WINDOWS
			if (linkout.substr(0, 19) == "   Creating library")
			{
				linkout.clear();
			}
#endif
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

	size_t i = 1;

	SOUP_IF_UNLIKELY (args.size() > i
		&& (args.at(i) == "help"
			|| args.at(i) == "-?"
			)
		)
	{
		// sun help
		if (args.size() > ++i)
		{
			// sun help ...
			if (args.at(i) == "create")
			{
				// sun help create
				std::cout << "\n";
				std::cout << "  sun [proj] create            Create executable project\n";
				std::cout << "  sun [proj] create static     Create static library project\n";
				std::cout << "  sun [proj] create dynamic    Create dynamic/shared library project\n";
				std::cout << "\n";
				return E_OK;
			}
			else
			{
				std::cout << "Unknown help topic \"" << args.at(i) << "\". Use 'sun help' for help overview.\n";
				return E_BADARG;
			}
		}
		else
		{
			std::cout << "\n";
			std::cout << "  sun [proj] create ...        Create project ('sun help create')\n";
			std::cout << "  sun [proj]                   Build project\n";
			std::cout << "  sun [proj] run ...           Build & run project\n";
			std::cout << "\n";
			return E_OK;
		}
	}

	std::string projname{};
	if (args.size() > i
		&& args.at(i) != "create"
		&& args.at(i) != "set"
		&& args.at(i) != "run"
		)
	{
		projname = args.at(i++);
	}

	SOUP_IF_UNLIKELY (args.size() > i
		&& args.at(i) != "run"
		)
	{
		if (args.at(i) == "create")
		{
			// sun [proj] create

			projname.append(".sun");

			SOUP_IF_UNLIKELY (std::filesystem::is_regular_file(projname))
			{
				std::cout << projname << " already exists, not going to overwrite it.\n";
				return E_BADARG;
			}

			std::ofstream of(projname);
			of << "+*.cpp\n";

			if (args.size() > ++i
				&& (args.at(i) == "static"
					|| args.at(i) == "dynamic"
					|| args.at(i) == "shared"
					)
				)
			{
				of << args.at(i) << "\n";
			}

			std::cout << "Done.\n";
			return E_OK;
		}
		else
		{
			// sun [proj] ...
			std::cout << "Unknown project command \"" << args.at(i) << "\". Use 'sun help' for help.\n";
			return E_BADARG;
		}
	}
	else
	{
		// sun [proj]

		Project proj(std::filesystem::current_path(), projname);

		SOUP_IF_UNLIKELY (!proj.load())
		{
			auto projfile = projname;
			projfile.append(".sun");
			SOUP_IF_LIKELY (!std::filesystem::is_regular_file(projfile))
			{
				std::cout << "No file by the name of " << projfile << " in the working directory.\n";
				std::cout << "Use 'sun " << projname;
				if (!projname.empty())
				{
					std::cout << " ";
				}
				std::cout << "create' to create it. Use 'sun help create' for more info.\n";
			}
			else
			{
				std::cout << "Failed to load " << projfile << ".\n";
			}
			return E_BADARG;
		}

		const auto outname = proj.getName();
		SOUP_IF_UNLIKELY (int ret = proj.compileAndLink(); ret != E_OK)
		{
			return ret;
		}

		if (args.size() > i
			&& args.at(i) == "run"
			)
		{
			std::cout << ">>> Running...\n";
			args.erase(args.cbegin(), args.cbegin() + 2);
			std::cout << soup::os::execute(proj.getOutFile(outname).string(), std::move(args));
		}

		return E_OK;
	}
}

SOUP_MAIN_CLI(&entry);

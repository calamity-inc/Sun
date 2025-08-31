#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stack>
#include <thread>

#include <soup/AtomicStack.hpp>
#include <soup/Compiler.hpp>
#include <soup/joaat.hpp>
#include <soup/main.hpp>
#include <soup/os.hpp>
#include <soup/string.hpp>
#include <soup/StringMatch.hpp>
#include <soup/Thread.hpp>

#define E_OK			0
#define E_BADARG		1
#define E_LINKERR		2
#define E_BADDEPEND		3
#define E_EXCEPTION		4

[[nodiscard]] static std::string get_name_no_extension(const std::filesystem::path& p)
{
	auto name = soup::string::fixType(p.filename().u8string());
	auto ext = soup::string::fixType(p.extension().u8string());
	return name.substr(0, name.length() - ext.length());
}

[[nodiscard]] static std::time_t file_time_to_unix_time(std::filesystem::file_time_type ft)
{
	return std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::time_point_cast<std::chrono::system_clock::duration>(
			ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
		).time_since_epoch()
	).count();
}

struct Dependency
{
	std::filesystem::path dir;
	std::filesystem::path include_dir;
	std::string name;
};

struct Project
{
	std::filesystem::path dir;
	std::filesystem::path sunfile;
	std::string name;
	std::vector<Dependency> dependencies{};
	std::string prog = "clang";
	std::string cpp_version{};
	soup::AtomicStack<std::filesystem::path> cpps{};
	bool opt_static = false;
	bool opt_dynamic = false;
	bool opt_rtti = false;
	std::vector<std::string> extra_args{};
	std::vector<std::string> global_args{};
	std::vector<std::string> extra_linker_args{};

	Project(std::filesystem::path dir, std::string name = {})
		: dir(dir), sunfile(std::move(dir))
	{
		name.append(".sun");
		sunfile /= name;
	}

	bool load(std::string/*&&*/ extralines[] = nullptr, size_t extralines_size = 0)
	{
		SOUP_IF_UNLIKELY (!std::filesystem::exists(sunfile))
		{
			return false;
		}

		std::ifstream in(sunfile);
		std::stack<bool> ifblks;
		size_t extralines_i = 0;
		for (std::string line; std::getline(in, line)
			|| (extralines_i != extralines_size && (line = std::move(extralines[extralines_i++]), true)); )
		{
			SOUP_IF_UNLIKELY (line.empty())
			{
				continue;
			}
			while (soup::string::isSpace(line.at(0)))
			{
				line.erase(0, 1);
				SOUP_IF_UNLIKELY (line.empty())
				{
					goto _continue_2;
				}
			}
			if (line.at(line.size() - 1) == '\r')
			{
				line.erase(line.size() - 1);
				SOUP_IF_UNLIKELY (line.empty())
				{
					continue;
				}
			}

			if (line.at(0) == '#')
			{
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
				else if (condition == "macos")
				{
					val = SOUP_MACOS;
				}
				else if (condition == "linux")
				{
					val = SOUP_LINUX;
				}
				else if (condition == "x86")
				{
					val = SOUP_X86;
				}
				else if (condition == "arm")
				{
					val = SOUP_ARM;
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
				val ^= invert;
				if (!ifblks.empty())
				{
					val &= ifblks.top();
				}
				ifblks.emplace(val);
				continue;
			}

			if (line == "endif")
			{
				if (ifblks.empty())
				{
					std::cout << "endif called while if-block is not active\n";
				}
				else
				{
					ifblks.pop();
				}
				continue;
			}

			if (!ifblks.empty()
				&& ifblks.top() == false
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
				std::string req = (sep == std::string::npos) ? line.substr(8) : line.substr(8, sep - 8);
				auto colon = req.find(':');
				if (colon == std::string::npos)
				{
					dep.dir /= req;
				}
				else
				{
					dep.dir /= req.substr(0, colon);
					dep.name = req.substr(colon + 1);
				}
				if (sep == std::string::npos)
				{
					dep.include_dir = dep.dir;
				}
				else
				{
					dep.include_dir = dir;
					dep.include_dir = line.substr(sep + 13);
				}
				dep.dir = std::filesystem::absolute(dep.dir);
				dep.include_dir = std::filesystem::absolute(dep.include_dir);
				dependencies.emplace_back(std::move(dep));
				continue;
			}

			if (line.substr(0, 4) == "c++ " || line.substr(0, 4) == "cpp ")
			{
				if (!cpp_version.empty())
				{
					std::cout << "C++ version is specified multiple times.\n";
				}
				cpp_version = line.substr(4);
				continue;
			}

			if (line.substr(0, 7) == "define ")
			{
				std::string arg_define = "-D";
				arg_define.append(line.substr(7));
				extra_args.emplace_back(std::move(arg_define));
				continue;
			}

			if (line.substr(0, 4) == "arg ")
			{
				extra_args.emplace_back(line.substr(4));
				continue;
			}

			if (line.substr(0, 11) == "global_arg ")
			{
				global_args.emplace_back(line.substr(11));
				continue;
			}

			if (line.substr(0, 11) == "linker_arg ")
			{
				extra_linker_args.emplace_back(line.substr(11));
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

			if (line == "32bit")
			{
				global_args.emplace_back("-m32");
				continue;
			}

			if (line == "rtti")
			{
				opt_rtti = true;
				continue;
			}

			if (line.substr(0, 9) == "compiler ")
			{
				prog = line.substr(9);
				continue;
			}

			std::cout << "Ignoring line with unknown data: " << line << "\n";
		_continue_2:;
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
		if (p.filename().u8string() == u8"src")
		{
			p = p.parent_path();
		}
		return soup::string::fixType(p.filename().u8string());
	}

	[[nodiscard]] std::string getIntSubdirName() const
	{
#if SOUP_WINDOWS
		auto hash = soup::joaat::hash("windows");
#else
		auto hash = soup::joaat::hash("linux");
#endif
		if (prog != "clang")
		{
			hash = soup::joaat::concat(hash, prog);
		}
		if (opt_rtti)
		{
			hash = soup::joaat::concat(hash, "rtti");
		}
		for (const auto& extra_arg : extra_args)
		{
			hash = soup::joaat::concat(hash, extra_arg);
		}
		for (const auto& arg : global_args)
		{
			hash = soup::joaat::concat(hash, arg);
		}
		return soup::string::hex(hash);
	}

	void matchFiles(std::string&& query, soup::AtomicStack<std::filesystem::path>& cpps, void(*callback)(soup::AtomicStack<std::filesystem::path>&, std::filesystem::path)) const
	{
		bool recursive = (query.find('/') != std::string::npos);
		if (query.size() > 3 && query.substr(query.size() - 3) == " -R")
		{
			recursive = true;
			query.erase(query.size() - 3, 3);
		}

		if (query.find('*') == std::string::npos)
		{
			callback(cpps, dir / query);
		}
		else if (recursive)
		{
			for (const auto& f : std::filesystem::recursive_directory_iterator(dir))
			{
				if (f.is_regular_file())
				{
					auto path = soup::string::fixType(std::filesystem::relative(f.path(), dir).u8string());
#if SOUP_WINDOWS
					soup::string::replaceAll(path, '\\', '/');
#endif
					//std::cout << path << std::endl;
					if (soup::StringMatch::wildcard(query, path, 1))
					{
						callback(cpps, f.path());
					}
				}
			}
		}
		else
		{
			for (const auto& f : std::filesystem::directory_iterator(dir))
			{
				if (f.is_regular_file())
				{
					if (soup::StringMatch::wildcard(query, soup::string::fixType(f.path().filename().u8string()), 1))
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
		compiler.prog = prog;
		if (!cpp_version.empty())
		{
			compiler.lang = "c++";
			compiler.lang.append(cpp_version);
		}
		compiler.rtti = opt_rtti;
		compiler.extra_args = extra_args;
		compiler.extra_args.insert(compiler.extra_args.end(), global_args.begin(), global_args.end());
		compiler.extra_linker_args = extra_linker_args;
		return compiler;
	}

	struct SharedCompileData
	{
		Project* proj;
		const soup::Compiler* compiler;
		std::filesystem::path base_path;
		std::time_t last_header_modification;
		std::mutex output_mutex;
		soup::AtomicStack<std::string> objects;
	};

	[[nodiscard]] std::vector<std::string> compile(soup::Compiler& compiler)
	{
		std::vector<std::string> objects{};
		if (!dependencies.empty())
		{
			for (const auto& dep : dependencies)
			{
				Project dep_proj(dep.dir, dep.name);
				SOUP_IF_UNLIKELY (!dep_proj.load())
				{
					std::cout << "Failed to load dependency: " << dep.dir << "\n";
					exit(E_BADDEPEND);
				}
				auto dep_name = dep_proj.getName();
				std::cout << ">>> Processing dependency: " << dep_name << "\n";
				SOUP_IF_UNLIKELY (!dep_proj.opt_static && !dep_proj.opt_dynamic)
				{
					std::cout << "Dependency does not specify 'static' or 'dynamic'.\n";
					exit(E_BADDEPEND);
				}

				dep_proj.global_args.insert(dep_proj.global_args.end(), global_args.begin(), global_args.end());

				if (dep_proj.opt_static && opt_static) // Static library depending on a static library?
				{
					// Compile dependency and add it to our linking pile
					auto dep_compiler = dep_proj.getCompiler();
					auto dep_objects = dep_proj.compile(dep_compiler);
					objects.insert(objects.end(), dep_objects.begin(), dep_objects.end());
					compiler.extra_linker_args.insert(compiler.extra_linker_args.end(), dep_proj.extra_linker_args.begin(), dep_proj.extra_linker_args.end());
				}
				else
				{
					auto err = dep_proj.compileAndLink();
					SOUP_IF_UNLIKELY (err != E_OK)
					{
						exit(err);
					}
				}
				// Add compiler include flag
				{
					std::string arg_include = "-I";
					arg_include.append(soup::string::fixType(dep.include_dir.u8string()));
					compiler.extra_args.emplace_back(std::move(arg_include));
				}
				if (dep_proj.opt_static)
				{
					if (!opt_static)
					{
						// Tell linker to include the static library
						compiler.extra_linker_args.emplace_back(soup::string::fixType(dep_proj.getOutFile().u8string()));
					}
				}
				else //if (dep_proj.opt_dynamic)
				{
#if SOUP_WINDOWS
					// Tell linker to include the dynamic library
					compiler.extra_linker_args.emplace_back(soup::string::fixType(dep_proj.getLibPath(dep_name).u8string()));
#else
					// Add dependency directory to linker search path
					{
						std::string arg_libpath = "-L";
						arg_libpath.append(soup::string::fixType(dep.dir.u8string()));
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
			std::cout << ">>> Now compiling " << getName() << "\n";
		}

		// We're about to consume the 'cpps' stack, which may change the result of getName if we don't pin it.
		name = getName();

		SharedCompileData data;
		data.proj = this;
		data.compiler = &compiler;
		data.base_path = dir;
		data.base_path /= "int";
		if (!std::filesystem::is_directory(data.base_path))
		{
			std::filesystem::create_directory(data.base_path);
		}
		data.base_path /= getIntSubdirName();
		if (!std::filesystem::is_directory(data.base_path))
		{
			std::filesystem::create_directory(data.base_path);
		}

		data.last_header_modification = 0;
		for (const auto& f : std::filesystem::directory_iterator(dir))
		{
			if (f.is_regular_file())
			{
				const auto name = soup::string::fixType(f.path().filename().u8string());
#if SOUP_CPP20
				if (name.ends_with(".hpp") || name.ends_with(".h"))
#else
				if (name.substr(0, 4) == ".hpp" || name.substr(0, 2) == ".h")
#endif
				{
					const auto t = file_time_to_unix_time(std::filesystem::last_write_time(f));
					if (data.last_header_modification < t)
					{
						data.last_header_modification = t;
					}
				}
			}
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
					auto node = data.proj->cpps.pop_front();
					if (!node)
					{
						break;
					}
					std::filesystem::path& cpp = *node;

					auto name = get_name_no_extension(cpp);

					auto op = data.base_path;
					op /= name;
					std::string o = soup::string::fixType(op.u8string());
					o.append(".o");

					bool need_compile = !std::filesystem::exists(o);
					if (!need_compile)
					{
						std::error_code ec;
						const auto last_compile = std::filesystem::last_write_time(o, ec);
						need_compile = data.last_header_modification > file_time_to_unix_time(last_compile)
							|| std::filesystem::last_write_time(cpp, ec) > last_compile
							|| ec;
					}
					if (need_compile)
					{
						data.output_mutex.lock();
						//std::error_code ec;
						std::cout << name /*<< " (" << file_time_to_unix_time(std::filesystem::last_write_time(cpp, ec)) << ")"*/ << "\n";
						data.output_mutex.unlock();

						std::string msg;
						try
						{
							msg = data.compiler->makeObject(soup::string::fixType(cpp.u8string()), o);
						}
						catch (const std::exception& e)
						{
							msg = e.what();
							msg.push_back('\n');
						}
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
		std::string name = getName();
		if (opt_static)
		{
			name.append(soup::Compiler::getStaticLibraryExtension());
		}
		else if (opt_dynamic)
		{
#if SOUP_LINUX
			if (!getCompiler().isCrossCompiler())
			{
				name.insert(0, "lib");
			}
#endif
			name.append(getCompiler().getDynamicLibraryExtension());
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
			linkout = compiler.makeStaticLibrary(objects, soup::string::fixType(outfile.u8string()));
		}
		else if (opt_dynamic)
		{
			linkout = compiler.makeDynamicLibrary(objects, soup::string::fixType(outfile.u8string()));
#if SOUP_WINDOWS
			if (linkout.substr(0, 19) == "   Creating library")
			{
				linkout.clear();
			}
#endif
		}
		else
		{
			linkout = compiler.makeExecutable(objects, soup::string::fixType(outfile.u8string()));
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
		&& (args[i] == "help"
			|| args[i] == "-?"
			)
		)
	{
		// sun help
		if (args.size() > ++i)
		{
			// sun help ...
			if (args[i] == "create")
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
				std::cout << "Unknown help topic \"" << args[i] << "\". Use 'sun help' for help overview.\n";
				return E_BADARG;
			}
		}
		else
		{
			std::cout << "\n";
			std::cout << "  sun [proj] create ...        Create project ('sun help create')\n";
			std::cout << "  sun [proj] {+opt}            Build project\n";
			std::cout << "  sun [proj] {+opt} run ...    Build & run project\n";
			std::cout << "\n";
			return E_OK;
		}
	}

	std::string projname{};
	if (args.size() > i
		&& args[i].c_str()[0] != '+'
		&& args[i] != "create"
		&& args[i] != "set"
		&& args[i] != "run"
		)
	{
		projname = args.at(i++);
	}

	std::vector<std::string> extralines;
	while (args.size() > i
		&& args[i].c_str()[0] == '+'
		)
	{
		if (args[i].size() > 1 && args[i][1] == '"')
		{
			std::string line = args[i].substr(2);
			while (true)
			{
				if (!line.empty() && line.back() == '"')
				{
					line.pop_back();
					break;
				}
				if (args.size() <= ++i)
				{
					break;
				}
				line.push_back(' ');
				line.append(args[i]);
			}
			extralines.emplace_back(std::move(line));
			++i;
		}
		else
		{
			extralines.emplace_back(args[i].substr(1));
			++i;
		}
	}

	SOUP_IF_UNLIKELY (args.size() > i
		&& args[i] != "run"
		)
	{
		if (args[i] == "create")
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
				&& (args[i] == "static"
					|| args[i] == "dynamic"
					|| args[i] == "shared"
					)
				)
			{
				of << args[i] << "\n";
			}

			std::cout << "Done.\n";
			return E_OK;
		}
		else
		{
			// sun [proj] ...
			std::cout << "Unknown project command \"" << args[i] << "\". Use 'sun help' for help.\n";
			return E_BADARG;
		}
	}
	else
	{
		// sun [proj]
		try
		{
			Project proj(std::filesystem::current_path(), projname);

			SOUP_IF_UNLIKELY (!proj.load(extralines.data(), extralines.size()))
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
				&& args[i] == "run"
				)
			{
				std::cout << ">>> Running...\n";
				args.erase(args.cbegin(), args.cbegin() + 2);
				std::cout << soup::os::execute(soup::string::fixType(proj.getOutFile().u8string()), std::move(args));
			}

			return E_OK;
		}
		catch (const std::exception& e)
		{
			std::cout << e.what() << "\n";
			return E_EXCEPTION;
		}
	}
}

SOUP_MAIN_CLI(&entry);

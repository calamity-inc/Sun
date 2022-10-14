#include "foolib.hpp"

const char* foo()
{
	return "Hello, world!";
}

// On Linux systems, the default is to export everything.
// This can be adjusted with -fvisibility=hidden
// You can list exports with nm -D <.so file>
void oopsie_woopsie()
{
}

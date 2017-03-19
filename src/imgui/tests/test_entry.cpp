#include "client/client.h"
#include "core/debug.h"

#define CATCH_CONFIG_RUNNER
#include <catch.hpp>
#include <reporters/catch_reporter_teamcity.hpp>


int main(int argc, char* const argv[])
{
	Client::Initialize();

	auto RetVal = Catch::Session().run(argc, argv);
	if(Core::IsDebuggerAttached() && RetVal != 0)
	{
		DBG_BREAK;
	}
	Client::Finalize();
	return RetVal;
}

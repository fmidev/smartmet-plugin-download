#include "Plugin.h"
#include <spine/PluginTest.h>

using namespace std;

void prelude(SmartMet::Spine::Reactor& reactor)
{
  auto handlers = reactor.getURIMap();
  while (handlers.find("/download") == handlers.end())
  {
    sleep(1);
    handlers = reactor.getURIMap();
  }

  cout << endl << "Testing download plugin" << endl << "=======================" << endl;
}

int main()
{
  SmartMet::Spine::Options options;
  options.quiet = true;
  options.defaultlogging = false;
  options.configfile = "cnf/reactor.conf";

  SmartMet::Spine::PluginTest::test(options, prelude, true);

  return 0;
}

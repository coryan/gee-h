#include <gh/active_completion_queue.hpp>
#include <gh/detail/election_observer_impl.hpp>

#include <csignal>

namespace {
using observer_type = gh::detail::election_observer_impl<gh::completion_queue<>>;

bool interrupt = false;
extern "C" void sighdlr(int sig) {
  interrupt = true;
}
} // anonymous namespace

int main(int argc, char* argv[]) try {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <election-name>" << std::endl;
    return 1;
  }
  char const* election_name = argv[1];

  auto etcd_channel = grpc::CreateChannel("localhost:2379", grpc::InsecureChannelCredentials());

  gh::log::instance().add_sink(
      gh::make_log_sink([](gh::severity sev, std::string&& x) { std::cerr << x << std::endl; }));

  gh::active_completion_queue queue;
  std::shared_ptr<gh::election_observer> election_observer(new observer_type(
      election_name, queue.cq(), etcdserverpb::KV::NewStub(etcd_channel), etcdserverpb::Watch::NewStub(etcd_channel)));

  auto subscriber = [](std::string const& key, std::string const& value) {
    if (key.empty()) {
      std::cout << "no current leader" << std::endl;
      return;
    }
    std::cout << "current leader is " << key << ", with value=" << value << std::endl;
  };

  auto token = election_observer->subscribe(std::move(subscriber));
  election_observer->startup();

  // ... block here until a signal is received ...
  std::signal(SIGINT, &sighdlr);
  std::signal(SIGTERM, &sighdlr);
  using namespace std::chrono_literals;
  while (not interrupt) {
    std::this_thread::sleep_for(20ms);
  }

  election_observer->unsubscribe(token);
  election_observer->shutdown();

} catch (std::exception const& ex) {
  std::cerr << "std::exception raised: " << ex.what() << std::endl;
  return 1;
} catch (...) {
  std::cerr << "unknown exception raised" << std::endl;
  return 1;
}

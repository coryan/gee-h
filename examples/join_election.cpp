#include <gh/active_completion_queue.hpp>
#include <gh/leader_election.hpp>
#include <gh/log.hpp>

#include <csignal>

namespace {
bool interrupt = false;
extern "C" void signal_handler(int sig) {
  interrupt = true;
}
} // anonymous namespace

int main(int argc, char* argv[]) try {
  using namespace std::chrono_literals;

  if (argc != 3 and argc != 4) {
    std::cerr << "Usage: " << argv[0] << " <prefix> <value> [etcd-address]" << std::endl;
    return 1;
  }
  std::string election_name = argv[1];
  std::string value = argv[2];
  char const* etcd_address = argc == 3 ? "localhost:2379" : argv[3];

  auto etcd_channel = grpc::CreateChannel(etcd_address, grpc::InsecureChannelCredentials());

  gh::log::instance().add_sink(
      gh::make_log_sink([](gh::severity sev, std::string&& x) { std::cerr << x << std::endl; }));

  auto queue = std::make_shared<gh::active_completion_queue>();
  gh::leader_election candidate(queue, etcd_channel, election_name, value, 5s);
  auto fut = candidate.campaign();

  // ... block here until a signal is received ...
  std::signal(SIGINT, &signal_handler);
  std::signal(SIGTERM, &signal_handler);

  // ... loop until the candidate becomes elected or the user interrupts the election, whichever one is first ...
  auto r = fut.wait_for(20ms);
  while (not interrupt and r != std::future_status::ready) {
    r = fut.wait_for(20ms);
  }
  if (r == std::future_status::ready) {
    // ... there is a corner case where the election
    if (fut.get()) {
      std::cout << "candidate has won the election... wait for interrupt" << std::endl;
    } else {
      std::cout << "election was aborted, possibly because server has crashed... abort client" << std::endl;
      throw std::runtime_error("election aborted");
    }
  }
  // ... wait until the user interrupts the program, good for a demo, but this is not what most programs would do ...
  while (not interrupt) {
    std::this_thread::sleep_for(20ms);
  }

  candidate.resign();
} catch (std::exception const& ex) {
  std::cerr << "std::exception raised: " << ex.what() << std::endl;
  return 1;
}

#include <gh/active_completion_queue.hpp>
#include <gh/detail/election_candidate_impl.hpp>
#include <gh/detail/session_impl.hpp>

#include <csignal>

namespace {
using session_type = gh::detail::session_impl<gh::completion_queue<>>;
using candidate_type = gh::detail::election_candidate_impl<gh::completion_queue<>>;

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

  gh::active_completion_queue queue;
  std::shared_ptr<gh::session> session(new session_type(queue.cq(), etcdserverpb::Lease::NewStub(etcd_channel), 5s));

  std::shared_ptr<gh::election_candidate> election_candidate(new candidate_type(
      queue.cq(), session->lease_id(), etcdserverpb::KV::NewStub(etcd_channel),
      etcdserverpb::Watch::NewStub(etcd_channel), election_name, value));

  auto fut = election_candidate->campaign();

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

  election_candidate->resign();
} catch (std::exception const& ex) {
  std::cerr << "std::exception raised: " << ex.what() << std::endl;
  return 1;
}

/**
 * Observe the behavior of gRPC and etcd when the cluster has partial and total failures.
 *
 * The code for leader election needs to handle the case where a compaction takes place while the election is running.
 * This program demonstrates why this is necessary using a local etcd server.
 */
#include <gh/active_completion_queue.hpp>
#include <gh/assert_throw.hpp>
#include <gh/detail/session_impl.hpp>

#include <fstream>

namespace gh {
class etcd_client {
public:
  // TODO() the credentials should be an argument
  etcd_client(std::vector<std::string> targets)
    : targets_(std::move(targets))
    , current_channel_() {}

  std::shared_ptr<grpc::Channel> pick_channel() {
    using namespace std::chrono_literals;
    if (current_channel_ and current_channel_->GetState(false) == GRPC_CHANNEL_READY) {
      return current_channel_;
    }
    std::shared_ptr<grpc::Channel> channel;
    for (auto const& target : targets_) {
      auto etcd_channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
      auto maintenance = etcdserverpb::Maintenance::NewStub(etcd_channel);
      etcdserverpb::StatusRequest req;
      etcdserverpb::StatusResponse resp;
      grpc::ClientContext ctx;
      ctx.set_deadline(std::chrono::system_clock::now() + 50ms);
      auto status = maintenance->Status(&ctx, req, &resp);
      if (status.ok()) {
        if (resp.leader() == resp.header().member_id()) {
          channel = etcd_channel;
          break;
        }
      }
    }
    if (not channel) {
      // TODO() update list of channels and try again ...
      throw std::runtime_error("Cannot find any working cluster member");
    }
    current_channel_ = channel;
    return current_channel_;
  }

private:
  std::vector<std::string> targets_;
  std::shared_ptr<grpc::Channel> current_channel_;
};
}

namespace {
using session_type = gh::detail::session_impl<gh::completion_queue<>>;

mvccpb::KeyValue
create_node(gh::etcd_client& client, std::string const& key, std::string const& value);

void update_node(gh::etcd_client& client, std::string const& key, std::string const& value);

long delete_node(gh::etcd_client& client, std::string const& key);
} // anonymous namespace

int main(int argc, char* argv[]) try {
  // This is going to be a rather large main() function, the program should be thought of as script to demonstrate a
  // corner case in the system ...

  // ... parse the command-line arguments, not really much parsing ...
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <prefix> [etcd-address...]" << std::endl;
    return 1;
  }
  std::string prefix = argv[1];
  std::vector<std::string> endpoints;
  for (int i = 2; i != argc; ++i) {
    endpoints.emplace_back(argv[i]);
  }
  if (endpoints.empty()) {
    endpoints.emplace_back("localhost:22379");
  }
  gh::etcd_client client(endpoints);
  std::cout << "Client created" << std::endl;

  // ... enable suffix operators for time intervals ...
  using namespace std::chrono_literals;

  // ... ensure logs are sent to stderr ...
  gh::log::instance().add_sink(gh::make_log_sink([](gh::severity sev, std::string&& x) {
    if (sev > gh::severity::info) {
      std::cerr << x << std::endl;
    }
  }));

  // ... create a completion queue, and run its event loop in a separate thread ...
  gh::active_completion_queue queue;
  std::cout << "Queue created" << std::endl;

  // ... abort on I/O errors ...
  std::cin.exceptions(std::ios::failbit | std::ios::eofbit);
  std::cout.exceptions(std::ios::failbit | std::ios::eofbit);

  // ... wait for the driver input ...
  auto c = client.pick_channel();
  std::string input;
  std::cout << "Connected to etcd cluster, press <Enter> to continue" << std::endl;
  std::getline(std::cin, input);

  // ... create two nodes in the etcd server with the given prefixes ...
  auto make_key = [&prefix](char const* name) {
    std::ostringstream os;
    os << prefix << "/" << name;
    return os.str();
  };
  auto key0 = make_key("node0");
  auto key1 = make_key("node1");
  auto node0 = create_node(client, key0, "node0:0");
  auto node1 = create_node(client, key1, "node1:0");

  std::cout << "Nodes created, press <Enter> to continue" << std::endl;
  std::getline(std::cin, input);

  // ... make several changes to the first node ...
  try {
    update_node(client, key0, "node0:1");
  } catch(...) {
    update_node(client, key0, "node0:1");
  }

  std::cout << "Nodes updated, press <Enter> to continue" << std::endl;
  std::getline(std::cin, input);

  int count = 0;
  for (int i = 0; i != 5; ++i) {
    try {
      update_node(client, key1, "node1:2");
    } catch(...) {
      ++count;
    }
  }
  if (count != 5) {
    throw std::runtime_error("expected 5 failures");
  }
  std::cout << "Node updates failed, press <Enter> to continue" << std::endl;
  std::getline(std::cin, input);

  (void)delete_node(client, key1);
  (void)delete_node(client, key0);
} catch (std::exception const& ex) {
  std::cerr << "std::exception raised: " << ex.what() << std::endl;
  return 1;
}

namespace {
using namespace std::chrono_literals;

std::int64_t
create_lease(gh::etcd_client& client) {
  auto lease_stub = etcdserverpb::Lease::NewStub(client.pick_channel());
  etcdserverpb::LeaseGrantRequest req;
  req.set_ttl(30); // 30 seconds
  etcdserverpb::LeaseGrantResponse resp;
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() + 50ms);
  auto status = lease_stub->LeaseGrant(&ctx, req, &resp);
  gh::detail::check_grpc_status(status, "create_lease()");
  return resp.id();
}

mvccpb::KeyValue
create_node(gh::etcd_client& client, std::string const& key, std::string const& value) {
  auto kv_stub = etcdserverpb::KV::NewStub(client.pick_channel());

  auto lease_id = create_lease(client);

  // Make a request to modify the value if the node does not exist ...
  etcdserverpb::TxnRequest req;
  auto& cmp = *req.add_compare();
  cmp.set_key(key);
  cmp.set_result(etcdserverpb::Compare::EQUAL);
  cmp.set_target(etcdserverpb::Compare::CREATE);
  cmp.set_create_revision(0);
  // ... if the key is not there we are going to create it, and store the value there ...
  auto& on_success = *req.add_success()->mutable_request_put();
  on_success.set_key(key);
  on_success.set_value(value);
  on_success.set_lease(lease_id);

  // ... execute the transaction in etcd ...
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() + 50ms);
  etcdserverpb::TxnResponse resp;
  auto status = kv_stub->Txn(&ctx, req, &resp);
  gh::detail::check_grpc_status(status, "create_node()", key);
  if (not resp.succeeded()) {
    std::ostringstream os;
    os << "Cannot create node " << key;
    throw std::runtime_error(os.str());
  }
  mvccpb::KeyValue kv;
  kv.set_key(std::move(key));
  kv.set_value(value);
  kv.set_create_revision(resp.header().revision());
  return kv;
}

void update_node(gh::etcd_client& client, std::string const& key, std::string const& value) {
  auto kv_stub = etcdserverpb::KV::NewStub(client.pick_channel());
  etcdserverpb::PutRequest req;
  req.set_key(key);
  req.set_value(value);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() + 50ms);
  etcdserverpb::PutResponse resp;
  auto status = kv_stub->Put(&ctx, req, &resp);
  gh::detail::check_grpc_status(status, "update_node()", key, value);
}

long delete_node(gh::etcd_client& client, std::string const& key) {
  auto kv_stub = etcdserverpb::KV::NewStub(client.pick_channel());
  etcdserverpb::DeleteRangeRequest req;
  req.set_key(key);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() + 50ms);
  etcdserverpb::DeleteRangeResponse resp;
  auto status = kv_stub->DeleteRange(&ctx, req, &resp);
  gh::detail::check_grpc_status(status, "delete_node()", key);
  return resp.header().revision();
}

} // anonymous namespace

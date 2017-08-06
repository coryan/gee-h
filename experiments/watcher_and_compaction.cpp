/**
 * Demonstrate the impact of compactions on watchers.
 *
 * The code for leader election needs to handle the case where a compaction takes place while the election is running.
 * This program demonstrates why this is necessary using a local etcd server.
 */
#include <gh/active_completion_queue.hpp>
#include <gh/assert_throw.hpp>
#include <gh/detail/session_impl.hpp>

namespace {
using session_type = gh::detail::session_impl<gh::completion_queue<>>;

mvccpb::KeyValue create_node(
    std::unique_ptr<etcdserverpb::KV::Stub>& kv_stub, std::string const& key, std::uint64_t lease_id,
    std::string const& value);

void update_node(
    std::unique_ptr<etcdserverpb::KV::Stub>& kv_stub, std::string const& key, std::uint64_t lease_id,
    std::string const& value);

long delete_node(std::unique_ptr<etcdserverpb::KV::Stub>& kv_stub, std::string const& key);
} // anonymous namespace

int main(int argc, char* argv[]) try {
  // This is going to be a rather large main() function, the program should be thought of as script to demonstrate a
  // corner case in the system ...

  // ... parse the command-line arguments, not really much parsing ...
  if (argc != 2 and argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <prefix> [etcd-address]" << std::endl;
    return 1;
  }
  std::string prefix = argv[1];
  char const* etcd_address = argc == 2 ? "localhost:2379" : argv[2];

  // ... enable suffix operators for time intervals ...
  using namespace std::chrono_literals;

  // ... create a connection to the etcd server ...
  auto etcd_channel = grpc::CreateChannel(etcd_address, grpc::InsecureChannelCredentials());

  // ... ensure logs are sent to stderr ...
  gh::log::instance().add_sink(
      gh::make_log_sink([](gh::severity sev, std::string&& x) { std::cerr << x << std::endl; }));

  // ... create a completion queue, and run its event loop in a separate thread ...
  gh::active_completion_queue queue;

  // ... create a lease to automatically remove the nodes at the end of the program ...
  auto session = std::make_unique<session_type>(queue.cq(), etcdserverpb::Lease::NewStub(etcd_channel), 5s);

  // ... create a watcher stream ...
  auto watch_stub = etcdserverpb::Watch::NewStub(etcd_channel);
  grpc::ClientContext wctxt;
  auto watcher_stream = watch_stub->Watch(&wctxt);

  // ... create two nodes in the etcd server with the given prefixes ...
  auto kv_stub = etcdserverpb::KV::NewStub(etcd_channel);
  auto make_key = [&prefix, lease = session->lease_id() ](char const* name) {
    std::ostringstream os;
    os << prefix << name << "/" << std::hex << lease;
    return os.str();
  };
  auto key0 = make_key("/node0");
  auto key1 = make_key("/node1");
  auto node0 = create_node(kv_stub, key0, session->lease_id(), "node0:0");
  auto node1 = create_node(kv_stub, key1, session->lease_id(), "node1:0");

  // ... make several changes to the first node ...
  update_node(kv_stub, key0, session->lease_id(), "node0:1");
  update_node(kv_stub, key0, session->lease_id(), "node0:2");
  update_node(kv_stub, key0, session->lease_id(), "node0:3");
  update_node(kv_stub, key0, session->lease_id(), "node0:4");
  update_node(kv_stub, key0, session->lease_id(), "node0:5");

  // ... delete the first one of the nodes ....
  long revision = delete_node(kv_stub, key0);
  update_node(kv_stub, key1, session->lease_id(), "node1:1");

  // ... compact the log in the etcd server ...
  etcdserverpb::CompactionRequest compact;
  compact.set_revision(revision);
  compact.set_physical(true);
  grpc::ClientContext cctxt;
  etcdserverpb::CompactionResponse compact_response;
  auto status = kv_stub->Compact(&cctxt, compact, &compact_response);
  gh::detail::check_grpc_status(status, "main()");
  std::cout << "Compacted to revision=" << revision << ", response=" << gh::detail::print_to_stream(compact_response)
            << std::endl;

  // ... request a watcher from before the compaction point ...
  etcdserverpb::WatchRequest create_watcher;
  auto& create = *create_watcher.mutable_create_request();
  create.set_key(prefix + '/');
  create.set_range_end(prefix + char('/' + 1));
  create.set_start_revision(node0.create_revision());
  create.set_progress_notify(true);
  create.set_prev_kv(true);
  (void)watcher_stream->Write(create_watcher);

  // ... it is canceled ...
  etcdserverpb::WatchResponse watch_response;
  (void)watcher_stream->Read(&watch_response);
  std::cout << "Read() returned " << gh::detail::print_to_stream(watch_response) << std::endl;
  GH_ASSERT_THROW(watch_response.created());

  (void)watcher_stream->Read(&watch_response);
  std::cout << "Read() returned " << gh::detail::print_to_stream(watch_response) << std::endl;
  GH_ASSERT_THROW(watch_response.compact_revision() == revision);

  // ... request a watcher from the current point, including deletes and previous key-values ...
  create.set_key(prefix + '/');
  create.set_range_end(prefix + char('/' + 1));
  create.set_start_revision(revision);
  create.set_progress_notify(true);
  create.set_prev_kv(true);
  (void)watcher_stream->Write(create_watcher);

  // ... we miss info about the deleted node, if the first node was the predecessor in the leader election, the
  // election would stop making progress ...
  (void)watcher_stream->Read(&watch_response);
  std::cout << "Read() returned " << gh::detail::print_to_stream(watch_response) << std::endl;
  GH_ASSERT_THROW(watch_response.created());

  (void)watcher_stream->Read(&watch_response);
  std::cout << "Read() returned " << gh::detail::print_to_stream(watch_response) << std::endl;
  GH_ASSERT_THROW(watch_response.events_size() > 0);

  // ... cancel the watcher ...
  etcdserverpb::WatchRequest cxlreq;
  auto& cancel = *cxlreq.mutable_cancel_request();
  cancel.set_watch_id(watch_response.watch_id());
  (void)watcher_stream->Write(cxlreq);
  (void)watcher_stream->Read(&watch_response);
  std::cout << "Read() returned " << gh::detail::print_to_stream(watch_response) << std::endl;
  GH_ASSERT_THROW(watch_response.canceled());

  // ... close the session ...
  session->revoke();

} catch (std::exception const& ex) {
  std::cerr << "std::exception raised: " << ex.what() << std::endl;
  return 1;
}

namespace {

mvccpb::KeyValue create_node(
    std::unique_ptr<etcdserverpb::KV::Stub>& kv_stub, std::string const& key, std::uint64_t lease_id,
    std::string const& value) {

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
  etcdserverpb::TxnResponse resp;
  auto status = kv_stub->Txn(&ctx, req, &resp);
  gh::detail::check_grpc_status(status, "create_node()", key, lease_id);
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

void update_node(
    std::unique_ptr<etcdserverpb::KV::Stub>& kv_stub, std::string const& key, std::uint64_t lease_id,
    std::string const& value) {
  etcdserverpb::PutRequest req;
  req.set_key(key);
  req.set_value(value);
  grpc::ClientContext ctx;
  etcdserverpb::PutResponse resp;
  auto status = kv_stub->Put(&ctx, req, &resp);
  gh::detail::check_grpc_status(status, "update_node()", key, lease_id, value);
}

long delete_node(std::unique_ptr<etcdserverpb::KV::Stub>& kv_stub, std::string const& key) {
  etcdserverpb::DeleteRangeRequest req;
  req.set_key(key);
  grpc::ClientContext ctx;
  etcdserverpb::DeleteRangeResponse resp;
  auto status = kv_stub->DeleteRange(&ctx, req, &resp);
  gh::detail::check_grpc_status(status, "delete_node()", key);
  return resp.header().revision();
}

} // anonymous namespace
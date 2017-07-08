#include "gh/detail/grpc_errors.hpp"
#include <etcd/etcdserver/etcdserverpb/rpc.pb.h>

#include <gtest/gtest.h>

/**
 * @test Verify that check_rpc_status works as expected.
 */
TEST(grpc_errors, check_grpc_status_ok) {
  using namespace gh::detail;

  grpc::Status status = grpc::Status::OK;
  ASSERT_NO_THROW(check_grpc_status(status, "test"));

  etcdserverpb::LeaseKeepAliveRequest req;
  ASSERT_NO_THROW(check_grpc_status(status, "test", " in iteration=", 42, ", request=", print_to_stream(req)));
}

/**
 * @test Verify that check_rpc_status throws what is expected.
 */
TEST(grpc_errors, check_grpc_status_error_annotations) {
  using namespace gh::detail;

  try {
    grpc::Status status(grpc::UNKNOWN, "bad thing");
    etcdserverpb::LeaseKeepAliveRequest req;
    req.set_id(42);
    check_grpc_status(status, "test", " request=", print_to_stream(req));
  } catch (std::runtime_error const& ex) {
    std::string const expected =
        R"""(test grpc error: bad thing [2] request=ID: 42
)""";
    ASSERT_EQ(ex.what(), expected);
  }
}

/**
 * @test Verify that check_rpc_status throws what is expected.
 */
TEST(grpc_errors, check_grpc_status_error_bare) {
  using namespace gh::detail;
  try {
    grpc::Status status(grpc::UNKNOWN, "bad thing");
    etcdserverpb::LeaseKeepAliveRequest req;
    req.set_id(42);
    check_grpc_status(status, "test");
  } catch (std::runtime_error const& ex) {
    std::string const expected =
        R"""(test grpc error: bad thing [2])""";
    ASSERT_EQ(ex.what(), expected);
  }
}

/**
 * @test Verify that print_to_stream works as expected.
 */
TEST(grpc_errors, print_to_stream_basic) {
  using namespace gh::detail;

  etcdserverpb::LeaseKeepAliveRequest req;
  req.set_id(42);

  std::string expected = R"""(ID: 42
)""";
  std::ostringstream os;
  os << print_to_stream(req);
  auto actual = os.str();
  ASSERT_EQ(actual, expected);
}

#include <gh/completion_queue.hpp>
#include <gh/detail/async_rpc_op.hpp>
#include <gh/detail/backoff_strategy.hpp>
#include <gh/detail/default_grpc_interceptor.hpp>
#include <etcd/etcdserver/etcdserverpb/rpc.grpc.pb.h>
#include <grpc++/grpc++.h>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

namespace gh {

class etcd_client;

class retry_policy {
public:
  retry_policy();

  std::chrono::milliseconds on_failure(etcd_client& client, grpc::Status status) {
    return impl_->on_failure(client, status);
  }

  class impl {
  public:
    virtual ~impl() = 0;
    virtual std::chrono::milliseconds on_failure(etcd_client& client, grpc::Status status) = 0;
  };

private:
  std::shared_ptr<impl> impl_;
};

class etcd_client {
public:
  virtual ~etcd_client() = 0;

  virtual etcdserverpb::LeaseGrantResponse grant_lease(etcdserverpb::LeaseGrantRequest r, retry_policy policy) = 0;

  virtual std::future<etcdserverpb::LeaseGrantResponse>
  async_grant_lease(etcdserverpb::LeaseGrantRequest r, retry_policy policy) = 0;
};

namespace detail {
using queue = gh::completion_queue<>;

using channel_ptr = std::shared_ptr<grpc::Channel>;

// A pending client operation ...
template <typename Result>
class etcd_client_op {
public:
  virtual bool attempt_failed(grpc::Status const& status) = 0;
  virtual void attempt_successful(Result&& result) = 0;
  virtual std::chrono::milliseconds current_delay() const = 0;
};

template <typename Result>
class callback_client_op : public etcd_client_op<Result> {
public:
  callback_client_op(std::function<void(Result&&)>&& callback);

  virtual void attempt_successful(Result&& r) override {
    callback_(std::move(r));
  }

  std::function<void(Result&&)> callback_;
};

template <typename Result>
class promise_client_op : public etcd_client_op<Result> {
public:
  promise_client_op();

  virtual void attempt_successful(Result&& r) override {
    promise_.set_value(std::move(r));
  }

  std::promise<Result> promise_;
};

class channel_source {
public:
  channel_source(std::shared_ptr<grpc::Channel> current, std::vector<std::string> urls,
                 std::shared_ptr<grpc::ChannelCredentials> creds)
    : current_(current)
    , urls_(urls)
    , creds_(creds)
    , index_(0) {
  }

  std::shared_ptr<grpc::Channel> current() const {
    return current_;
  }
  std::shared_ptr<grpc::Channel> next_channel() {
    auto channel = grpc::CreateChannel(urls_[index_], creds_);
    if (++index_ >= urls_.size()) {
      index_ = 0;
    }
    return channel;
  }

private:
  std::shared_ptr<grpc::Channel> current_;
  std::vector<std::string> urls_;
  std::shared_ptr<grpc::ChannelCredentials> creds_;
  int index_;
};

/**
 * Implement a etcd_client.
 *
 * There are two levels of abstraction to deal with:
 *
 */
class etcd_client_impl : public etcd_client {
public:
  etcdserverpb::LeaseGrantResponse grant_lease(etcdserverpb::LeaseGrantRequest request, retry_policy policy) override {
    auto future = async_grant_lease(std::move(request), policy);
    return future.get();
  }

  std::future<etcdserverpb::LeaseGrantResponse>
  async_grant_lease(etcdserverpb::LeaseGrantRequest request, retry_policy policy) override {
    auto op = std::make_shared<promise_client_op<etcdserverpb::LeaseGrantResponse>>();
    grant_lease_schedule(op, std::move(request), policy);
    return op->promise_.get_future();
  }

private:
  std::shared_ptr<channel_source> channels() {
    return std::make_shared<channel_source>(channel_, urls_, credentials_);
  }

  /// The wrapper around a single RPC.
  using grant_lease_rpc = async_rpc_op<etcdserverpb::LeaseGrantRequest, etcdserverpb::LeaseGrantResponse>;

  void grant_lease_result(
    std::shared_ptr<etcd_client_op<etcdserverpb::LeaseGrantResponse>> op, grant_lease_rpc const& rpc, bool fok,
    retry_policy& ctx) {
    if (fok) {
      // TODO() - we should eliminate this copy ...
      auto response = rpc.response;
      return op->attempt_successful(std::move(response));
    }
    if (op->attempt_failed()) {
      // TODO() - this should be delayed using op->current_delay() ...
      return grant_lease_schedule(op, rpc.request, ctx);
    }
  }

  void grant_lease_schedule(
      std::shared_ptr<etcd_client_op<etcdserverpb::LeaseGrantResponse>> op, etcdserverpb::LeaseGrantRequest request,
      retry_policy ctx) {
    auto callback = [this, ctx, op](grant_lease_rpc const& rpc, bool fok) {
      this->grant_lease_result(op, rpc, fok, ctx);
    };
    find_working_channel(ctx, [ this, op, ctx, r = std::move(request), cb = std::move(callback) ](channel_ptr ch) {
      auto lc = etcdserverpb::Lease::NewStub(ch);
      etcdserverpb::LeaseGrantRequest request(r);
      this->queue_.async_rpc(
          lc.get(), &etcdserverpb::Lease::Stub::AsyncLeaseGrant, std::move(request), ctx->op_name(), cb);
    });
  }


  template <typename Functor>
  void find_working_channel(retry_policy& ctx, Functor&& f) {
    auto ch = get_current_channel();
    if (ch) {
      return f(ch);
    }

    find_working_url(ctx, std::move(f), 0);
  }

  using url_functor = std::function<void(channel_ptr)>;

  void find_working_url(retry_policy& ctx, url_functor fu, std::size_t index) {
    if (index >= urls_.size()) {
      if (not ctx->record_failure()) {
        fu(channel_ptr());
      }
      auto delay = ctx->current_delay();
      return reset_retry_timer(ctx, fu, delay);
    }
    auto channel = grpc::CreateChannel(urls_[index], credentials_);
    auto maintenance = etcdserverpb::Maintenance::NewStub(channel);
    etcdserverpb::StatusRequest request;
    queue_.async_rpc(
        maintenance.get(), &etcdserverpb::Maintenance::Stub::AsyncStatus, std::move(request), "etcd_cluster_op/status",
        [this, index, fu, channel, ctx](auto& fop, bool fok) {
          this->on_status_response(ctx, fu, index, channel, fop, fok);
        });
  }

  void reset_retry_timer(retry_policy& ctx, url_functor functor, std::chrono::milliseconds delay) {
    queue_.make_relative_timer(delay, "etcd_cluster_op/retry", [this, ctx, functor](auto& op, bool ok) {
      this->on_retry_expired(ctx, functor, ok);
    });
  }

  void on_retry_expired(retry_policy& ctx, url_functor functor, bool ok) {
    if (not ok) {
      functor(channel_ptr());
      return;
    }
    find_working_url(ctx, functor, 0);
  }

  using status_op = async_rpc_op<etcdserverpb::StatusRequest, etcdserverpb::StatusResponse>;

  void on_status_response(
      retry_policy& ctx, url_functor functor, std::size_t index, std::shared_ptr<grpc::Channel> channel,
      status_op const& op, bool ok) {
    if (not ok) {
      ctx->record_failure();
      set_current_channel(channel_ptr());
      return find_working_url(ctx, functor, index + 1);
    }
    ctx->record_success();
    set_current_channel(channel);
    functor(channel);
  }

  channel_ptr get_current_channel() {
    std::lock_guard<std::mutex> lk(mu_);
    return channel_;
  }

  void set_current_channel(channel_ptr ch) {
    std::lock_guard<std::mutex> lk(mu_);
    channel_ = ch;
  }

private:
  queue queue_;
  std::mutex mu_;
  channel_ptr channel_;
  std::shared_ptr<grpc::ChannelCredentials> credentials_;
  std::vector<std::string> urls_;
};
} // namespace detail
} // namespace gh

TEST(etcd_client_impl, basic) {
  using namespace std::chrono_literals;
  gh::detail::etcd_client_impl client;
}

TEST(etcd_client_impl, sync_call) {
  using namespace std::chrono_literals;
  gh::detail::etcd_client_impl client;
}

namespace gh {
retry_policy::impl::~impl() {
}

class default_retry_policy : public retry_policy::impl {
public:
  default_retry_policy(
      std::chrono::milliseconds maximum_delay, std::chrono::milliseconds initial_delay, int maximum_failures)
      : maximum_delay_(maximum_delay)
      , current_delay_(initial_delay)
      , maximum_failures_(maximum_failures)
      , current_failures_(0) {
  }

  std::chrono::milliseconds on_failure(etcd_client&, grpc::Status status) override {
    if (++current_failures_ > maximum_failures_) {
      throw std::runtime_error("too many failures");
    }
    auto current = current_delay_;
    current_delay_ *= 2;
    if (current_delay_ > maximum_delay_) {
      current_delay_ = maximum_delay_;
    }
    return current;
  }

private:
  std::chrono::milliseconds maximum_delay_;
  std::chrono::milliseconds current_delay_;
  int maximum_failures_;
  int current_failures_;
};

using namespace std::chrono_literals;
retry_policy::retry_policy()
    : impl_(new default_retry_policy(10ms, 15min, 100)) {
}
} // namespace gh

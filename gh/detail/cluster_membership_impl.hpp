#ifndef gh_detail_cluster_membership_impl_hpp
#define gh_detail_cluster_membership_impl_hpp

#include <gh/completion_queue.hpp>
#include <gh/detail/async_rpc_op.hpp>
#include <gh/detail/rpc_backoff_policy.hpp>
#include <gh/detail/deadline_timer.hpp>
#include <gh/detail/default_grpc_interceptor.hpp>
#include <etcd/etcdserver/etcdserverpb/rpc.grpc.pb.h>
#include <grpc++/grpc++.h>
#include <mutex>
#include <set>
#include <sstream>
#include <string>

namespace gh {
namespace detail {
/**
 * Keep track of a etcd cluster membership.
 *
 * @tparam completion_queue_interceptor_type a dependency injection point to control the type of completion queue,
 * mostly used in testing/mocking.
 */
template <typename completion_queue_interceptor_type = gh::detail::default_grpc_interceptor>
class cluster_membership_impl {
public:
  //@{
  /// @name type traits

  /// The type of completion queue based on the interceptor type.
  using completion_queue_type = gh::completion_queue<completion_queue_interceptor_type>;

  /// The asynchronous operation used to request the list of members.
  using member_list_op = gh::detail::async_rpc_op<etcdserverpb::MemberListRequest, etcdserverpb::MemberListResponse>;
  //@}

  /// The minimum interval between refresh attempts.
  static std::chrono::milliseconds constexpr min_refresh_interval{5000};

  template <typename duration_type>
  cluster_membership_impl(
      completion_queue_type& queue, std::shared_ptr<grpc::ChannelCredentials> credentials,
      std::shared_ptr<rpc_backoff_policy> backoff, std::string const& url, duration_type refresh_interval)
      : queue_(queue)
      , credentials_(std::move(credentials))
      , backoff_strategy_(std::move(backoff))
      , refresh_interval_(std::chrono::duration_cast<std::chrono::microseconds>(refresh_interval)) {
    member_urls_.insert(url);
    validate_arguments();
  }

  template <typename duration_type>
  cluster_membership_impl(
      completion_queue_type& queue, std::shared_ptr<grpc::ChannelCredentials> credentials,
      std::shared_ptr<rpc_backoff_policy> backoff, std::initializer_list<std::string>&& l, duration_type refresh_interval)
      : queue_(queue)
      , credentials_(std::move(credentials))
      , backoff_strategy_(std::move(backoff))
      , member_urls_(l)
      , refresh_interval_(std::chrono::duration_cast<std::chrono::microseconds>(refresh_interval)) {
    validate_arguments();
  }

  void startup() {
    reset_refresh_timer();
  }

  void shutdown() {
    if (refresh_timer_) {
      refresh_timer_->cancel();
    }
  }

  std::vector<std::string> current_urls() const {
    std::lock_guard<std::mutex> lock(mu_);
    return std::vector<std::string>(member_urls_.begin(), member_urls_.end());
  }

private:
  void validate_arguments() {
    using namespace std::chrono;
    if (refresh_interval_ <= min_refresh_interval) {
      std::ostringstream os;
      os << "cluster_membership_impl<>() - refresh interval (" << duration_cast<milliseconds>(refresh_interval_).count()
         << "ms) too short, should be >= " << min_refresh_interval.count() << "ms";
      throw std::invalid_argument(os.str());
    }
  }

  void on_refresh_expired(bool ok) {
    refresh_timer_.reset();
    if (not ok) {
      return;
    }
    auto urls = current_urls();
    try_query_cluster_membership(std::move(urls), 0);
  }

  void try_query_cluster_membership(std::vector<std::string> urls, std::size_t i) {
    if (i >= urls.size()) {
      backoff_strategy_->on_failure();
      return reset_refresh_timer();
    }
    auto channel = grpc::CreateChannel(urls[i], credentials_);
    auto cluster = etcdserverpb::Cluster::NewStub(channel);
    etcdserverpb::MemberListRequest req;
    queue_.async_rpc(
        cluster.get(), &etcdserverpb::Cluster::Stub::AsyncMemberList, std::move(req), "cluster_membership/member_list",
        [ this, u = std::move(urls), i ](auto const& op, bool ok) { this->on_member_list_response(u, i, op, ok); });
  }

  void on_member_list_response(std::vector<std::string> urls, std::size_t i, member_list_op const& rop, bool rok) {
    if (not rok or not rop.status.ok()) {
      return try_query_cluster_membership(std::move(urls), i + 1);
    }
    std::set<std::string> new_urls;
    for (auto const& member : rop.response.members()) {
      for (auto const& url : member.clienturls()) {
        new_urls.insert(url);
      }
    }
    if (new_urls.empty()) {
      return try_query_cluster_membership(std::move(urls), i + 1);
    }
    std::lock_guard<std::mutex> lock(mu_);
    member_urls_.swap(new_urls);
    reset_refresh_timer();
  }

  void reset_refresh_timer() {
    refresh_timer_ =
        queue_.make_relative_timer(refresh_interval_, "cluster_membership/refresh_timer", [this](auto& op, bool ok) {
          this->on_refresh_expired(ok);
        });
  }

private:
  completion_queue_type& queue_;
  std::shared_ptr<grpc::ChannelCredentials> credentials_;
  std::shared_ptr<rpc_backoff_policy> backoff_strategy_;

  mutable std::mutex mu_;
  std::set<std::string> member_urls_;
  std::chrono::microseconds refresh_interval_;
  std::shared_ptr<deadline_timer> refresh_timer_;
};

template <typename completion_queue_interceptor_type>
std::chrono::milliseconds constexpr cluster_membership_impl<completion_queue_interceptor_type>::min_refresh_interval;
} // namespace detail
} // namespace gh

#endif // gh_detail_cluster_membership_impl_hpp

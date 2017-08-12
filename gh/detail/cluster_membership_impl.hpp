#ifndef gh_detail_cluster_membership_impl_hpp
#define gh_detail_cluster_membership_impl_hpp

#include <gh/detail/async_rpc_op.hpp>
#include <gh/detail/backoff_strategy.hpp>
#include <gh/detail/deadline_timer.hpp>
#include <etcd/etcdserver/etcdserverpb/rpc.grpc.pb.h>
#include <grpc++/grpc++.h>
#include <mutex>
#include <set>
#include <string>

namespace gh {
namespace detail {
/**
 * Keep track of a etcd cluster membership.
 *
 * @tparam completion_queue_type a dependency injection point to mock the completion queue.
 */
template <typename completion_queue_type>
class cluster_membership_impl {
public:
  cluster_membership_impl(
      completion_queue_type& queue, std::shared_ptr<grpc::ChannelCredentials> credentials,
      std::shared_ptr<backoff_strategy> backoff, std::string const& url)
      : queue_(queue)
      , credentials_(std::move(credentials))
      , backoff_strategy_(backoff)
      , mu_()
      , member_urls_() {
    member_urls_.insert(url);
  }

  cluster_membership_impl(
      completion_queue_type& queue, std::shared_ptr<grpc::ChannelCredentials> credentials,
      std::shared_ptr<backoff_strategy> backoff,
      std::initializer_list<std::string>&& l)
      : queue_(queue)
      , credentials_(std::move(credentials))
      , backoff_strategy_(backoff)
      , mu_()
      , member_urls_(l) {
  }

  void startup() {
    auto delay = backoff_strategy_->current_delay();
    current_timer_ = queue_.make_relative_timer(
        delay, "cluster_membership/timer", [this](auto const& op, bool ok) { this->on_refresh_expired(ok); });
  }

  std::vector<std::string> current_urls() const {
    std::lock_guard<std::mutex> lock(mu_);
    return std::vector<std::string>(member_urls_.begin(), member_urls_.end());
  }

private:
  void on_refresh_expired(bool ok) {
    if (not ok) {
      return;
    }
    auto urls = current_urls();
    try_query_cluster_membership(std::move(urls), 0);
  }

  void try_query_cluster_membership(std::vector<std::string> urls, std::size_t i) {
    if (i >= urls.size()) {
      return reset_refresh_timer();
    }
    auto channel = grpc::CreateChannel(urls[i], credentials_);
    auto cluster = etcdserverpb::Cluster::NewStub(channel);
    etcdserverpb::MemberListRequest req;
    queue_.async_rpc(
        cluster.get(), &etcdserverpb::Cluster::Stub::AsyncMemberList, std::move(req), "cluster_membership/member_list",
        [ this, u = std::move(urls), i ](auto const& op, bool ok) { this->on_member_list_response(u, i, op, ok); });
  }

  using member_list_op = gh::detail::async_rpc_op<etcdserverpb::MemberListRequest, etcdserverpb::MemberListResponse>;

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
    backoff_strategy_->record_success();
  }

  void reset_refresh_timer() {
    auto delay = record_failure();
    current_timer_ = queue_.make_relative_timer(
        delay, "cluster_membership/timer", [this](auto& op, bool ok) { this->on_refresh_expired(ok); });
  }

  std::chrono::milliseconds record_failure() {
    std::lock_guard<std::mutex> lock(mu_);
    return backoff_strategy_->record_failure();
  }

private:
  completion_queue_type& queue_;
  std::shared_ptr<grpc::ChannelCredentials> credentials_;
  std::shared_ptr<backoff_strategy> backoff_strategy_;

  mutable std::mutex mu_;
  std::set<std::string> member_urls_;
  std::shared_ptr<deadline_timer> current_timer_;
};
} // namespace detail
} // namespace gh

#endif // gh_detail_cluster_membership_impl_hpp

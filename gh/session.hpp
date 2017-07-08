#ifndef gh_session_hpp
#define gh_session_hpp

#include <chrono>
#include <cstdint>

namespace gh {

/**
 * Define the interface for a session, an abstraction to create and maintain etcd leases.
 */
class session {
public:
  /**
   * Destroy a session, releasing only local resources.
   *
   * The destructor just ensures that local resources (threads, RPC streams, etc.) are released.  No attempt is made
   * to release resources in etcd, such as the lease.  If the application wants to release the resources it should
   * call revoke() before destroying the object.
   */
  ~session() noexcept(false);

  /**
   * The session's lease.
   *
   * The lease may expire or otherwise become invalid while the session is shutting down.  Applications should avoid
   * using the lease after calling initiate_shutdown().
   */
  virtual std::uint64_t lease_id() const = 0;

  virtual std::chrono::milliseconds actual_TTL() const = 0;

  /**
   * Requests the lease to be revoked.
   *
   * If successful, all pending keep alive operations have been
   * canceled and the lease is revoked on the server.
   */
  virtual void revoke() = 0;
};

} // namespace gh

#endif // gh_session_hpp

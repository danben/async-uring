#ifndef ASYNCIO_H
#define ASYNCIO_H

#include <functional>

#include <liburing.h>
#include <linux/time_types.h>
#include <sys/socket.h>

#include "deferred.h"
#include "scheduler.h"

using namespace std;

typedef Deferred<int32_t> scall_res;

class Asyncio {
	public:
		// General file IO
		static scall_res read(int fd, void* buf, size_t count) {
			return io_uring_op([fd, buf, count](io_uring_sqe* sqe) {
					io_uring_prep_read(sqe, fd, buf, count, -1);
					});
		}

		static scall_res readv(int fd, const iovec* iovecs, unsigned nr_vecs, uint64_t offset) {
			return io_uring_op([fd, iovecs, nr_vecs, offset](io_uring_sqe* sqe) {
					io_uring_prep_readv(sqe, fd, iovecs, nr_vecs, offset);
					});
		}

		static scall_res write(int fd, void* buf, size_t count) {
			return io_uring_op([fd, buf, count](io_uring_sqe* sqe) {
					io_uring_prep_write(sqe, fd, buf, count, -1);
					});
		}

		static scall_res writev(int fd, const iovec* iovecs, unsigned nr_vecs, uint64_t offset) {
			return io_uring_op([fd, iovecs, nr_vecs, offset](io_uring_sqe* sqe) {
					io_uring_prep_writev(sqe, fd, iovecs, nr_vecs, offset);
					});
		}

		static scall_res close(int fd) {
			return io_uring_op([fd](io_uring_sqe* sqe) {
					io_uring_prep_close(sqe, fd);
					});
		}

		static scall_res fsync(int fd) {
			return io_uring_op([fd](io_uring_sqe* sqe) {
					io_uring_prep_fsync(sqe, fd, 0);
					});
		}

		// Socket IO
		static scall_res socket(int domain, int type, int protocol) {
			return io_uring_op([domain, type, protocol](io_uring_sqe* sqe) {
					io_uring_prep_socket(sqe, domain, type, protocol, 0);
					});
		}

		static scall_res accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
			return io_uring_op([sockfd, addr, addrlen](io_uring_sqe* sqe) {
					io_uring_prep_accept(sqe, sockfd, addr, addrlen, 0);
					});
		}

		static scall_res connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
			return io_uring_op([sockfd, addr, addrlen](io_uring_sqe* sqe) {
					io_uring_prep_connect(sqe, sockfd, addr, addrlen);
					});
		}

		static scall_res recv(int sockfd, void* buf, size_t len) {
			return io_uring_op([sockfd, buf, len](io_uring_sqe* sqe) {
					io_uring_prep_recv(sqe, sockfd, buf, len, 0);
					});
		}

		static scall_res recvmsg(int fd, msghdr* msg) {
			return io_uring_op([fd, msg](io_uring_sqe* sqe) {
					io_uring_prep_recvmsg(sqe, fd, msg, 0);
					});
		}

		static scall_res send(int sockfd, void* buf, size_t len) {
			return io_uring_op([sockfd, buf, len](io_uring_sqe* sqe) {
					io_uring_prep_send(sqe, sockfd, buf, len, 0);
					});
		}

		static scall_res sendmsg(int fd, msghdr* msg) {
			return io_uring_op([fd, msg](io_uring_sqe* sqe) {
					io_uring_prep_sendmsg(sqe, fd, msg, 0);
					});
		}


		// Timers
		static Deferred<std::monostate> sleep(uint64_t seconds) {
			__kernel_timespec ts { };
			ts.tv_sec = seconds;
			return io_uring_op([seconds, &ts](io_uring_sqe* sqe) mutable {
					// TODO: figure out why IORING_TIMEOUT_ETIME_SUCCESS isn't working
					io_uring_prep_timeout(sqe, &ts, 0, IORING_TIMEOUT_ETIME_SUCCESS);
					}).ignore();
		}
		

	private:
		static scall_res io_uring_op(function<void(io_uring_sqe*)> f) {
			scall_res ret {};
			auto scheduler = Scheduler::get();

			// Get an sqe, set it up with user data pointing to this
			// deferred. Hand it over to the sq.
			io_uring_sqe *sqe = scheduler->get_sqe();
			f(sqe);
			io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(ret.wrapped.get()));
			scheduler->submit();
			return ret;
		}
};

#endif

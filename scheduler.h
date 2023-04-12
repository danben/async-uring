#ifndef SCHEDULER_H
#define SCHEDULER_H
#include <cstdio>
#include <cstring>
#include <deque>
#include <iostream>
#include <functional>
#include <optional>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <variant>

#include <liburing.h>

using namespace std;

static constexpr uint32_t QUEUE_DEPTH { 1000 };

class JobConcept {
	public:
	virtual void run() = 0;
};

template <typename T>
class Job : public JobConcept {
	/* A Job is a thunk paired with its input value,
	 * ready to run in the scheduler. The JobConcept
	 * base class is used for type erasure, so we
	 * can put Jobs with different function types
	 * into a single work queue.
	 */
	public:
		Job<T>(function<void(T)> f, T a) : f(f), a(a) {}

		void run() override {
			f(a);
		}

	private:
		function<void(T)> f;
		T a;
};

class IvarConcept {};

template <typename T>
class Ivar : public IvarConcept {
	public:
		Ivar() = default;
		Ivar(const Ivar&) = delete;
		Ivar& operator=(const Ivar&) = delete;
		Ivar(Ivar&&) = default;
		Ivar& operator=(Ivar&&) = default;

		bool is_full();

		T value();

		void fill(T t);

		Ivar(T t);
		deque<function<void(T)>> handlers {};

	private:
	optional<T> val { };
};

template <typename T>
class Deferred;

class Scheduler {
	public:
		Scheduler(const Scheduler&) = delete;
		Scheduler& operator=(const Scheduler&) = delete;

		static Scheduler* get();
		static void run_until_shutdown();
		static void shutdown();

		template<typename T>
		friend class Deferred;
		friend class Asyncio;

	private:
		Scheduler() {
			io_uring_queue_init(QUEUE_DEPTH, &(this->ring), 0);
		};
		inline static Scheduler* singleton { nullptr };
		deque<unique_ptr<JobConcept>> jobs {};
		unordered_set<shared_ptr<IvarConcept>> pending_deferreds;
		io_uring ring;
		io_uring_cqe* cqe;
		bool shutdown_requested { false };

		template<typename T>
		friend class Ivar;

		void do_cycle();
		io_uring_sqe* get_sqe();
		void submit();

		template<typename T>
		void enqueue(function<void(T)>, T);

};

/**
 * BEGIN IVAR IMPLEMENTATION
 */

template <typename T>
Ivar<T>::Ivar(T t) : val(t) { }

template <typename T>
bool Ivar<T>::is_full() {
	return val.has_value();
}

template <typename T>
T Ivar<T>::value() {
	return val.value();
}

template <typename T>
void Ivar<T>::fill(T t) {
	if (is_full()) {
		throw invalid_argument("Ivar is already full");
	} else {
		val = t;
		auto scheduler = Scheduler::get();
		for (auto handler : handlers) {
			scheduler->enqueue(handler, t);
		}
		handlers.clear();
	}
}
/**
 * END IVAR IMPLEMENTATION
 */

/**
 * BEGIN SCHEDULER IMPLEMENTATION
 */
Scheduler* Scheduler::get() {
	if (singleton == nullptr) {
		singleton = new Scheduler();
	}
	return singleton;
}

void Scheduler::run_until_shutdown() {
	auto scheduler = Scheduler::get();
	while (!scheduler->shutdown_requested) {
		scheduler->do_cycle();
	}
}

void Scheduler::shutdown() {
	get()->shutdown_requested = true;
}

void Scheduler::do_cycle() {
	auto jobs_in_this_cycle = jobs.size();
	auto num_cqe_entries_ready = io_uring_cq_ready(&ring);
	if (jobs_in_this_cycle == 0 && num_cqe_entries_ready == 0) {
		auto ret = io_uring_wait_cqe(&ring, &cqe);
		if (ret < 0) {
			perror("io_uring_wait_cqe");	
		}
	} else {
		for (int i = 0; i < jobs_in_this_cycle; ++i) {
			jobs.front()->run();
			jobs.pop_front();
		}
		while (num_cqe_entries_ready > 0) {
			--num_cqe_entries_ready;
			auto ret = io_uring_wait_cqe(&ring, &cqe);
			if (ret < 0) {
				perror("io_uring_wait_cqe");	
			}
			auto d = reinterpret_cast<Ivar<int32_t>*>(io_uring_cqe_get_data(cqe));
			auto res = cqe->res;

			// TODO: improve error handling by tying the error message to the
			// specific request and raising an exception
			if (res < 0) {
				printf("In scheduler: %s\n", strerror(-res));
			}
			io_uring_cqe_seen(&ring, cqe);
			d->fill(res);
		}
	}
}

template<typename T>
void Scheduler::enqueue(function<void(T)> f, T a) {
	jobs.push_back(make_unique<Job<T>>(f, a));
}

io_uring_sqe* Scheduler::get_sqe() {
	return io_uring_get_sqe(&(get()->ring));
}

void Scheduler::submit() {
	io_uring_submit(&ring);
}

/**
 * END SCHEDULER IMPLEMENTATION
 */
#endif

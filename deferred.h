#ifndef DEFERRED_H
#define DEFERRED_H

#include <cstdio>
#include <deque>
#include <iostream>
#include <functional>
#include <optional>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <variant>

#include <liburing.h>
#include "scheduler.h"

using namespace std;

static constexpr monostate _monostate {};

template <typename T>
class Deferred {
	public:
		Deferred();

		Deferred(shared_ptr<Ivar<T>>);
		Deferred(T);

		void fill(T);
		void upon(function<void(T)>);
		bool is_full();

		template<typename R>
		Deferred<R> bind(function<Deferred<R>(T)>);

		template<typename R>
		Deferred<R> map(function<R(T)>);

		Deferred<monostate> ignore();

	private:
		friend class Asyncio;
		shared_ptr<Ivar<T>> wrapped;
};


template<typename T>
Deferred<T>::Deferred() : wrapped(make_shared<Ivar<T>>()) {
	Scheduler::get()->pending_deferreds.insert(wrapped);
}

template<typename T>
Deferred<T>::Deferred(shared_ptr<Ivar<T>> wrapped) : wrapped(wrapped) {
	if (!wrapped->is_full()) {
		Scheduler::get()->pending_deferreds.insert(wrapped);
	}
}

template<typename T>
Deferred<T>::Deferred(T val) : wrapped(make_shared<Ivar<T>>(val)) { }

template<typename T>
void Deferred<T>::fill(T t) {
	if (wrapped->is_full()) {
		throw invalid_argument("Deferred: ivar is already full");
	}

	wrapped->fill(t);
	Scheduler::get()->pending_deferreds.erase(wrapped);
}

template<typename T>
bool Deferred<T>::is_full() {
	return wrapped->is_full();
}

template<typename T>
void Deferred<T>::upon(function<void(T)> f) {
	if (wrapped->is_full()) {
		Scheduler::get()->enqueue(f, wrapped->value());
	} else {
		wrapped->handlers.push_back(f);
	}
}

template<typename T>
template<typename R>
Deferred<R> Deferred<T>::bind(function<Deferred<R>(T)> f) {
	Deferred<R> ret {};

	upon([ret, f](T t) mutable {
		f(t).upon([ret](R r) mutable { ret.fill(r); });
	});

	return ret;
}

template<typename T>
template<typename R>
Deferred<R> Deferred<T>::map(function<R(T)> f) {
	function<Deferred<R>(T)> g { [f](T t) { return Deferred<R>{ f(t) }; } };
	return bind(g);
}


template <typename T>
Deferred<monostate> Deferred<T>::ignore() {
	function<monostate(T)> f { [](T t) { return _monostate; } };
	return map(f);
}
#endif

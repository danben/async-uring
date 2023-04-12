#include <ctime>
#include <iostream>
#include "asyncio.h"
#include "deferred.h"
#include "scheduler.h"

using namespace std;

void simple_math() {
	auto d = Deferred<uint32_t>();
	auto e = d.bind<uint32_t>([](uint32_t x) { return Deferred<uint32_t>(x * 2); });
	e.upon([](uint32_t x) { cout << "x is " << x << endl; });
	d.fill(7);
}

void read_from_file() {
	auto fd = open("test_file", O_RDONLY);
	char buf[10];
	auto f = Asyncio::read(fd, &buf, 9);
	buf[10] = '\0';
	f.upon([buf](int32_t bytes_read) { printf("Read %d bytes into buf: %s\n", bytes_read, buf); });
}

Deferred<monostate> print_time_and_sleep(int seconds) {
	cout << time(0) << endl;
	return Asyncio::sleep(seconds);
}

void print_some_times() {
	print_time_and_sleep(3)
		.bind<monostate>([](monostate) { return print_time_and_sleep(3); })
		.bind<monostate>([](monostate) { return print_time_and_sleep(3); })
		.upon([](monostate) { cout << time(0) << endl; });
}

int main() {
	simple_math();
	read_from_file();
	print_some_times();
	Scheduler::run_until_shutdown();
	return 0;
}

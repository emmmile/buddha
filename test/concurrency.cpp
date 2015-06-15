#include <cmath>
#include <random>
#include <thread>
#include <mutex>
#include <vector>
#define BOOST_TEST_DYN_LINK        // this is optional
#define BOOST_AUTO_TEST_MAIN
#define BOOST_TEST_MODULE Concurrency
#include <boost/test/unit_test.hpp>
#define BOOST_LOG_DYN_LINK
#include <boost/log/trivial.hpp>
#include "timer.h"
#include "atomic_wrapper.h"
using namespace std;


unsigned int size = 512 * 512;
unsigned int points = 1024 * 1024;
unsigned int threads = 128;


// NO SYNCRONIZATION

vector<uint32_t> data;

void no_syncronization_add ( ) {
	std::mt19937 gen(0);
	std::uniform_int_distribution<int> distribution(0,size-1);

	for ( unsigned int i = 0; i < points; ++i ) {
		data[distribution(gen)]++;
	}
}

BOOST_AUTO_TEST_CASE( no_syncronization ) {
	data.resize(size);
	fill(data.begin(), data.end(), 0);

	timer time;
	vector<thread> test;
	test.resize(threads);
	for ( unsigned int t = 0; t < threads; ++t )
		test[t] = thread(&no_syncronization_add);

	for ( unsigned int t = 0; t < threads; ++t )
		test[t].join();

	unsigned int total = std::accumulate(data.begin(), data.end(), 0);
	BOOST_LOG_TRIVIAL(info) << "sum:  " << total << " (" << threads * points << ")";
	BOOST_LOG_TRIVIAL(info) << "time: " << time.elapsed() << " s";
}



// MUTEXES
mutex single;


void mutex_add ( ) {
	std::mt19937 gen(0);
	std::uniform_int_distribution<int> distribution(0,size-1);

	single.lock();
	for ( unsigned int i = 0; i < points; ++i ) {
		data[distribution(gen)]++;
	}
	single.unlock();
}

BOOST_AUTO_TEST_CASE( mutex_syncronization ) {
	data.resize(size);
	fill(data.begin(), data.end(), 0);

	timer time;
	vector<thread> test;
	test.resize(threads);
	for ( unsigned int t = 0; t < threads; ++t )
		test[t] = thread(&mutex_add);

	for ( unsigned int t = 0; t < threads; ++t )
		test[t].join();

	unsigned int total = std::accumulate(data.begin(), data.end(), 0);
	BOOST_LOG_TRIVIAL(info) << "sum:  " << total << " (" << threads * points << ")";
	BOOST_LOG_TRIVIAL(info) << "time: " << time.elapsed() << " s";
    BOOST_CHECK( total == threads * points );
}








/*void mutex_add_many ( ) {
	std::mt19937 gen(0);
	std::uniform_int_distribution<int> distribution(0,size-1);

	for ( unsigned int i = 0; i < points; ++i ) {
		single.lock();
		data[distribution(gen)]++;
		single.unlock();
	}
}


BOOST_AUTO_TEST_CASE( mutex_syncronization_many ) {
	timer time;
	data.resize(size);
	fill(data.begin(), data.end(), 0);

	vector<thread> test;
	test.resize(threads);
	for ( unsigned int t = 0; t < threads; ++t )
		test[t] = thread(&mutex_add_many);

	for ( unsigned int t = 0; t < threads; ++t )
		test[t].join();

	unsigned int total = std::accumulate(data.begin(), data.end(), 0);
	BOOST_LOG_TRIVIAL(info) << "sum:  " << total << " (" << threads * points << ")";
	BOOST_LOG_TRIVIAL(info) << "time: " << time.elapsed() << " s";
    BOOST_CHECK( total == threads * points );
}*/













vector<std::unique_ptr<std::mutex>> mutexes;
unsigned int blocks = 1024;

void mutexes_add ( ) {
	std::mt19937 gen(0);
	std::uniform_int_distribution<int> distribution(0,size-1);

	for ( unsigned int i = 0; i < points; ++i ) {
		unsigned int index = distribution(gen);
		mutexes[index % blocks]->lock();
		data[index]++;
		mutexes[index % blocks]->unlock();
	}
}

BOOST_AUTO_TEST_CASE( mutexes_syncronization ) {
	data.resize(size);
	fill(data.begin(), data.end(), 0);

	vector<thread> test;
	test.resize(threads);

	for ( unsigned int m = 0; m < blocks; ++m )
		mutexes.emplace_back(new mutex());

	timer time;
	for ( unsigned int t = 0; t < threads; ++t )
		test[t] = thread(&mutexes_add);

	for ( unsigned int t = 0; t < threads; ++t )
		test[t].join();

	unsigned int total = std::accumulate(data.begin(), data.end(), 0);
	BOOST_LOG_TRIVIAL(info) << "sum:  " << total << " (" << threads * points << ")";
	BOOST_LOG_TRIVIAL(info) << "time: " << time.elapsed() << " s";
    BOOST_CHECK( total == threads * points );
}









vector<atomic_wrapper<uint32_t>> atomicdata;

void atomic_add ( ) {
	std::mt19937 gen(0);
	std::uniform_int_distribution<int> distribution(0,size-1);

	for ( unsigned int i = 0; i < points; ++i )
		++atomicdata[distribution(gen)];
}

BOOST_AUTO_TEST_CASE( atomic_syncronization ) {
	for ( unsigned int i = 0; i < size; ++i)
		atomicdata.emplace_back(0);

	timer time;
	vector<thread> test;
	test.resize(threads);
	for ( unsigned int t = 0; t < threads; ++t )
		test[t] = thread(&atomic_add);

	for ( unsigned int t = 0; t < threads; ++t )
		test[t].join();

	unsigned int total = 0;
	for ( unsigned int i = 0; i < size; ++i) total += atomicdata[i].load();
	BOOST_LOG_TRIVIAL(info) << "sum:  " << total << " (" << threads * points << ")";
	BOOST_LOG_TRIVIAL(info) << "time: " << time.elapsed() << " s";
    BOOST_CHECK( total == threads * points );
}





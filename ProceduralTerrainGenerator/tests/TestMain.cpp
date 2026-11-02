#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include <iostream>
#include <ostream>

int main(int argc, char* argv[])
{
	//Generators log every call to std::cout - keep the real stream for doctest and silence the rest.
	//A null buffer only makes the insertions fail, so it is also safe on the thread pool
	std::streambuf* original = std::cout.rdbuf();
	std::ostream reportStream(original);
	std::cout.rdbuf(nullptr);

	doctest::Context context(argc, argv);
	context.setCout(&reportStream);
	int result = context.run();

	std::cout.rdbuf(original);
	return result;
}

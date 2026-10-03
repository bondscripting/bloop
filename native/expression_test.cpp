#include "expression.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

struct TestCase
{
	std::string expression;
	double expected;
};

void RequireNear(double actual, double expected, const std::string& expression)
{
	if (std::abs(actual - expected) > 1e-12)
	{
		throw std::runtime_error("Unexpected result for expression: " + expression);
	}
}

void RequireInvalid(const std::string& expression, const bloop::Variables& variables)
{
	try
	{
		bloop::EvaluateExpression(expression, variables);
	}
	catch (const std::invalid_argument&)
	{
		return;
	}
	throw std::runtime_error("Expected invalid expression: " + expression);
}

}

int main()
{
	const bloop::Variables variables = {
		{"size", 20.0},
		{"corner", 8.0},
		{"thickness", 3.0},
		{"pad", 1.0},
		{"size1", 10.0},
		{"size2", 11.0},
		{"cut", 1.0},
		{"gap", 1.0},
	};

	const std::vector<TestCase> cases = {
		{"size - corner", 12.0},
		{"size / 2", 10.0},
		{"(size - corner) / 2", 6.0},
		{"size + 2*pad", 22.0},
		{"-size1 + thickness", -7.0},
		{"thickness + cut", 4.0},
		{"-thickness - gap", -4.0},
		{"0xffffffff", 4294967295.0},
		{"1 / 2", 0.5},
		{"1.25e2 + .5", 125.5},
		{"--2", 2.0},
	};

	try
	{
		for (const auto& test : cases)
		{
			RequireNear(bloop::EvaluateExpression(test.expression, variables), test.expected, test.expression);
		}
		RequireInvalid("missing + 1", variables);
		RequireInvalid("size / 0", variables);
		RequireInvalid("size // 2", variables);
		RequireInvalid("__import__('os')", variables);
		RequireInvalid("(size + 1", variables);
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
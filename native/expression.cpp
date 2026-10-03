#include "expression.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <system_error>

namespace bloop
{
namespace
{

class Parser
{
public:
	Parser(std::string_view expression, const Variables& variables)
		: expression_(expression), variables_(variables)
	{
	}

	double Parse()
	{
		const double result = ParseExpression();
		SkipWhitespace();
		if (position_ != expression_.size())
		{
			Fail("unexpected character");
		}
		return result;
	}

private:
	double ParseExpression()
	{
		double value = ParseTerm();
		while (true)
		{
			SkipWhitespace();
			if (Consume('+'))
			{
				value += ParseTerm();
			}
			else if (Consume('-'))
			{
				value -= ParseTerm();
			}
			else
			{
				return value;
			}
		}
	}

	double ParseTerm()
	{
		double value = ParseUnary();
		while (true)
		{
			SkipWhitespace();
			if (Consume('*'))
			{
				value *= ParseUnary();
			}
			else if (Consume('/'))
			{
				const double divisor = ParseUnary();
				if (divisor == 0.0)
				{
					Fail("division by zero");
				}
				value /= divisor;
			}
			else
			{
				return value;
			}
		}
	}

	double ParseUnary()
	{
		SkipWhitespace();
		if (Consume('+'))
		{
			return ParseUnary();
		}
		if (Consume('-'))
		{
			return -ParseUnary();
		}
		return ParsePrimary();
	}

	double ParsePrimary()
	{
		SkipWhitespace();
		if (Consume('('))
		{
			const double value = ParseExpression();
			SkipWhitespace();
			if (!Consume(')'))
			{
				Fail("expected ')'");
			}
			return value;
		}
		if (position_ == expression_.size())
		{
			Fail("expected a value");
		}

		const char current = expression_[position_];
		if (IsDigit(current) || current == '.')
		{
			return ParseNumber();
		}
		if (IsIdentifierStart(current))
		{
			return ParseIdentifier();
		}
		Fail("expected a number, variable, or '('");
	}

	double ParseNumber()
	{
		const std::size_t start = position_;
		if (expression_[position_] == '0' && position_ + 1 < expression_.size() &&
			(expression_[position_ + 1] == 'x' || expression_[position_ + 1] == 'X'))
			{
			position_ += 2;
			const std::size_t digitsStart = position_;
			while (position_ < expression_.size() && IsHexDigit(expression_[position_]))
			{
				++position_;
			}
			if (position_ == digitsStart)
			{
				Fail("expected hexadecimal digits");
			}

			std::uint64_t value = 0;
			const char* begin = expression_.data() + digitsStart;
			const char* end = expression_.data() + position_;
			const auto parsed = std::from_chars(begin, end, value, 16);
			if (parsed.ec != std::errc{} || parsed.ptr != end)
			{
				Fail("hexadecimal literal is out of range");
			}
			return static_cast<double>(value);
		}

		bool hasDigits = false;
		while (position_ < expression_.size() && IsDigit(expression_[position_]))
		{
			++position_;
			hasDigits = true;
		}
		if (position_ < expression_.size() && expression_[position_] == '.')
		{
			++position_;
			while (position_ < expression_.size() && IsDigit(expression_[position_]))
			{
				++position_;
				hasDigits = true;
			}
		}
		if (!hasDigits)
		{
			Fail("expected digits in number");
		}
		if (position_ < expression_.size() &&
			(expression_[position_] == 'e' || expression_[position_] == 'E'))
			{
			++position_;
			if (position_ < expression_.size() &&
				(expression_[position_] == '+' || expression_[position_] == '-'))
				{
				++position_;
			}
			const std::size_t exponentStart = position_;
			while (position_ < expression_.size() && IsDigit(expression_[position_]))
			{
				++position_;
			}
			if (position_ == exponentStart)
			{
				Fail("expected exponent digits");
			}
		}

		double value = 0.0;
		const char* begin = expression_.data() + start;
		const char* end = expression_.data() + position_;
		const auto parsed = std::from_chars(begin, end, value, std::chars_format::general);
		if (parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(value))
		{
			Fail("number is out of range");
		}
		return value;
	}

	double ParseIdentifier()
	{
		const std::size_t start = position_++;
		while (position_ < expression_.size() && IsIdentifierContinue(expression_[position_]))
		{
			++position_;
		}
		const std::string name(expression_.substr(start, position_ - start));
		const auto found = variables_.find(name);
		if (found == variables_.end())
		{
			Fail("unknown variable '" + name + "'");
		}
		return found->second;
	}

	void SkipWhitespace()
	{
		while (position_ < expression_.size() && IsWhitespace(expression_[position_]))
		{
			++position_;
		}
	}

	bool Consume(char expected)
	{
		if (position_ < expression_.size() && expression_[position_] == expected)
		{
			++position_;
			return true;
		}
		return false;
	}

	[[noreturn]] void Fail(const std::string& message) const
	{
		throw std::invalid_argument(
			"Invalid expression at position " + std::to_string(position_) + ": " + message);
	}

	static bool IsWhitespace(char value)
	{
		return value == ' ' || value == '\t' || value == '\n' || value == '\r';
	}

	static bool IsDigit(char value)
	{
		return value >= '0' && value <= '9';
	}

	static bool IsHexDigit(char value)
	{
		return IsDigit(value) || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
	}

	static bool IsIdentifierStart(char value)
	{
		return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_';
	}

	static bool IsIdentifierContinue(char value)
	{
		return IsIdentifierStart(value) || IsDigit(value);
	}

	std::string_view expression_;
	const Variables& variables_;
	std::size_t position_ = 0;
};

}

double EvaluateExpression(std::string_view expression, const Variables& variables)
{
	return Parser(expression, variables).Parse();
}

}
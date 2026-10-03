#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace bloop
{

using Variables = std::unordered_map<std::string, double>;

double EvaluateExpression(std::string_view expression, const Variables& variables);

}
#include "scene.h"
#include "expression.h"
#include "lodepng.h"
#include "tinyxml2.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bloop
{
namespace
{

constexpr double SmallFloat = 1.0 / 65536.0;

struct Color
{
	std::uint8_t r = 0;
	std::uint8_t g = 0;
	std::uint8_t b = 0;
	std::uint8_t a = 0;

	bool operator==(const Color& other) const
	{
		return r == other.r && g == other.g && b == other.b && a == other.a;
	}
};

using ParameterMap = std::map<std::string, std::optional<std::string>>;
using RawArguments = std::map<std::string, std::string>;

enum class ObjectType
{
	Group,
	Circle,
	Ellipse,
	Rectangle,
	Union,
	Intersect,
	Subtract,
	Stack,
	Rotate,
	Scale,
	Shear
};

struct NodeSpec
{
	ObjectType type = ObjectType::Group;
	RawArguments arguments;
	std::vector<NodeSpec> children;
};

struct Definition
{
	ParameterMap parameters;
	NodeSpec child;
};

struct CommandLine
{
	std::string inputFile;
	std::string outputFile = "bloop.png";
	RawArguments arguments;
	std::vector<int> geometry;
	int samples = 1;
};

[[noreturn]] void Fail(const std::string& message)
{
	throw std::runtime_error(message);
}

ParameterMap CoreObjectParameters()
{
	return {{"color", "color"}, {"x", "0"}, {"y", "0"}};
}

void ParseParameters(const tinyxml2::XMLElement* element, ParameterMap& parameters)
{
	for (auto* child = element->FirstChildElement(); child; child = child->NextSiblingElement())
	{
		if (std::string_view(child->Name()) != "param")
		{
			Fail("Unexpected element <" + std::string(child->Name()) + "> in <params>.");
		}

		std::optional<std::string> name;
		std::optional<std::string> defaultValue;
		for (auto* attribute = child->FirstAttribute(); attribute; attribute = attribute->Next())
		{
			const std::string_view attributeName(attribute->Name());
			if (attributeName == "name")
			{
				name = attribute->Value();
			}
			else if (attributeName == "default")
			{
				defaultValue = attribute->Value();
			}
			else
			{
				Fail("Unexpected attribute '" + std::string(attribute->Name()) + "'.");
			}
		}
		if (!name || name->empty())
		{
			Fail("Parameter name is not defined.");
		}
		if (parameters.find(*name) != parameters.end())
		{
			Fail("Duplicate parameter name '" + *name + "'.");
		}
		parameters.emplace(*name, defaultValue);
	}
}

RawArguments ValidateArguments(const RawArguments& supplied, const ParameterMap& parameters,
	const std::string& typeName)
	{
	RawArguments result = supplied;
	for (const auto& [name, value] : supplied)
	{
		if (parameters.find(name) == parameters.end())
		{
			Fail("Object type '" + typeName + "' does not have a parameter named '" + name + "'.");
		}
	}
	for (const auto& [name, defaultValue] : parameters)
	{
		if (result.find(name) == result.end())
		{
			if (!defaultValue || defaultValue->empty())
			{
				Fail("Object of type '" + typeName + "' is missing argument for parameter '" + name + "' with no default value.");
			}
			result.emplace(name, *defaultValue);
		}
	}
	return result;
}

RawArguments ReadAttributes(const tinyxml2::XMLElement* element)
{
	RawArguments arguments;
	for (auto* attribute = element->FirstAttribute(); attribute; attribute = attribute->Next())
	{
		arguments.emplace(attribute->Name(), attribute->Value());
	}
	return arguments;
}

class ObjectBuilder
{
public:
	void ParseDefinitions(const tinyxml2::XMLElement* element)
	{
		for (auto* child = element->FirstChildElement(); child; child = child->NextSiblingElement())
		{
			ParseDefinition(child);
		}
	}

	NodeSpec ParseObject(const tinyxml2::XMLElement* element) const
	{
		const std::string typeName = element->Name();
		const RawArguments attributes = ReadAttributes(element);
		const auto definition = definitions_.find(typeName);
		if (definition != definitions_.end())
		{
			if (element->FirstChildElement())
			{
				Fail("Object of type '" + typeName + "' cannot have children.");
			}
			NodeSpec object;
			object.type = ObjectType::Group;
			object.arguments = ValidateArguments(attributes, definition->second.parameters, typeName);
			object.children.push_back(definition->second.child);
			return object;
		}

		const auto type = BuiltinType(typeName);
		if (!type)
		{
			Fail("Unknown object type '" + typeName + "'.");
		}

		ParameterMap parameters = CoreObjectParameters();
		bool allowsManyChildren = false;
		bool allowsOneChild = false;
		switch (*type)
		{
		case ObjectType::Circle:
			parameters.emplace("radius", std::nullopt);
			break;
		case ObjectType::Ellipse:
		case ObjectType::Rectangle:
			parameters.emplace("height", std::nullopt);
			parameters.emplace("width", std::nullopt);
			break;
		case ObjectType::Union:
		case ObjectType::Intersect:
		case ObjectType::Subtract:
		case ObjectType::Stack:
			allowsManyChildren = true;
			break;
		case ObjectType::Rotate:
			parameters.emplace("angle", "0");
			allowsOneChild = true;
			break;
		case ObjectType::Scale:
			parameters.emplace("scx", "1");
			parameters.emplace("scy", "1");
			allowsOneChild = true;
			break;
		case ObjectType::Shear:
			parameters.emplace("shx", "0");
			parameters.emplace("shy", "0");
			allowsOneChild = true;
			break;
		case ObjectType::Group:
			break;
		}

		NodeSpec object;
		object.type = *type;
		object.arguments = ValidateArguments(attributes, parameters, typeName);
		for (auto* child = element->FirstChildElement(); child; child = child->NextSiblingElement())
		{
			object.children.push_back(ParseObject(child));
		}
		if (!allowsManyChildren && !allowsOneChild && !object.children.empty())
		{
			Fail("Object of type '" + typeName + "' cannot have children.");
		}
		if (allowsOneChild && object.children.size() > 1)
		{
			Fail("Object of type '" + typeName + "' can have at most one child.");
		}
		return object;
	}

private:
	void ParseDefinition(const tinyxml2::XMLElement* element)
	{
		const std::string typeName = element->Name();
		if (BuiltinType(typeName) || definitions_.find(typeName) != definitions_.end())
		{
			Fail("Duplicate definition of object type '" + typeName + "'.");
		}

		ParameterMap parameters = CoreObjectParameters();
		const tinyxml2::XMLElement* childObject = nullptr;
		bool parsedParameters = false;
		for (auto* child = element->FirstChildElement(); child; child = child->NextSiblingElement())
		{
			if (std::string_view(child->Name()) == "params")
			{
				if (parsedParameters || childObject)
				{
					Fail("Unexpected element <params>.");
				}
				ParseParameters(child, parameters);
				parsedParameters = true;
			}
			else
			{
				if (childObject)
				{
					Fail("Object of type '" + typeName + "' can have at most one child.");
				}
				childObject = child;
			}
		}
		if (!childObject)
		{
			Fail("No child in user defined object type '" + typeName + "'.");
		}
		Definition definition{std::move(parameters), ParseObject(childObject)};
		definitions_.emplace(typeName, std::move(definition));
	}

	static std::optional<ObjectType> BuiltinType(const std::string& name)
	{
		static const std::unordered_map<std::string, ObjectType> types = {
			{"circle", ObjectType::Circle},
			{"ellipse", ObjectType::Ellipse},
			{"rectangle", ObjectType::Rectangle},
			{"union", ObjectType::Union},
			{"intersect", ObjectType::Intersect},
			{"subtract", ObjectType::Subtract},
			{"stack", ObjectType::Stack},
			{"rotate", ObjectType::Rotate},
			{"scale", ObjectType::Scale},
			{"shear", ObjectType::Shear}
		};
		const auto found = types.find(name);
		return found == types.end() ? std::nullopt : std::optional<ObjectType>(found->second);
	}

	std::unordered_map<std::string, Definition> definitions_;
};

Variables ResolveArguments(const RawArguments& arguments, const Variables& parentArguments)
{
	Variables resolved;
	for (const auto& [name, expression] : arguments)
	{
		resolved.emplace(name, EvaluateExpression(expression, parentArguments));
	}
	return resolved;
}

Color ColorFromRGBA(double value)
{
	if (!std::isfinite(value) || std::trunc(value) != value ||
		value < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
		value >= 18446744073709551616.0)
		{
		Fail("Color must be an integer in the signed or unsigned 64-bit range.");
	}
	const std::uint64_t packed = value < 0
		? static_cast<std::uint64_t>(static_cast<std::int64_t>(value))
		: static_cast<std::uint64_t>(value);
	return {
		static_cast<std::uint8_t>((packed >> 24) & 0xff),
		static_cast<std::uint8_t>((packed >> 16) & 0xff),
		static_cast<std::uint8_t>((packed >> 8) & 0xff),
		static_cast<std::uint8_t>(packed & 0xff)
	};
}

std::uint8_t RoundByte(double value)
{
	const double rounded = std::nearbyint(value);
	return static_cast<std::uint8_t>(std::clamp(rounded, 0.0, 255.0));
}

Color InterpolateColors(const Color& a, const Color& b, double rgbT, double alphaT)
{
	return {
		RoundByte((1.0 - rgbT) * a.r + rgbT * b.r),
		RoundByte((1.0 - rgbT) * a.g + rgbT * b.g),
		RoundByte((1.0 - rgbT) * a.b + rgbT * b.b),
		RoundByte((1.0 - alphaT) * a.a + alphaT * b.a)
	};
}

Color CompositeColors(const Color& base, const Color& over)
{
	const double baseAlpha = base.a / 255.0;
	const double overAlpha = over.a / 255.0;
	const double outAlpha = baseAlpha + overAlpha * (1.0 - baseAlpha);
	if (outAlpha <= 0.0)
	{
		return {};
	}
	return {
		RoundByte((base.r * baseAlpha + over.r * overAlpha * (1.0 - baseAlpha)) / outAlpha),
		RoundByte((base.g * baseAlpha + over.g * overAlpha * (1.0 - baseAlpha)) / outAlpha),
		RoundByte((base.b * baseAlpha + over.b * overAlpha * (1.0 - baseAlpha)) / outAlpha),
		RoundByte(outAlpha * 255.0)
	};
}

class Object
{
public:
	static Object Create(const NodeSpec& spec, const Variables& parentArguments)
	{
		const Variables resolved = ResolveArguments(spec.arguments, parentArguments);
		Variables childArguments = parentArguments;
		for (const auto& [name, value] : resolved)
		{
			childArguments[name] = value;
		}

		Object object;
		object.type_ = spec.type;
		object.x_ = resolved.at("x");
		object.y_ = resolved.at("y");
		object.color_ = ColorFromRGBA(resolved.at("color"));
		switch (object.type_)
		{
		case ObjectType::Circle:
			object.first_ = resolved.at("radius");
			break;
		case ObjectType::Ellipse:
		case ObjectType::Rectangle:
			object.first_ = resolved.at("width");
			object.second_ = resolved.at("height");
			break;
		case ObjectType::Rotate:
			object.first_ = resolved.at("angle") * (std::acos(-1.0) / 180.0);
			break;
		case ObjectType::Scale:
			object.first_ = resolved.at("scx");
			object.second_ = resolved.at("scy");
			if (object.first_ == 0.0 || object.second_ == 0.0)
			{
				Fail("Scale values 'scx' and 'scy' must be non-zero.");
			}
			break;
		case ObjectType::Shear:
			object.first_ = resolved.at("shx");
			object.second_ = resolved.at("shy");
			object.determinant_ = 1.0 - object.first_ * object.second_;
			if (std::abs(object.determinant_) < SmallFloat)
			{
				Fail("Shear values 'shx' and 'shy' produce a non-invertible transform.");
			}
			break;
		default:
			break;
		}

		object.children_.reserve(spec.children.size());
		for (const auto& child : spec.children)
		{
			object.children_.push_back(Create(child, childArguments));
		}
		return object;
	}

	std::optional<Color> Probe(double x, double y) const
	{
		x -= x_;
		y -= y_;
		switch (type_)
		{
		case ObjectType::Circle:
			return x * x + y * y < first_ * first_ ? std::optional<Color>(color_) : std::nullopt;
		case ObjectType::Ellipse:
		{
			const double width = first_;
			const double height = second_;
			double radius = 0.0;
			double scaleX = 1.0;
			double scaleY = 1.0;
			if (width > 0.0 && height > 0.0)
			{
				if (width >= height)
				{
					radius = width / 2.0;
					scaleY = height / width;
				}
				else
				{
					radius = height / 2.0;
					scaleX = width / height;
				}
			}
			const double localX = (x - radius) / scaleX;
			const double localY = (y - radius) / scaleY;
			return localX * localX + localY * localY < radius * radius
				? std::optional<Color>(color_) : std::nullopt;
		}
		case ObjectType::Rectangle:
			return x >= 0.0 && x < first_ && y >= 0.0 && y < second_
				? std::optional<Color>(color_) : std::nullopt;
		case ObjectType::Union:
			for (auto child = children_.rbegin(); child != children_.rend(); ++child)
			{
				if (const auto result = child->Probe(x, y))
				{
					return result;
				}
			}
			return std::nullopt;
		case ObjectType::Intersect:
		{
			std::optional<Color> result;
			for (const auto& child : children_)
			{
				result = child.Probe(x, y);
				if (!result)
				{
					return std::nullopt;
				}
			}
			return result;
		}
		case ObjectType::Subtract:
		{
			if (children_.empty())
			{
				return std::nullopt;
			}
			const auto result = children_.front().Probe(x, y);
			if (!result)
			{
				return std::nullopt;
			}
			for (std::size_t index = 1; index < children_.size(); ++index)
			{
				if (children_[index].Probe(x, y))
				{
					return std::nullopt;
				}
			}
			return result;
		}
		case ObjectType::Stack:
		{
			std::optional<Color> result;
			for (const auto& child : children_)
			{
				const auto childColor = child.Probe(x, y);
				if (!childColor)
				{
					continue;
				}
				result = result ? CompositeColors(*result, *childColor) : childColor;
				if (result->a >= 255)
				{
					break;
				}
			}
			return result;
		}
		case ObjectType::Rotate:
		{
			const double cosine = std::cos(-first_);
			const double sine = std::sin(-first_);
			return ProbeFirstChild(x * cosine - y * sine, x * sine + y * cosine);
		}
		case ObjectType::Scale:
			return ProbeFirstChild(x / first_, y / second_);
		case ObjectType::Shear:
			return ProbeFirstChild((x - first_ * y) / determinant_, (y - second_ * x) / determinant_);
		case ObjectType::Group:
			return ProbeFirstChild(x, y);
		}
		return std::nullopt;
	}

private:
	std::optional<Color> ProbeFirstChild(double x, double y) const
	{
		return children_.empty() ? std::nullopt : children_.front().Probe(x, y);
	}

	ObjectType type_ = ObjectType::Group;
	double x_ = 0.0;
	double y_ = 0.0;
	double first_ = 0.0;
	double second_ = 0.0;
	double determinant_ = 1.0;
	Color color_;
	std::vector<Object> children_;
};

Color BlendColors(const std::vector<std::pair<Color, int>>& colorsAndWeights)
{
	Color result;
	double totalRGBWeight = 0.0;
	double totalAlphaWeight = 0.0;
	for (const auto& [color, weight] : colorsAndWeights)
	{
		const double rgbWeight = weight * color.a;
		totalRGBWeight += rgbWeight;
		totalAlphaWeight += weight;
		const double rgbT = rgbWeight / std::max(totalRGBWeight, SmallFloat);
		const double alphaT = weight / std::max(totalAlphaWeight, SmallFloat);
		result = InterpolateColors(result, color, rgbT, alphaT);
	}
	return result;
}

Color ProbeWithDefault(const Object& scene, double x, double y, const Color& background)
{
	const auto color = scene.Probe(x, y);
	return color ? *color : background;
}

Color ProbeHighResolution(const Object& scene, int x, int y, const Color& background, int samples)
{
	const double offset = 1.0 / (samples * 2.0);
	std::vector<std::pair<Color, int>> histogram;
	for (int j = 0; j < samples; ++j)
	{
		const double ySample = static_cast<double>(j) / samples;
		for (int i = 0; i < samples; ++i)
		{
			const double xSample = static_cast<double>(i) / samples;
			const Color color = ProbeWithDefault(scene, x + offset + xSample, y + offset + ySample, background);
			auto existing = std::find_if(histogram.begin(), histogram.end(), [&color](const auto& item)
			{
				return item.first == color;
			});
			if (existing == histogram.end())
			{
				histogram.emplace_back(color, 1);
			}
			else
			{
				++existing->second;
			}
		}
	}
	std::stable_sort(histogram.begin(), histogram.end(), [](const auto& a, const auto& b)
	{
		return a.second > b.second;
	});
	return BlendColors(histogram);
}

void DrawImage(const std::string& outputFile, const Object& scene, const Color& background,
	const std::vector<int>& geometry, int samples)
	{
	const int width = geometry[0];
	const int height = geometry[1];
	const int xOffset = geometry.size() == 4 ? geometry[2] : 0;
	const int yOffset = geometry.size() == 4 ? geometry[3] : 0;
	if (width <= 0 || height <= 0)
	{
		Fail("Geometry dimensions must be positive.");
	}
	const auto pixelCount = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
	if (pixelCount > std::numeric_limits<std::size_t>::max() / 4)
	{
		Fail("Image dimensions are too large.");
	}
	std::vector<unsigned char> image(static_cast<std::size_t>(pixelCount) * 4);
	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			const Color color = samples == 1
				? ProbeWithDefault(scene, x + xOffset + 0.5, y + yOffset + 0.5, background)
				: ProbeHighResolution(scene, x + xOffset, y + yOffset, background, samples);
			const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
			image[index] = color.r;
			image[index + 1] = color.g;
			image[index + 2] = color.b;
			image[index + 3] = color.a;
		}
	}

	const unsigned error = lodepng::encode(outputFile, image, width, height, LCT_RGBA, 8);
	if (error != 0)
	{
		Fail("Could not write PNG '" + outputFile + "': " + lodepng_error_text(error));
	}
}

int ParseInteger(std::string_view value, const std::string& description)
{
	int result = 0;
	const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
	if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
	{
		Fail(description + " must contain integers.");
	}
	return result;
}

std::vector<int> ParseGeometry(const std::string& value)
{
	std::vector<int> geometry;
	std::size_t start = 0;
	while (start <= value.size())
	{
		const std::size_t comma = value.find(',', start);
		const std::size_t end = comma == std::string::npos ? value.size() : comma;
		geometry.push_back(ParseInteger(std::string_view(value).substr(start, end - start),
			"Geometry passed to -g"));
		if (comma == std::string::npos)
		{
			break;
		}
		start = comma + 1;
	}
	if (geometry.size() != 2 && geometry.size() != 4)
	{
		Fail("Geometry passed to -g must have 2 or 4 arguments.");
	}
	return geometry;
}

CommandLine ParseCommandLine(int argc, char** argv)
{
	CommandLine command;
	for (int index = 1; index < argc;)
	{
		const std::string argument = argv[index++];
		if (argument == "-i" || argument == "-g" || argument == "-o" || argument == "-s")
		{
			if (index >= argc)
			{
				Fail("Missing argument to '" + argument + "'.");
			}
			const std::string value = argv[index++];
			if (argument == "-i")
			{
				command.inputFile = value;
			}
			else if (argument == "-g")
			{
				command.geometry = ParseGeometry(value);
			}
			else if (argument == "-o")
			{
				command.outputFile = value;
			}
			else
			{
				command.samples = ParseInteger(value, "Argument passed to -s");
				if (command.samples <= 0)
				{
					Fail("Argument passed to -s must be 1 or greater.");
				}
			}
		}
		else
		{
			if (index >= argc)
			{
				Fail("Missing argument to '" + argument + "'.");
			}
			command.arguments[argument] = argv[index++];
		}
	}
	if (command.inputFile.empty())
	{
		Fail("Missing input file.");
	}
	if (command.geometry.empty())
	{
		Fail("Missing geometry.");
	}
	return command;
}

std::pair<Variables, NodeSpec> ParseScene(const tinyxml2::XMLElement* root,
	const RawArguments& suppliedArguments)
	{
	ParameterMap parameters = {{"color", "0xffffffff"}};
	ObjectBuilder builder;
	const tinyxml2::XMLElement* objectElement = nullptr;
	bool parsedParameters = false;
	bool parsedDefinitions = false;
	for (auto* child = root->FirstChildElement(); child; child = child->NextSiblingElement())
	{
		const std::string_view tag(child->Name());
		if (tag == "params")
		{
			if (parsedParameters || objectElement)
			{
				Fail("Unexpected element <params>.");
			}
			ParseParameters(child, parameters);
			parsedParameters = true;
		}
		else if (tag == "define")
		{
			if (parsedDefinitions || objectElement)
			{
				Fail("Unexpected element <define>.");
			}
			builder.ParseDefinitions(child);
			parsedDefinitions = true;
		}
		else
		{
			if (objectElement)
			{
				Fail("Unexpected element <" + std::string(child->Name()) + ">.");
			}
			objectElement = child;
		}
	}
	if (!objectElement)
	{
		Fail("No object in scene.");
	}
	const RawArguments validated = ValidateArguments(suppliedArguments, parameters, root->Name());
	const Variables resolved = ResolveArguments(validated, {});
	return {resolved, builder.ParseObject(objectElement)};
}

}

int Run(int argc, char** argv)
{
	try
	{
		const CommandLine command = ParseCommandLine(argc, argv);
		tinyxml2::XMLDocument document;
		const tinyxml2::XMLError result = document.LoadFile(command.inputFile.c_str());
		if (result != tinyxml2::XML_SUCCESS)
		{
			Fail("Could not parse input XML '" + command.inputFile + "': " + document.ErrorStr());
		}
		const tinyxml2::XMLElement* root = document.RootElement();
		if (!root)
		{
			Fail("Input XML has no root element.");
		}
		auto [sceneArguments, sceneSpec] = ParseScene(root, command.arguments);
		const Object scene = Object::Create(sceneSpec, sceneArguments);
		const Color background = ColorFromRGBA(sceneArguments.at("color"));
		DrawImage(command.outputFile, scene, background, command.geometry, command.samples);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}

}
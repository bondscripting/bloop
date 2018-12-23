#!/usr/bin/python3

import math;
from PIL import Image, ImageDraw;
import sys;
import xml.etree.ElementTree as ET;

class BloopException(Exception):
	pass;

def main():
	try:
		inputFileName = None;
		outputFileName = "bloop.png";
		args = {};
		geometry = None;
		numSamples = 1;

		i = 1;
		numArgs = len(sys.argv);

		while (i < numArgs):
			arg = sys.argv[i];
			i += 1;

			if (arg == "-i"):
				if (i < numArgs):
					inputFileName = sys.argv[i];
					i += 1;
				else:
					raise BloopException("Missing argument to '-i'.");

			elif (arg == "-g"):
				if (i < numArgs):
					geometry = tuple(int(v) for v in sys.argv[i].split(","));
					i += 1;
					if ((len(geometry) != 2) and (len(geometry) != 4)):
						raise BloopException("Geometry passed to -g must have 2 or 4 arguments.");
				else:
					raise BloopException("Missing argument to '-g'.");

			elif (arg == "-o"):
				if (i < numArgs):
					outputFileName = sys.argv[i];
					i += 1;
				else:
					raise BloopException("Missing argument to '-o'.");

			elif (arg == "-s"):
				if (i < numArgs):
					numSamples = int(sys.argv[i]);
					if (numSamples <= 0):
						raise BloopException("Argument passed to -s must be 1 or greater.");
					i += 1;
				else:
					raise BloopException("Missing argument to '-s'.");

			else:
				if (i < numArgs):
					args[arg] = sys.argv[i];
					i += 1;
				else:
					raise BloopException("Missing argument to '{0}'.".format(arg));

		if (not inputFileName):
			raise BloopException("Missing input file.");

		if (not geometry):
			raise BloopException("Missing geometry.");

		ProcessInputFile(inputFileName, outputFileName, args, geometry, numSamples);

	except BloopException as e:
		print(str(e));
		exit(1);


def ProcessInputFile(inputFileName, outputFileName, args, geometry, numSamples):
	tree = ET.parse(inputFileName);
	rootElement = tree.getroot();
	resolvedArgs, sceneFactory = ParseScene(rootElement, args);
	scene = sceneFactory.CreateObject(resolvedArgs);
	backColor = ColorFromRGBA(resolvedArgs["color"]);
	DrawImage(outputFileName, scene, backColor, geometry, numSamples);


def ParseScene(element, args):
	parsedDefinitions = False;
	parsers = {**CORE_PARSERS};
	parsedParams = False;
	params = {**SCENE_PARAMS};
	objectFactory = None;

	for child in element:
		if (child.tag == "params"):
			if (parsedParams or objectFactory):
				raise BloopException("Unexpected element <params>.".format(child.tag));
			ParseParameters(child, params);
			parsedParams = True;

		elif (child.tag == "define"):
			if (parsedDefinitions or objectFactory):
				raise BloopException("Unexpected element <define>.".format(child.tag));
			ParseDefinitions(child, parsers);
			parsedDefinitions = True;

		else:
			if (objectFactory):
				raise BloopException("Unexpected element <{0}>.".format(child.tag));
			objectFactory = ParseObject(child, parsers);

	if (not objectFactory):
		raise BloopException("No object in scene.");

	ValidateArguments(args, params, element.tag);
	resolvedArgs = ResolveArgs(args, {});
	return resolvedArgs, objectFactory;


def ParseDefinitions(element, parsers):
	for child in element:
		ParseDefinition(child, parsers);


def ParseDefinition(element, parsers):
	parsedParams = False;
	params = {**CORE_OBJECT_PARAMS};
	childFactory = None;

	if (element.tag in parsers):
		raise BloopException("Duplicate definition of object type '{0}'.".format(element.tag));

	for child in element:
		if (child.tag == "params"):
			if (parsedParams or childFactory):
				raise BloopException("Unexpected element <params>.".format(child.tag));
			ParseParameters(child, params);
			parsedParams = True;

		else:
			if (childFactory):
				raise BloopException("Object of type '{0}' can have at most one child.".format(element.tag));
			childFactory = ParseObject(child, parsers);

	if (not childFactory):
		raise BloopException("No child in user defined object type '{0}'.".format(element.tag));

	parsers[element.tag] = ObjectParser(BaseObject, params, PreparsedChildParser(childFactory));


def ParseParameters(element, params):
	for child in element:
		ParseParameter(child, params);


def ParseParameter(element, params):
	name = None;
	defaultValue = None;
	for k, v in element.attrib.items():
		if (k == "name"):
			name = v;
		elif (k == "default"):
			defaultValue = v;
		else:
			raise BloopException("Unexpected attribute '{0}'.".format(k));

	if (not name):
		raise BloopException("Parameter name is not defined.");

	if name in params:
		raise BloopException("Duplicate parameter name '{0}'.".format(name));

	params[name] = defaultValue;


def ParseObjects(element, parsers):
	objectFactories = [ParseObject(child, parsers) for child in element];
	return objectFactories;


def ParseObject(element, parsers):
	parser = parsers.get(element.tag);
	if (not parser):
		raise BloopException("Unknown object type '{0}'".format(element.tag));
	factory = parser.Parse(element, parsers);
	return factory;


def ValidateArguments(args, params, T):
	for k in args:
		if (not k in params):
			raise BloopException("Object type '{0}' does not have a parameter named '{1}'.".format(T, k));

	for k, v in params.items():
		if (not k in args):
			if (not v):
				raise BloopException("Object of type '{0}' is missing argument for parameter '{1}' with no default value.".format(T, k));
			args[k] = v;


def ResolveArgs(args, parentArgs):
	return {k: eval(v, {}, parentArgs) for k, v in args.items()};


class ObjectParser:
	def __init__(self, T, params, childParser):
		self.T = T;
		self.params = params;
		self.childParser = childParser;

	def Parse(self, element, parsers):
		args = {};
		childFactories = None;

		for k, v in element.attrib.items():
			args[k] = v;

		ValidateArguments(args, self.params, element.tag);
		childFactories = self.childParser.Parse(element, parsers);
		factory = ObjectFactory(self.T, args, childFactories);
		return factory;


class NullChildParser:
	def Parse(self, element, parsers):
		if (len(element) > 0):
			raise BloopException("Object of type '{0}' cannot have children.".format(element.tag));
		return [];


class SingleChildParser:
	def Parse(self, element, parsers):
		if (len(element) > 1):
			raise BloopException("Object of type '{0}' can have at most one child.".format(element.tag));
		childFactories = ParseObjects(element, parsers);
		return childFactories;


class ListChildParser:
	def Parse(self, element, parsers):
		childFactories = ParseObjects(element, parsers);
		return childFactories;


class PreparsedChildParser:
	def __init__(self, childFactory):
		self.childFactory = childFactory;

	def Parse(self, element, parsers):
		if (len(element) > 0):
			raise BloopException("Object of type '{0}' cannot have children.".format(element.tag));
		return [self.childFactory];


class ObjectFactory:
	def __init__(self, T, args, childFactories):
		self.T = T;
		self.args = args;
		self.childFactories = childFactories;

	def CreateObject(self, parentArgs):
		resolvedArgs = ResolveArgs(self.args, parentArgs);
		unifiedArgs = {**parentArgs, **resolvedArgs};
		children = [factory.CreateObject(unifiedArgs) for factory in self.childFactories];
		return self.T(resolvedArgs, children);


class BaseObject:
	def __init__(self, args, children):
		self.x = args["x"];
		self.y = args["y"];
		self.rgba = args["color"];
		self.color = ColorFromRGBA(self.rgba);
		self.children = children;
		pass;

	def ToLocalCoordinates(self, x, y):
		return x - self.x, y - self.y;

	def ProbeHiResWithDefault(self, x, y, defaultColor, numSamples):
		offset = 1 / (numSamples * 2);
		colorHistogram = {};
		for j in range(numSamples):
			yj = j / numSamples;
			for i in range(numSamples):
				xi = i / numSamples;
				color = self.ProbeWithDefault(x + offset + xi, y + offset + yj, defaultColor);
				if (color in colorHistogram):
					colorHistogram[color] += 1;
				else:
					colorHistogram[color] = 1;

		sortedColors = sorted(colorHistogram.items(), key=lambda x: x[1], reverse=True);
		color = BlendColors(sortedColors);
		return color;

	def ProbeWithDefault(self, x, y, defaultColor):
		color = self.Probe(x, y);
		if (not color):
			color = defaultColor;
		return color;

	def Probe(self, x, y):
		x, y = self.ToLocalCoordinates(x, y);
		for child in self.children:
			return child.Probe(x, y);
		return None;


class Circle(BaseObject):
	def __init__(self, args, children):
		super().__init__(args, children);
		self.radius = args["radius"];

	def Probe(self, x, y):
		x, y = self.ToLocalCoordinates(x, y);
		if (((x * x) + (y * y)) < (self.radius * self.radius)):
			return self.color;
		return None;


class Ellipse(BaseObject):
	def __init__(self, args, children):
		super().__init__(args, children);
		width = args["width"];
		height = args["height"];
		if ((width <= 0) or (height <= 0)):
			self.radius = 0;
			self.scx = 1;
			self.scy = 1;
		elif (width >= height):
			self.radius = width / 2;
			self.scx = 1;
			self.scy = height / width;
		else:
			self.radius = height / 2;
			self.scx = width / height;
			self.scy = 1;

	def ToLocalCoordinates(self, x, y):
		x, y = super().ToLocalCoordinates(x, y);
		return (x - self.radius) / self.scx, (y - self.radius) / self.scy;

	def Probe(self, x, y):
		x, y = self.ToLocalCoordinates(x, y);
		if (((x * x) + (y * y)) < (self.radius * self.radius)):
			return self.color;
		return None;


class Rectangle(BaseObject):
	def __init__(self, args, children):
		super().__init__(args, children);
		self.width = args["width"];
		self.height = args["height"];

	def Probe(self, x, y):
		x, y = self.ToLocalCoordinates(x, y);
		if ((x >= 0) and (x < self.width) and (y >= 0) and (y < self.height)):
			return self.color;
		return None;


class Union(BaseObject):
	def __init__(self, args, children):
		super().__init__(args, children);

	def Probe(self, x, y):
		x, y = self.ToLocalCoordinates(x, y);
		for child in reversed(self.children):
			result = child.Probe(x, y);
			if (result):
				return result;
		return None;


class Intersect(BaseObject):
	def __init__(self, args, children):
		super().__init__(args, children);

	def Probe(self, x, y):
		x, y = self.ToLocalCoordinates(x, y);
		result = None;
		for child in self.children:
			result = child.Probe(x, y);
			if (result == None):
				return None;
		return result;


class Subtract(BaseObject):
	def __init__(self, args, children):
		super().__init__(args, children);

	def Probe(self, x, y):
		result = None;
		if (len(self.children) > 0):
			x, y = self.ToLocalCoordinates(x, y);
			result = self.children[0].Probe(x, y);
			for i in range(1, len(self.children)):
				if (self.children[i].Probe(x, y) != None):
					return None;
		return result;


class Rotate(BaseObject):
	def __init__(self, args, children):
		super().__init__(args, children);
		self.angle = math.radians(args["angle"]);

	def ToLocalCoordinates(self, x, y):
		x, y = super().ToLocalCoordinates(x, y);
		cs = math.cos(-self.angle);
		sn = math.sin(-self.angle);
		return (x * cs) - (y * sn), (x * sn) + (y * cs);


class Scale(BaseObject):
	def __init__(self, args, children):
		super().__init__(args, children);
		self.scx = args["scx"];
		self.scy = args["scy"];

	def ToLocalCoordinates(self, x, y):
		x, y = super().ToLocalCoordinates(x, y);
		return x / self.scx, y / self.scy;


class Shear(BaseObject):
	def __init__(self, args, children):
		super().__init__(args, children);
		self.scx = args["shx"];
		self.scy = args["shy"];

	def ToLocalCoordinates(self, x, y):
		x, y = super().ToLocalCoordinates(x, y);
		return x - (self.scx * y), y - (self.scy * x);


def DrawImage(outputFileName, scene, backColor, geometry, numSamples):
	width = geometry[0];
	height = geometry[1];
	xOffset = 0;
	yOffset = 0;
	if (len(geometry) == 4):
		xOffset = geometry[2];
		yOffset = geometry[3];

	image = Image.new("RGBA", (width, height), backColor);
	draw = ImageDraw.Draw(image);

	class CacheElement:
		def init(self):
			self.color = None;
			self.processed = False;

	prevRow = tuple(CacheElement() for x in range(width));
	currentRow = tuple(CacheElement() for x in range(width));

	for y in range(height):
		for x in range(width):
			color = scene.ProbeWithDefault(x + xOffset + 0.5, y + yOffset + 0.5, backColor);

			current = currentRow[x];
			current.color = color;
			current.processed = False;
			process = False;

			if ((y > 0) and (numSamples > 1)):
				j = y - 1;
				for i in range(max(x - 1, 0), min(x + 2, width)):
					prev = prevRow[i];
					if (prev.color != color):
						process = True;
						if (not prev.processed):
							c = scene.ProbeHiResWithDefault(i + xOffset, j + yOffset, backColor, numSamples);
							draw.point((i, j), c);
							prev.processed = True;

			if ((x > 0) and (numSamples > 1)):
				i = x - 1;
				prev = currentRow[i];
				if (prev.color != color):
					process = True;
					if (not prev.processed):
						c = scene.ProbeHiResWithDefault(i + xOffset, y + yOffset, backColor, numSamples);
						draw.point((i, y), c);
						prev.processed = True;

			if (process):
				color = scene.ProbeHiResWithDefault(x + xOffset, y + yOffset, backColor, numSamples);
				current.processed = True;

			draw.point((x, y), color);

		prevRow, currentRow = currentRow, prevRow;

	image.save(outputFileName, "PNG");


def ColorFromRGBA(rgba):
	if (type(rgba) is tuple):
		return rgba;
	r = (rgba >> 24) & 0xff;
	g = (rgba >> 16) & 0xff;
	b = (rgba >> 8) & 0xff;
	a = rgba & 0xff;
	return (r, g, b, a);


def BlendColors(colorsAndWeights):
	result = (0, 0, 0, 0);
	totalRGBWeight = 0;
	totalAlphaWeight = 0;
	for color, weight in colorsAndWeights:
		rgbWeight = weight * color[3];
		totalRGBWeight += rgbWeight;
		totalAlphaWeight += weight;
		rgbT = rgbWeight / max(totalRGBWeight, SMALL_FLOAT);
		alphaT = weight / max(totalAlphaWeight, SMALL_FLOAT);
		result = InterpolateColors(result, color, rgbT, alphaT);
	return result;


def InterpolateColors(colorA, colorB, rgbT, alphaT):
	return (
		round(math.sqrt(Lerp(colorA[0]**2, colorB[0]**2, rgbT))),
		round(math.sqrt(Lerp(colorA[1]**2, colorB[1]**2, rgbT))),
		round(math.sqrt(Lerp(colorA[2]**2, colorB[2]**2, rgbT))),
		round(Lerp(colorA[3], colorB[3], alphaT))
	);


def Lerp(a, b, t):
	return ((1 - t) * a) + (t * b);


SCENE_PARAMS = {
	"color": "0xffffffff"
};

CORE_OBJECT_PARAMS = {
	"x": "0",
	"y": "0",
	"color": "color"
};

CIRCLE_PARAMS = {
	**CORE_OBJECT_PARAMS,
	"radius": None
};

ELLIPSE_PARAMS = {
	**CORE_OBJECT_PARAMS,
	"width": None,
	"height": None
};

RECTANGLE_PARAMS = {
	**CORE_OBJECT_PARAMS,
	"width": None,
	"height": None
};

ROTATE_PARAMS = {
	**CORE_OBJECT_PARAMS,
	"angle": "0"
};

SCALE_PARAMS = {
	**CORE_OBJECT_PARAMS,
	"scx": "1",
	"scy": "1"
};

SHEAR_PARAMS = {
	**CORE_OBJECT_PARAMS,
	"shx": "0",
	"shy": "0"
};

CORE_PARSERS = {
	"circle": ObjectParser(Circle, CIRCLE_PARAMS, NullChildParser()),
	"ellipse": ObjectParser(Ellipse, ELLIPSE_PARAMS, NullChildParser()),
	"rectangle": ObjectParser(Rectangle, RECTANGLE_PARAMS, NullChildParser()),
	"union": ObjectParser(Union, CORE_OBJECT_PARAMS, ListChildParser()),
	"intersect": ObjectParser(Intersect, CORE_OBJECT_PARAMS, ListChildParser()),
	"subtract": ObjectParser(Subtract, CORE_OBJECT_PARAMS, ListChildParser()),
	"rotate": ObjectParser(Rotate, ROTATE_PARAMS, SingleChildParser()),
	"scale": ObjectParser(Scale, SCALE_PARAMS, SingleChildParser()),
	"shear": ObjectParser(Shear, SHEAR_PARAMS, SingleChildParser())
};

SMALL_FLOAT = 1.0 / 2**16;


if __name__ == "__main__":
	main();

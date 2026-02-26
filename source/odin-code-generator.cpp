#include "odin-code-generator.h"
#include <fmt/format.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/descriptor_lite.h>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "odin.pb.h"

using namespace google::protobuf;

struct Context
{
	google::protobuf::io::Printer printer;
	std::string *error;
	std::string_view proto_package;
};

// replaces '.' with '_' in type names.
// idx.e. FirstType.SecondType -> FirstType_SecondType
static std::string ConvertFullTypeName(const std::string_view full_name, const std::string_view package_name)
{
	const size_t offset = package_name.empty() ? 0 : package_name.size() + 1;
	std::string result{full_name.substr(offset)};
	std::replace(result.begin(), result.end(), '.', '_');
	return result;
}

enum class OdinBuiltinType
{
	t_none = 0,
	t_i32 = 1,		   // TYPE_INT32, TYPE_SINT32, TYPE_SFIXED32
	t_i64 = 2,		   // TYPE_INT64, TYPE_SINT64, TYPE_SFIXED64
	t_u32 = 3,		   // TYPE_UINT32, TYPE_FIXED32
	t_u64 = 4,		   // TYPE_UINT64, TYPE_FIXED64
	t_f64 = 5,		   // TYPE_DOUBLE
	t_f32 = 6,		   // TYPE_FLOAT
	t_bool = 7,		   // TYPE_BOOL
	t_enum = 8,		   // TYPE_ENUM
	t_string = 9,	   // TYPE_STRING
	t_byte_slice = 10, // TYPE_BYTES
	t_message = 11,	   // TYPE_MESSAGE, TYPE_GROUP
};

static OdinBuiltinType GetOdinBuiltinType(const FieldDescriptor::Type type)
{
	switch (type)
	{
	case FieldDescriptor::TYPE_DOUBLE:
		return OdinBuiltinType::t_f64;
	case FieldDescriptor::TYPE_FLOAT:
		return OdinBuiltinType::t_f32;
	case FieldDescriptor::TYPE_INT64:
		return OdinBuiltinType::t_i64;
	case FieldDescriptor::TYPE_UINT64:
		return OdinBuiltinType::t_u64;
	case FieldDescriptor::TYPE_INT32:
		return OdinBuiltinType::t_i32;
	case FieldDescriptor::TYPE_FIXED64:
		return OdinBuiltinType::t_u64;
	case FieldDescriptor::TYPE_FIXED32:
		return OdinBuiltinType::t_u32;
	case FieldDescriptor::TYPE_BOOL:
		return OdinBuiltinType::t_bool;
	case FieldDescriptor::TYPE_STRING:
		return OdinBuiltinType::t_string;
	case FieldDescriptor::TYPE_GROUP:
		return OdinBuiltinType::t_message;
	case FieldDescriptor::TYPE_MESSAGE:
		return OdinBuiltinType::t_message;
	case FieldDescriptor::TYPE_BYTES:
		return OdinBuiltinType::t_byte_slice;
	case FieldDescriptor::TYPE_UINT32:
		return OdinBuiltinType::t_u32;
	case FieldDescriptor::TYPE_ENUM:
		return OdinBuiltinType::t_enum;
	case FieldDescriptor::TYPE_SFIXED32:
		return OdinBuiltinType::t_i32;
	case FieldDescriptor::TYPE_SFIXED64:
		return OdinBuiltinType::t_i64;
	case FieldDescriptor::TYPE_SINT32:
		return OdinBuiltinType::t_i32;
	case FieldDescriptor::TYPE_SINT64:
		return OdinBuiltinType::t_i64;
	default:
		return OdinBuiltinType::t_none;
	}
}

static std::string_view GetOdinBuiltinTypeName(const OdinBuiltinType type)
{
	switch (type)
	{
	case OdinBuiltinType::t_none:
		return "";
	case OdinBuiltinType::t_i32:
		return "i32";
	case OdinBuiltinType::t_i64:
		return "i64";
	case OdinBuiltinType::t_u32:
		return "u32";
	case OdinBuiltinType::t_u64:
		return "u64";
	case OdinBuiltinType::t_f64:
		return "f64";
	case OdinBuiltinType::t_f32:
		return "f32";
	case OdinBuiltinType::t_bool:
		return "bool";
	case OdinBuiltinType::t_enum:
		return "";
	case OdinBuiltinType::t_string:
		return "string";
	case OdinBuiltinType::t_byte_slice:
		return "[]u8";
	case OdinBuiltinType::t_message:
		return "";
	default:
		return "";
	}
}

static std::string GetOdinFieldTypeName(const FieldDescriptor &field_desc, const std::string_view package_name)
{
	std::string type_name{};

	bool is_map = false;

	if (const Descriptor *const message_desc = field_desc.message_type(); message_desc)
	{
		if (is_map = message_desc->map_key() != nullptr; is_map)
		{
			const std::string key_type_str = GetOdinFieldTypeName(*message_desc->map_key(), package_name);
			const std::string value_type_str = GetOdinFieldTypeName(*message_desc->map_value(), package_name);
			type_name = fmt::format("map[{}]{}", key_type_str, value_type_str);
		}
		else
		{
			type_name = ConvertFullTypeName(message_desc->full_name(), package_name);
		}
	}
	else if (const EnumDescriptor *const enum_desc = field_desc.enum_type(); enum_desc)
	{
		type_name = ConvertFullTypeName(enum_desc->full_name(), package_name);
	}
	else
	{
		const OdinBuiltinType odin_type = GetOdinBuiltinType(field_desc.type());
		type_name = GetOdinBuiltinTypeName(odin_type);
	}

	if (!is_map && field_desc.is_repeated())
	{
		type_name = fmt::format("[]{}", type_name);
	}

	return type_name;
}

// FIXME: make tag name overridable
static void PrintUnionDiscriminantField(const OneofDescriptor &oneof_desc, Context *const context)
{
    std::map<std::string, std::string> vars{
        {"oneof_name", std::string(oneof_desc.name())},
    };
    context->printer.Print(vars, "$oneof_name$_variant: enum {\n");

    vars.clear();
    context->printer.Indent();
    for (int idx = 0; idx < oneof_desc.field_count(); ++idx) {
        const FieldDescriptor *field = oneof_desc.field(idx);
        vars["name"] = field->name();
        vars["number"] = fmt::to_string(idx);
        context->printer.Print(vars, "$name$ = $number$,\n");
    }

    context->printer.Outdent();
    context->printer.Print(vars, "},\n");
}

static bool PrintField(const FieldDescriptor &field_desc, Context *const context)
{
	// TODO: handle default values

	const std::map<std::string, std::string> vars{
		{"name", std::string(field_desc.name())},
		{"odin_type", GetOdinFieldTypeName(field_desc, context->proto_package)},
		{"id", fmt::to_string(field_desc.number())},
		{"proto_type", fmt::to_string((int) field_desc.type())},
		{"packed", fmt::to_string(field_desc.is_packed())}};

	context->printer.Print(vars, "$name$ : $odin_type$ `");

	context->printer.Print(vars, "id:\"$id$\"");
	context->printer.Print(vars, " type:\"$proto_type$\"");

	if (field_desc.is_packable())
	{
		context->printer.Print(vars, " packed:\"$packed$\"");
	}

	if (const Descriptor *const message_desc = field_desc.message_type();
		message_desc && message_desc->map_key() != nullptr)
	{
		const std::string key_type_str = fmt::to_string((int) message_desc->map_key()->type());
		const std::string value_type_str = fmt::to_string((int) message_desc->map_value()->type());

		const std::map<std::string, std::string> map_vars{{"key_type", key_type_str}, {"value_type", value_type_str}};

		context->printer.Print(map_vars, " key_type:\"$key_type$\"");
		context->printer.Print(map_vars, " value_type:\"$value_type$\"");
	}

	context->printer.Print("`,\n");

	return true;
}

static bool SetError(Context *const context, const FieldDescriptor *field, const std::string &message)
{
    SourceLocation loc;
    assert(field->GetSourceLocation(&loc) && "synthetic field passed");

    // default error prefix by protoc seems to be "--odin_out: <filename>: "
    *context->error = fmt::format("line {}: {}", loc.start_line + 1, message);
    return false;
}

// Returns whether a oneof type can be transformed into a tagged union, and whether any attached annotations are valid.
static bool ValidateOneofFieldTypes(const OneofDescriptor &oneof_desc, Context *const context, bool *out_use_tagged_union)
{
    bool force_tagged = false;
    std::unordered_set<std::string_view> used_field_types;

    for (int idx = 0; idx < oneof_desc.field_count(); ++idx)
    {
        const FieldDescriptor *field = oneof_desc.field(idx);
        bool has_extension = field->options().HasExtension(odin);
        std::string_view field_type;

        if (has_extension)
        {
            const OdinOptions &options = field->options().GetExtension(odin);
            // NOTE: we cannot detect has_external() and has_typedef_() being set at the same time as protoc
            // for some bizarre reason does not enforce detection of this, only the last used option is set
            if (options.has_external() && options.external().empty())
            {
                return SetError(context, field, "(odin).external must not be empty");
            }
            else if (options.has_typedef_() && options.typedef_().empty())
            {
                return SetError(context, field, "(odin).typedef must not be empty");
            }

            field_type = options.has_external() ? options.external() : options.typedef_();
            force_tagged = true;
        }
        else if (field->type() == FieldDescriptor::TYPE_MESSAGE)
        {
            field_type = field->message_type()->full_name();
        }
        else
        {
            field_type = GetOdinBuiltinTypeName(GetOdinBuiltinType(field->type()));
        }

        auto [_, inserted] = used_field_types.insert(field_type);
        if (!inserted)
        {
            if (force_tagged)
            {
                return SetError(context, field, fmt::format(
                    "Duplicate Odin union type {} for field {} after applying (odin).external/typedef overrides",
                    field_type, field->name()
                ));
            }

            *out_use_tagged_union = false; // silently fallback to C style #raw_union
            return true;
        }
    }

    *out_use_tagged_union = true;
    return true;
}

// Alias -> Underlying type
using TypeAliasMap = std::unordered_map<std::string_view, std::string_view>;

static bool PrintOneof(const OneofDescriptor &oneof_desc, Context *const context, TypeAliasMap *type_aliases)
{
    bool gen_tagged_union;
    if (!ValidateOneofFieldTypes(oneof_desc, context, &gen_tagged_union))
    {
        return false;
    }

	std::map<std::string, std::string> vars{
		{"name", std::string(oneof_desc.name())},
	};
    if (gen_tagged_union)
    {
        context->printer.Print(vars, "\n$name$: union {\n");
        context->printer.Indent();

        for (int idx = 0; idx < oneof_desc.field_count(); ++idx)
        {
            const FieldDescriptor *field = oneof_desc.field(idx);
            bool has_extension = field->options().HasExtension(odin);
            std::string effective_type;
            if (has_extension)
            {
                const OdinOptions &options = field->options().GetExtension(odin);
                if (options.has_external())
                {
                    effective_type = options.external();
                }
                else if (options.has_typedef_())
                {
                    effective_type = options.typedef_();
                    std::string_view alias = options.typedef_();
                    std::string_view underlying_type = field->type() == FieldDescriptor::TYPE_MESSAGE
                        ? field->message_type()->name() // cannot get Odin builtin type name for messages
                        : GetOdinBuiltinTypeName(GetOdinBuiltinType(field->type()));

                    // TODO: "[]u8" and "[]byte" are supposed to create a collision
                    auto [it, inserted] = type_aliases->emplace(alias, underlying_type);
                    if (!inserted && it->second != underlying_type)
                    {
                        return SetError(context, field, fmt::format(
                            "Type alias '{}' already refers to '{}', cannot redefine as '{}'",
                            alias, it->second, underlying_type
                        ));
                    }
                }
            }
            else if (field->type() == FieldDescriptor::TYPE_MESSAGE)
            {
                effective_type = field->message_type()->name();
            }
            else
            {
                effective_type = GetOdinFieldTypeName(*field, context->proto_package);
            }

            context->printer.Print({{"odin_type", effective_type}}, "$odin_type$,\n");
        }
        // TODO: generate field nr -> union discriminant lookup, as types cannot have field tags
    }
    else
    {
        context->printer.Print(vars, "\n$name$: struct #raw_union {\n");
        context->printer.Indent();

        for (int idx = 0; idx < oneof_desc.field_count(); ++idx)
        {
            if (!PrintField(*oneof_desc.field(idx), context))
            {
                return false;
            }
        }
    }

	context->printer.Outdent();
	context->printer.Print("},\n");
    if (!gen_tagged_union)
    {
        PrintUnionDiscriminantField(oneof_desc, context);
    }

	return true;
}

static bool PrintEnum(const EnumDescriptor &enum_desc, Context *const context)
{
	std::map<std::string, std::string> vars{
		{"name", ConvertFullTypeName(enum_desc.full_name(), context->proto_package)},
	};

	context->printer.Print(vars, "\n$name$ :: enum {\n");
	context->printer.Indent();

	vars.clear();

	for (int idx = 0; idx < enum_desc.value_count(); ++idx)
	{
		const EnumValueDescriptor &value_desc = *enum_desc.value(idx);

		vars["name"] = value_desc.name();
		vars["value"] = fmt::to_string(value_desc.number());

		context->printer.Print(vars, "$name$ = $value$,\n");
	}

	context->printer.Outdent();
	context->printer.Print("}\n");

	return true;
}

static bool PrintMessage(const Descriptor &message_desc, Context *const context, TypeAliasMap *type_aliases)
{
	// we don't generate custom types for maps
	assert(message_desc.map_key() == nullptr);

	const std::map<std::string, std::string> vars{
		{"name", ConvertFullTypeName(message_desc.full_name(), context->proto_package)},
	};

	context->printer.Print(vars, "\n$name$ :: struct {\n");
	context->printer.Indent();

	for (int idx = 0; idx < message_desc.field_count(); ++idx)
	{
		const FieldDescriptor &field = *message_desc.field(idx);
		if (field.containing_oneof() != nullptr)
		{
			// oneof fields will be generated separately
			continue;
		}

		if (!PrintField(field, context))
		{
			return false;
		}
	}

	for (int idx = 0; idx < message_desc.oneof_decl_count(); ++idx)
	{
		if (!PrintOneof(*message_desc.oneof_decl(idx), context, type_aliases))
		{
			return false;
		}
	}

	context->printer.Outdent();
	context->printer.Print("}\n");

	for (int idx = 0; idx < message_desc.nested_type_count(); ++idx)
	{
		const Descriptor &nested_type = *message_desc.nested_type(idx);

		// TODO: find a better way to check if it is a map
		if (nested_type.map_key() != nullptr)
		{
			// Don't generate custom types for maps
			// instead we will generate a native odin map
			// specialization when writing the field
			continue;
		}

		if (!PrintMessage(nested_type, context, type_aliases))
		{
			return false;
		}
	}

	for (int idx = 0; idx < message_desc.enum_type_count(); ++idx)
	{
		if (!PrintEnum(*message_desc.enum_type(idx), context))
		{
			return false;
		}
	}

	return true;
}

static bool PrintFile(const FileDescriptor &file_desc, Context *const context)
{
	// TODO: read this from args
	const std::string base_package_name = "proto";

	const std::string package_name =
		file_desc.package().empty()
			? base_package_name
			: fmt::format("{}_{}", base_package_name, ConvertFullTypeName(file_desc.package(), ""));

	std::map<std::string, std::string> vars{
		{"package", package_name},
	};

	context->printer.Print(vars, "\npackage $package$\n");

	// TODO: handle dependencies, i.e. file_desc.dependency and file_desc.public_dependency

    TypeAliasMap type_aliases_to_generate;
	for (int idx = 0; idx < file_desc.message_type_count(); ++idx)
	{
		if (!PrintMessage(*file_desc.message_type(idx), context, &type_aliases_to_generate))
		{
			return false;
		}
	}

    vars.clear();
    for (const auto &[alias, underlying] : type_aliases_to_generate) {
        vars["alias"] = alias;
        vars["underlying"] = underlying;
        context->printer.Print(vars, "$alias$ :: distinct $underlying$\n");
    }

	for (int idx = 0; idx < file_desc.enum_type_count(); ++idx)
	{
		if (!PrintEnum(*file_desc.enum_type(idx), context))
		{
			return false;
		}
	}

	return true;
}

bool OdinCodeGenerator::Generate(const FileDescriptor *const file, const std::string &parameter,
								 compiler::GeneratorContext *const generator_context, std::string *const error) const
{
	const std::string output_filename = fmt::format("{}.pb.odin", compiler::StripProto(file->name()));
	auto *output = generator_context->Open(output_filename);

	Context context{
		.printer = {output, '$'},
		.error = error,
		.proto_package = file->package(),
	};

	compiler::Version compiler_version;
	generator_context->GetCompilerVersion(&compiler_version);

	const std::map<std::string, std::string> vars{
		{"compiler_version_major", fmt::to_string(compiler_version.major())},
		{"compiler_version_minor", fmt::to_string(compiler_version.minor())},
		{"compiler_version_patch", fmt::to_string(compiler_version.patch())},
	};

	context.printer.Print(
		vars, "// Auto-generated by odin-protoc-plugin (https://github.com/lordhippo/odin-protoc-plugin)\n");
	context.printer.Print(
		vars, "// protoc version: $compiler_version_major$.$compiler_version_minor$.$compiler_version_patch$\n");
	context.printer.Print(
		vars, "// Use with the runtime odin-protobuf library (https://github.com/lordhippo/odin-protobuf)\n");

	return PrintFile(*file, &context);
}

#include "shader_parser.h"
#include "gpu/enum.h"

#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4505)
#define STB_C_LEXER_IMPLEMENTATION
#include <stb_c_lexer.h>
#pragma warning(pop)

namespace
{
	using namespace Graphics::AST;

	// clang-format off
	NodeStorageClass STORAGE_CLASSES[] = 
	{
		"extern",
		"nointerpolation",
		"precise",
		"shared",
		"groupshared",
		"static",
		"uniform",
		"volatile",
	};

	NodeModifier MODIFIERS[] =
	{
		"const",
		"row_major",
		"column_major",
	};

	NodeType BASE_TYPES[] =
	{
		{"void", 0},
		{"float", 4},
		{"float2", 8},
		{"float3", 12},
		{"float4", 16},
		{"float3x3", 36},
		{"float4x4", 64},
		{"int", 4},
		{"int2", 8},
		{"int3", 12},
		{"int4", 16},
		{"uint", 4},
		{"uint2", 8},
		{"uint3", 12},
		{"uint4", 16},
	};

	NodeType SRV_TYPES[] =
	{
		"Buffer", 
		"ByteAddressBuffer", 
		"StructuredBuffer", 
		"Texture1D", 
		"Texture1DArray", 
		"Texture2D",
		"Texture2DArray", 
		"Texture3D", 
		"Texture2DMS", 
		"Texture2DMSArray", 
		"TextureCube",
		"TextureCubeArray",
	};

	NodeType UAV_TYPES[] =
	{
		"RWBuffer",
		"RWByteAddressBuffer",
		"RWStructuredBuffer",
		"RWTexture1D",
		"RWTexture1DArray",
		"RWTexture2D",
		"RWTexture2DArray",
		"RWTexture3D",
	};

	NodeType ENUM_TYPES[] =
	{
		{"AddressingMode", GPU::AddressingMode::MAX},
		{"FilteringMode", GPU::FilteringMode::MAX},
		{"FillMode", GPU::FillMode::MAX},
		{"CullMode", GPU::CullMode::MAX},
		{"BlendType", GPU::BlendType::MAX},
		{"BlendFunc", GPU::BlendFunc::MAX},
		{"CompareMode", GPU::CompareMode::MAX},
		{"StencilFunc", GPU::StencilFunc::MAX},
	};

	const char* BASE_LIBRARY = R"(
		[internal]
		struct SamplerState
		{
			AddressingMode AddressU;
			AddressingMode AddressV;
			AddressingMode AddressW;
			FilteringMode MinFilter;
			FilteringMode MagFilter;
			float MipLODBias;
			uint MaxAnisotropy;
			float BorderColor;//[4];
			float MinLOD;
			float MaxLOD;
		};

		[internal]
		struct BlendState
		{
			uint Enable;
			BlendType SrcBlend;
			BlendType DestBlend;
			BlendFunc BlendOp;
			BlendType SrcBlendAlpha;
			BlendType DestBlendAlpha;
			BlendFunc BlendOpAlpha;
			uint WriteMask;
		};

		[internal]
		struct StencilFaceState
		{
			StencilFunc Fail;
			StencilFunc DepthFail;
			StencilFunc Pass;
			CompareMode Func;
		};

		[internal]
		struct RenderState
		{
			BlendState BlendStates;//[8];

			StencilFaceState StencilFront;
			StencilFaceState StencilBack;
			uint DepthEnable;
			uint DepthWriteMask;
			CompareMode DepthFunc;
			uint StencilEnable;
			uint StencilRef;
			uint StencilRead;
			uint StencilWrite;

			FillMode FillMode;
			CullMode CullMode;
			float DepthBias;
			float SlopeScaledDepthBias;
			uint AntialiasedLineEnable;
		};

		[internal]
		struct Technique
		{
			void VertexShader;
			void GeometryShader;
			void HullShader;
			void DomainShader;
			void PixelShader;
			void ComputeShader;

			RenderState RenderState;
		};
	)";

	// clang-format on
}

namespace Graphics
{
	namespace AST
	{
		NodeDeclaration* NodeShaderFile::FindVariable(const char* name)
		{
			for(auto* parameter : variables_)
				if(parameter->name_ == name)
					return parameter;
			return nullptr;
		}

		NodeDeclaration* NodeType::FindMember(const char* name) const
		{
			for(auto* member : members_)
				if(member->name_ == name)
					return member;
			return nullptr;
		}

		bool NodeType::HasEnumValue(const char* name) const
		{
			if(enumValueFn_ == nullptr)
				return false;
			i32 val = 0;
			return enumValueFn_(val, name);
		}

		const char* NodeType::FindEnumName(i32 val) const { return enumNameFn_(val); }

	} // namespace AST

	ShaderParser::ShaderParser()
	{
		AddNodes(STORAGE_CLASSES);
		AddReserved(STORAGE_CLASSES);

		AddNodes(MODIFIERS);
		AddReserved(MODIFIERS);
		AddNodes(BASE_TYPES);
		AddNodes(SRV_TYPES);
		AddNodes(UAV_TYPES);
		AddNodes(ENUM_TYPES);
	}

	ShaderParser::~ShaderParser() {}

#define CHECK_TOKEN(expectedType, expectedToken)                                                                       \
	if(*expectedToken != '\0' && token_.value_ != expectedToken)                                                       \
	{                                                                                                                  \
		Error(lexCtx, node, ErrorType::UNEXPECTED_TOKEN,                                                               \
		    Core::String().Printf(                                                                                     \
		        "\'%s\': Unexpected token. Did you mean \'%s\'?", token_.value_.c_str(), expectedToken));              \
		return node;                                                                                                   \
	}                                                                                                                  \
	if(token_.type_ != expectedType)                                                                                   \
	{                                                                                                                  \
		Error(lexCtx, node, ErrorType::UNEXPECTED_TOKEN,                                                               \
		    Core::String().Printf("\'%s\': Unexpected token.", token_.value_.c_str()));                                \
		return node;                                                                                                   \
	}                                                                                                                  \
	while(false)

#define PARSE_TOKEN()                                                                                                  \
	if(!NextToken(lexCtx))                                                                                             \
	{                                                                                                                  \
		Error(lexCtx, node, ErrorType::UNEXPECTED_EOF, Core::String().Printf("Unexpected EOF"));                       \
		return node;                                                                                                   \
	}

	void ShaderParser::Parse(const char* shaderFileName, const char* shaderCode, IShaderParserCallbacks* callbacks)
	{
		callbacks_ = callbacks;
		Core::Vector<char> stringStore;
		stringStore.resize(1024 * 1024);

		Core::String patchedShaderCode = BASE_LIBRARY;
		patchedShaderCode.Appendf("\n%s", shaderCode);

		stb_lexer lexCtx;
		stb_c_lexer_init(
		    &lexCtx, patchedShaderCode.begin(), patchedShaderCode.end(), stringStore.data(), stringStore.size());

		fileName_ = shaderFileName;
		AST::NodeShaderFile* shaderFile = ParseShaderFile(lexCtx);

		++shaderFile;
	}

	AST::NodeShaderFile* ShaderParser::ParseShaderFile(stb_lexer& lexCtx)
	{
		AST::NodeShaderFile* node = AddNode<AST::NodeShaderFile>();
		shaderFileNode_ = node;

		while(NextToken(lexCtx))
		{
			switch(token_.type_)
			{
			case AST::TokenType::CHAR:
			{
				auto* attributeNode = ParseAttribute(lexCtx);
				if(attributeNode)
					attributeNodes_.push_back(attributeNode);
			}
			break;
			case AST::TokenType::IDENTIFIER:
			{
				if(token_.value_ == "struct")
				{
					auto* structNode = ParseStruct(lexCtx);
					if(structNode)
					{
						structNodes_.Add(structNode);
						node->structs_.push_back(structNode);
					}
				}
				else
				{
					auto* declNode = ParseDeclaration(lexCtx);
					if(declNode)
					{
						if(declNode->isFunction_)
						{
							node->functions_.push_back(declNode);
						}
						else
						{
							// Trailing ;
							CHECK_TOKEN(AST::TokenType::CHAR, ";");

							node->variables_.push_back(declNode);
						}
					}
				}
			}
			break;
			}
		}

		return node;
	}

	AST::NodeAttribute* ShaderParser::ParseAttribute(stb_lexer& lexCtx)
	{
		AST::NodeAttribute* node = nullptr;

		if(token_.value_ == "[")
		{
			PARSE_TOKEN();
			CHECK_TOKEN(AST::TokenType::IDENTIFIER, "");
			node = AddNode<AST::NodeAttribute>(token_.value_.c_str());

			PARSE_TOKEN();
			if(token_.value_ == "(")
			{
				PARSE_TOKEN();
				while(token_.value_ != ")")
				{
					if(token_.type_ == AST::TokenType::INT || token_.type_ == AST::TokenType::FLOAT ||
					    token_.type_ == AST::TokenType::STRING)
					{
						node->parameters_.emplace_back(token_.value_);
						PARSE_TOKEN();
					}
					else
					{
						Error(lexCtx, node, ErrorType::UNEXPECTED_TOKEN,
						    Core::String().Printf(
						        "\'%s\': Unexpected token. Should be uint, int, float or string value.",
						        token_.value_.c_str()));
						return node;
					}

					if(token_.type_ == AST::TokenType::CHAR && token_.value_ == ",")
					{
						PARSE_TOKEN();
					}
				}
				PARSE_TOKEN();
			}
			CHECK_TOKEN(AST::TokenType::CHAR, "]");
		}
		return node;
	}

	AST::NodeStorageClass* ShaderParser::ParseStorageClass(stb_lexer& lexCtx)
	{
		AST::NodeStorageClass* node = nullptr;
		if(Find(node, token_.value_))
		{
			PARSE_TOKEN();
		}
		return node;
	}

	AST::NodeModifier* ShaderParser::ParseModifier(stb_lexer& lexCtx)
	{
		AST::NodeModifier* node = nullptr;
		if(Find(node, token_.value_))
		{
			PARSE_TOKEN();
		}
		return node;
	}

	AST::NodeType* ShaderParser::ParseType(stb_lexer& lexCtx)
	{
		AST::NodeType* node = nullptr;

		CHECK_TOKEN(AST::TokenType::IDENTIFIER, "");

		// Check it's not a reserved keyword.
		if(reserved_.find(token_.value_) != reserved_.end())
		{
			Error(lexCtx, node, ErrorType::RESERVED_KEYWORD,
			    Core::String().Printf("\'%s\': is a reserved keyword. Type expected.", token_.value_.c_str()));
			return node;
		}

		// Find type.
		if(!Find(node, token_.value_))
		{
			Error(lexCtx, node, ErrorType::TYPE_MISSING,
			    Core::String().Printf("\'%s\': type missing", token_.value_.c_str()));
			return node;
		}

		return node;
	}

	AST::NodeTypeIdent* ShaderParser::ParseTypeIdent(stb_lexer& lexCtx)
	{
		AST::NodeTypeIdent* node = AddNode<AST::NodeTypeIdent>();

		// Parse base modifiers.
		while(auto* modifierNode = ParseModifier(lexCtx))
		{
			node->baseModifiers_.push_back(modifierNode);
		}

		// Parse type.
		node->baseType_ = ParseType(lexCtx);

		// check for template.
		PARSE_TOKEN();
		if(token_.value_ == "<")
		{
			PARSE_TOKEN();

			while(auto* modifierNode = ParseModifier(lexCtx))
			{
				node->templateModifiers_.push_back(modifierNode);
			}

			node->templateType_ = ParseType(lexCtx);

			PARSE_TOKEN();
			CHECK_TOKEN(TokenType::CHAR, ">");
			PARSE_TOKEN();
		}

		return node;
	}

	AST::NodeStruct* ShaderParser::ParseStruct(stb_lexer& lexCtx)
	{
		AST::NodeStruct* node = nullptr;

		// Check struct.
		CHECK_TOKEN(AST::TokenType::IDENTIFIER, "struct");

		// Parse name.
		PARSE_TOKEN();
		CHECK_TOKEN(AST::TokenType::IDENTIFIER, "");
		node = AddNode<AST::NodeStruct>(token_.value_.c_str());

		NodeType* nodeType = nullptr;
		if(Find(nodeType, token_.value_))
		{
			Error(lexCtx, node, ErrorType::TYPE_REDEFINITION,
			    Core::String().Printf("\'%s\': 'struct' type redefinition.", token_.value_.c_str()));
			return node;
		}

		node = AddNode<AST::NodeStruct>(token_.value_.c_str());

		// Check if token is a reserved keyword.
		if(reserved_.find(token_.value_) != reserved_.end())
		{
			Error(lexCtx, node, ErrorType::RESERVED_KEYWORD,
			    Core::String().Printf("\'%s\': is a reserved keyword. Type expected.", token_.value_.c_str()));
			return node;
		}

		//
		node->type_ = AddNode<AST::NodeType>(token_.value_.c_str(), -1);

		// consume attributes.
		node->attributes_ = std::move(attributeNodes_);

		// Parse {
		PARSE_TOKEN();
		if(token_.value_ == "{")
		{
			PARSE_TOKEN();
			while(token_.value_ != "}")
			{
				while(auto* attributeNode = ParseAttribute(lexCtx))
				{
					PARSE_TOKEN();
					attributeNodes_.push_back(attributeNode);
				}

				CHECK_TOKEN(AST::TokenType::IDENTIFIER, "");
				auto* memberNode = ParseDeclaration(lexCtx);
				if(memberNode)
				{
					// Trailing ;
					CHECK_TOKEN(AST::TokenType::CHAR, ";");

					node->type_->members_.push_back(memberNode);
				}
				PARSE_TOKEN();
			}
			PARSE_TOKEN();
		}
		else if(token_.value_ != ";")
		{
			CHECK_TOKEN(AST::TokenType::CHAR, "{");
		}

		// Check ;
		CHECK_TOKEN(AST::TokenType::CHAR, ";");

		return node;
	}


	AST::NodeDeclaration* ShaderParser::ParseDeclaration(stb_lexer& lexCtx)
	{
		AST::NodeDeclaration* node = nullptr;

		Core::Vector<NodeStorageClass*> storageClasses;

		// Parse storage class.
		while(auto* storageClassNode = ParseStorageClass(lexCtx))
		{
			storageClasses.push_back(storageClassNode);
		}

		// Parse type identifier.
		auto* nodeTypeIdent = ParseTypeIdent(lexCtx);

		// Check name.
		CHECK_TOKEN(AST::TokenType::IDENTIFIER, "");

		// Check if identifier is a reserved keyword.
		if(reserved_.find(token_.value_) != reserved_.end())
		{
			Error(lexCtx, node, ErrorType::RESERVED_KEYWORD,
			    Core::String().Printf("\'%s\': is a reserved keyword.", token_.value_.c_str()));
			return node;
		}
		node = AddNode<AST::NodeDeclaration>(token_.value_.c_str());
		node->storageClasses_ = std::move(storageClasses);
		node->type_ = nodeTypeIdent;

		// consume attributes.
		node->attributes_ = std::move(attributeNodes_);

		// Check for params/semantic/assignment/end.
		PARSE_TOKEN();
		if(token_.value_ == "(")
		{
			node->isFunction_ = true;

			PARSE_TOKEN();
			while(token_.value_ != ")")
			{
				CHECK_TOKEN(AST::TokenType::IDENTIFIER, "");
				auto* parameterNode = ParseDeclaration(lexCtx);
				if(parameterNode)
				{
					// Trailing ,
					if(token_.value_ == "," || token_.value_ == ")")
					{
						node->parameters_.push_back(parameterNode);
					}

					if(token_.value_ == ",")
						PARSE_TOKEN();
				}
			}

			PARSE_TOKEN();
		}

		if(token_.value_ == ":")
		{
			// Parse semantic.
			PARSE_TOKEN();
			CHECK_TOKEN(AST::TokenType::IDENTIFIER, "");

			// Check if identifier is a reserved keyword.
			if(reserved_.find(token_.value_) != reserved_.end())
			{
				Error(lexCtx, node, ErrorType::RESERVED_KEYWORD,
				    Core::String().Printf("\'%s\': is a reserved keyword. Semantic expected.", token_.value_.c_str()));
				return node;
			}

			node->semantic_ = token_.value_;

			PARSE_TOKEN();
		}

		if(token_.value_ == "=")
		{
			PARSE_TOKEN();
			node->value_ = ParseValue(lexCtx, node->type_->baseType_);
		}

		if(node->isFunction_)
		{
			// Capture function body.
			if(token_.value_ == "{")
			{
				const char* beginCode = lexCtx.parse_point;
				const char* endCode = nullptr;
				i32 scopeLevel = 1;
				i32 parenLevel = 0;
				i32 bracketLevel = 0;
				while(scopeLevel > 0)
				{
					endCode = lexCtx.parse_point;
					PARSE_TOKEN();
					if(token_.value_ == "{")
						++scopeLevel;
					if(token_.value_ == "}")
						--scopeLevel;
					if(token_.value_ == "(")
						++parenLevel;
					if(token_.value_ == ")")
						--parenLevel;
					if(token_.value_ == "[")
						++bracketLevel;
					if(token_.value_ == "]")
						--bracketLevel;

					if(parenLevel < 0)
					{
						Error(lexCtx, node, ErrorType::UNMATCHED_PARENTHESIS,
						    Core::String().Printf("\'%s\': Unmatched parenthesis.", token_.value_.c_str()));
						return node;
					}
					if(bracketLevel < 0)
					{
						Error(lexCtx, node, ErrorType::UNMATCHED_BRACKET,
						    Core::String().Printf("\'%s\': Unmatched parenthesis.", token_.value_.c_str()));
						return node;
					}
				}

				if(parenLevel > 0)
				{
					Error(lexCtx, node, ErrorType::UNMATCHED_PARENTHESIS,
					    Core::String().Printf("\'%s\': Missing ')'", token_.value_.c_str()));
					return node;
				}
				if(bracketLevel > 0)
				{
					Error(lexCtx, node, ErrorType::UNMATCHED_BRACKET,
					    Core::String().Printf("\'%s\': Missing ']'.", token_.value_.c_str()));
					return node;
				}

				node->value_ = AddNode<AST::NodeValue>();
				node->value_->type_ = AST::ValueType::RAW_CODE;
				node->value_->data_ = Core::String(beginCode, endCode);
			}
		}

		return node;
	}

	AST::NodeValue* ShaderParser::ParseValue(stb_lexer& lexCtx, AST::NodeType* nodeType)
	{
		AST::NodeValue* node = nullptr;

		// Attempt to parse values.
		if(nodeType->members_.size() > 0)
		{
			node = ParseValues(lexCtx, nodeType);
			if(node != nullptr)
			{
				return node;
			}

			// Attempt to parse member value.
			node = ParseMemberValue(lexCtx, nodeType);
			if(node != nullptr)
			{
				return node;
			}
		}

		if(token_.type_ == AST::TokenType::FLOAT || token_.type_ == AST::TokenType::INT ||
		    token_.type_ == AST::TokenType::STRING || token_.type_ == AST::TokenType::IDENTIFIER)
		{
			auto* nodeValue = AddNode<AST::NodeValue>();
			switch(token_.type_)
			{
			case AST::TokenType::FLOAT:
				nodeValue->type_ = AST::ValueType::FLOAT;
				break;
			case AST::TokenType::INT:
				nodeValue->type_ = AST::ValueType::INT;
				break;
			case AST::TokenType::STRING:
				nodeValue->type_ = AST::ValueType::STRING;
				break;
			case AST::TokenType::IDENTIFIER:
			{
				if(nodeType->IsEnum())
				{
					nodeValue->type_ = AST::ValueType::ENUM;
					if(nodeType->HasEnumValue(token_.value_.c_str()))
					{
						nodeValue->data_ = token_.value_;
					}
					else
					{
						Core::String errorStr;
						errorStr.Printf("\'%s\': Invalid value. Expecting enum value for \'%s\'. Valid values are:\n",
						    token_.value_.c_str(), nodeType->name_.c_str());
						for(i32 idx = 0; idx < nodeType->maxEnumValue_; ++idx)
						{
							errorStr.Appendf(" - %s\n", nodeType->FindEnumName(idx));
						}

						Error(lexCtx, node, ErrorType::INVALID_VALUE, errorStr);
						return node;
					}
				}
				else
				{
					// Check if the type is matched by a variable in the shader file.
					if(auto nodeVariable = shaderFileNode_->FindVariable(token_.value_.c_str()))
					{
						if(nodeVariable->type_->baseType_ != nodeType)
						{
							Core::String errorStr;
							errorStr.Printf("\'%s\': has invalid type. Expecting type \'%s\'", token_.value_.c_str(),
							    nodeType->name_.c_str());
							Error(lexCtx, node, ErrorType::INVALID_TYPE, errorStr);
							return node;
						}

						PARSE_TOKEN();
						// delete nodeValue?
						return nodeVariable->value_;
					}
					else
					{
						Core::String errorStr;
						errorStr.Printf("\'%s\': Identifier missing.", token_.value_.c_str());
						Error(lexCtx, node, ErrorType::IDENTIFIER_MISSING, errorStr);
						return node;
					}
				}
			}
			break;
			default:
				break;
			}
			nodeValue->data_ = token_.value_;
			node = nodeValue;
			PARSE_TOKEN();

			return node;
		}
		else
		{
			Error(lexCtx, node, ErrorType::UNEXPECTED_TOKEN,
			    Core::String().Printf("\'%s\': Unexpected token.", token_.value_.c_str()));
		}
		return node;
	}

	AST::NodeValues* ShaderParser::ParseValues(stb_lexer& lexCtx, AST::NodeType* nodeType)
	{
		AST::NodeValues* node = nullptr;

		if(token_.type_ == AST::TokenType::CHAR && token_.value_ == "{")
		{
			node = AddNode<AST::NodeValues>();

			PARSE_TOKEN();
			while(token_.value_ != "}")
			{
				auto nodeValue = ParseMemberValue(lexCtx, nodeType);
				if(nodeValue)
					node->values_.push_back(nodeValue);

				if(token_.value_ == ";")
					return node;

				if(token_.type_ == AST::TokenType::CHAR && token_.value_ == ",")
				{
					PARSE_TOKEN();
				}
			}
			PARSE_TOKEN();
		}

		return node;
	}

	AST::NodeMemberValue* ShaderParser::ParseMemberValue(stb_lexer& lexCtx, AST::NodeType* nodeType)
	{
		AST::NodeMemberValue* node = nullptr;

		if(token_.type_ == AST::TokenType::CHAR && token_.value_ == ".")
		{
			PARSE_TOKEN();
			node = AddNode<AST::NodeMemberValue>();

			node->member_ = token_.value_;

			auto* memberType = nodeType->FindMember(node->member_.c_str());
			if(memberType == nullptr)
			{
				Core::String errorStr;
				errorStr.Printf("\'%s\': Invalid member. Valid values are:\n", token_.value_.c_str());
				for(auto* member : nodeType->members_)
				{
					errorStr.Appendf(" - %s\n", member->name_.c_str());
				}
				Error(lexCtx, node, ErrorType::INVALID_MEMBER, errorStr);
			}

			PARSE_TOKEN();
			CHECK_TOKEN(AST::TokenType::CHAR, "=");

			PARSE_TOKEN();
			node->value_ = ParseValue(lexCtx, memberType->type_->baseType_);
		}

		return node;
	}

	bool ShaderParser::NextToken(stb_lexer& lexCtx)
	{
		while(stb_c_lexer_get_token(&lexCtx) > 0)
		{
			if(lexCtx.token < CLEX_eof)
			{
				char tokenStr[] = {(char)lexCtx.token, '\0'};
				token_.type_ = AST::TokenType::CHAR;
				token_.value_ = tokenStr;
			}
			else if(lexCtx.token == CLEX_id)
			{
				token_.type_ = AST::TokenType::IDENTIFIER;
				token_.value_ = lexCtx.string;
			}
			else if(lexCtx.token == CLEX_floatlit)
			{
				token_.type_ = AST::TokenType::FLOAT;
				token_.value_ = Core::String(lexCtx.where_firstchar, lexCtx.where_lastchar + 1);
			}
			else if(lexCtx.token == CLEX_intlit)
			{
				token_.type_ = AST::TokenType::INT;
				token_.value_ = Core::String(lexCtx.where_firstchar, lexCtx.where_lastchar + 1);
			}
			else if(lexCtx.token == CLEX_dqstring)
			{
				token_.type_ = AST::TokenType::STRING;
				token_.value_ = lexCtx.string;
			}
			token_ = token_;
			return true;
		}
		token_ = AST::Token();
		return false;
	}

	AST::Token ShaderParser::GetToken() const { return token_; }

	void ShaderParser::Error(stb_lexer& lexCtx, AST::Node*& node, ErrorType errorType, Core::StringView errorStr)
	{
		stb_lex_location loc;
		stb_c_lexer_get_location(&lexCtx, lexCtx.parse_point, &loc);

		const char* line_start = lexCtx.parse_point - loc.line_offset;
		const char* line_end = strstr(line_start, "\n");
		Core::String line(line_start, line_end);
		if(callbacks_)
		{
			callbacks_->OnError(
			    errorType, fileName_, loc.line_number, loc.line_offset, line.c_str(), errorStr.ToString().c_str());
		}
		else
		{
			Core::Log("%s(%i-%i): error: %u: %s\n", fileName_, loc.line_number, loc.line_offset, (u32)errorType,
			    errorStr.ToString().c_str());
			Core::Log("%s\n", line.c_str());
			Core::String indent("> ");
			indent.reserve(loc.line_offset + 1);
			for(i32 idx = 1; idx < loc.line_offset; ++idx)
				indent += " ";
			indent += "^";
			Core::Log("> %s\n", indent.c_str());
		}

		node = nullptr;
	}
} // namespace Graphics
